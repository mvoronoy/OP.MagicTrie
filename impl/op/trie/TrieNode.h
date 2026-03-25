#ifndef _OP_TRIE_TRIENODE__H_
#define _OP_TRIE_TRIENODE__H_

#include <op/common/Bitset.h>
#include <op/common/StackAlloc.h>

#include <op/vtm/SegmentManager.h>

#include <op/trie/BitIndexedVector.h>
#include <op/trie/AntiHashTable.h>
#include <op/trie/TriePosition.h>
#include <op/vtm/PersistedReference.h>
#include <op/vtm/StringMemoryManager.h>

namespace OP
{
    namespace trie
    {

        /** Represent single node of Trie*/
        template <class PayloadManager>
        struct TrieNode
        {
            using payload_manager_t = PayloadManager;
            using this_t = TrieNode<payload_manager_t>;
            using atom_t = OP::common::atom_t;
            using NullableAtom = OP::vtm::NullableAtom;
            using dim_t = OP::vtm::dim_t;
            using fast_dim_t = OP::vtm::fast_dim_t;
            using FarAddress = OP::vtm::FarAddress;
            using atom_string_t = OP::common::atom_string_t;
            using atom_string_view_t = OP::common::atom_string_view_t;
            using payload_t = typename payload_manager_t::payload_t;
            using data_storage_t = typename payload_manager_t::data_storage_t;
            using stem_str_address_t = vtm::smm::SmartStringAddress<>;

            /*declare 256-bit presence bitset*/
            using presence_t = common::Bitset<4, std::uint64_t>;

            struct NodeData
            {
                vtm::FarAddress _child = {};
                stem_str_address_t _stem = {};
                data_storage_t _value = {};
            };

            static_assert(std::is_standard_layout_v<NodeData>,
                "self-control of NodeData failed - result structure is not plain");

            using key_value_t = containers::KeyValueContainer< NodeData, this_t >;
            
            constexpr static dim_t expected_magic_word = 0x55AA;

            const dim_t magic_word_c = expected_magic_word;
            presence_t _child_presence, _value_presence;
            /**modification version of node*/
            node_version_t _version;
            FarAddress _hash_table;
            /** Capacity of allocated KeyValueContainer */
            dim_t _capacity;

            TrieNode(dim_t capacity) noexcept
                : _version(0)
                , _capacity(capacity)
            {
                //capacity must be pow of 2 and lay in range [4-256]
                assert(_capacity >= 4 && _capacity <= 256 && ((_capacity - 1) & _capacity) == 0);
            }

            template <class TTopology>
            void create_interior(TTopology& topology)
            {
                wrap_key_value_t wrapper;
                kv_container(topology, wrapper);
                _hash_table = wrapper->create();
            }

            template <class TTopology>
            void destroy_interior(TTopology& topology)
            {
                assert(presence_first_set() == presence_t::nil_c);//all interior must be already deleted
                wrap_key_value_t wrapper;
                kv_container(topology, wrapper);
                wrapper->destroy(_hash_table);
                _hash_table = {};
            }


            /** Create entry of key in the current node.
            * 
            * Node size is grown if needed.
            * 
            * @pre key must not exists yet.
            * 
            * @tparam FInterior functor in form `void(NodeData& new_entry, bool& out_has_value, bool& out_has_child)` - 
            *       where caller is responsible to assign all necessary fields of new_entry and indicate if it contains
            *       data by: `out_has_value = true/false` and indicate if entry contains child by `out_has_child = true/false`
            */ 
            template <class TSegmentTopology, class FInterior>
            void insert(TSegmentTopology& topology, atom_t key, FInterior&& make_interior)
            {
                for (;;)
                {
                    wrap_key_value_t container;
                    kv_container(topology, container); //resolve correct instance implemented by this node

                    auto ins_res = container->insert(key,
                        [&](NodeData& to_construct) {
                            ::new (&to_construct)NodeData;
                            bool has_data = false, has_child = false;
                            make_interior(to_construct, has_data, has_child);
                            if(has_data)
                                _value_presence.set(key); 
                            if(has_child)
                                _child_presence.set(key);
                        });

                    if (ins_res == containers::KvInsert::ok)
                        break;

                    assert(ins_res != containers::KvInsert::already_exists);//only possible reason to be there - capacity is over
                    grow(topology, *container);
                }
                ++_version;
            }

            /**
            * @return true if entire node should be deleted
            */
            template <class TSegmentTopology>
            bool erase(TSegmentTopology& topology, atom_t key, bool erase_data)
            {
                ++_version;
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                stem_str_address_t stem_addr;
                if (erase_data)
                {
                    assert(presence(key));
                    NodeData* node = container->get(key);
                    assert(node);
                    stem_addr = node->_stem;

                    payload_manager_t::destroy(topology, node->_value);
                    if (!_child_presence.get(key))
                    {//in case there is a child, need keep this entry and erase data only
                        //entry totally removed
                        container->erase(key);
                    }
                    _value_presence.clear(key);//must be after `container->erase`
                }

                if (presence(key))
                { //when value/child presented need keep sequence in this node
                    return false;
                }

                if (!stem_addr.is_nil())
                {
                    vtm::StringMemoryManager smm(topology);
                    smm.destroy(stem_addr);
                }

                //_value_presence.clear(key); //it may look duplicate, but isn't
                //@! think to reduce space of hashtable
                return presence_first_set() == presence_t::nil_c; //erase entire node if no more entries
            }

            /**
            *   Frees content of this node and give a caller addresses of all descendant children.
            *   So without recursion caller can destroy children after all.
            *   Node is not destroyed.
            *   @return number of data-slots destroyed
            */
            template <class TSegmentTopology>
            size_t erase_all(TSegmentTopology& topology, std::stack<FarAddress>& child_process)
            {
                size_t data_slots = 0;
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                vtm::StringMemoryManager string_memory_manager(topology);

                for (auto i = presence_first_set(); !vtm::is_nil(i);
                    i = presence_next_set(static_cast<atom_t>(i)))
                {
                    NodeData* node = container->get(static_cast<atom_t>(i));
                    assert(node);
                    if (_value_presence.get(i))
                    {//wipe-out data
                        payload_manager_t::destroy(topology, node->_value);
                        _value_presence.clear(i);
                        ++data_slots;
                    }
                    if (!node->_stem.is_nil())
                    {
                        string_memory_manager.destroy(node->_stem);//alters `->_stem` as well
                    }
                    if (_child_presence.get(i))
                    {//wipe children
                        assert(!node->_child.is_nil());
                        child_process.push(node->_child);
                        _child_presence.clear(i);
                    }
                }
                return data_slots;
            }

            /**
            \tparam F has signature `{user-type} (const NodeData&)`
            */
            template <class TSegmentTopology, class F>
            auto rawc(TSegmentTopology& topology, atom_t key, F&& callback) const
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto node_data = container->cget(key);
                assert(node_data); //must already be allocated
                return callback(*node_data);
            }

            template <class TSegmentTopology, class F>
            auto raw(TSegmentTopology& topology, atom_t key, F&& callback) const
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                NodeData* node_data = container->get(key);
                assert(node_data); //must already be allocated
                return callback(*node_data);
            }

            bool has_child(atom_t key) const
            {
                return _child_presence.get(key);
            }

            template <class TSegmentTopology>
            void set_child(TSegmentTopology& topology, atom_t key, FarAddress address)
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                NodeData* node_data = container->get(key);
                assert(node_data); //there we have only valid pointers
                assert(!_child_presence.get(key));
                set_raw_child(key, *node_data, address);
            }

            void set_raw_child(atom_t key, NodeData& node_data, FarAddress address)
            {
                node_data._child = address;
                _child_presence.set(key);
                ++_version;
            }

            template <class TSegmentTopology>
            void remove_child(TSegmentTopology& topology, atom_t key)
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                NodeData* node_data = container->get(key);
                assert(node_data); //there we have only valid pointers
                assert(_child_presence.get(key));//must exists
                node_data->_child = FarAddress{};
                if(!_value_presence.get(key)) // no more reason to keep entry
                    container->erase(key);
                _child_presence.clear(key);
                ++_version;
            }

            /**Get child address if present, otherwise return null-pos*/
            template <class TSegmentTopology>
            FarAddress get_child(TSegmentTopology& topology, atom_t key) const
            {
                if (!_child_presence.get(key))
                    return FarAddress{};
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto node_data = container->cget(key);
                assert(node_data); //there we have only valid pointers
                return node_data->_child;
            }

            bool has_value(atom_t key) const
            {
                return _value_presence.get(key);
            }

            /**Get associated data if present, otherwise exception is thrown*/
            template <class TSegmentTopology>
            payload_t get_value(TSegmentTopology& topology, atom_t key) const
            {
                if (!_value_presence.get(key))
                    throw std::invalid_argument("key doesn't contain data");
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto node = container->cget(key);
                return payload_manager_t::rawc(topology, node->_value,
                    [](const payload_t& raw) -> payload_t { return raw; });
            }

            template <class TSegmentTopology, class FValueCallback>
            auto get_value(TSegmentTopology& topology, atom_t key, FValueCallback&& callback) const
            {
                if (!_value_presence.get(key))
                    throw std::invalid_argument("key doesn't contain data");
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto node = container->cget(key);
                return payload_manager_t::rawc(topology, node->_value, std::forward<FValueCallback>(callback));
            }

            /**
            *
            *   \tparam TPayload - allowed be either explicit value to assign or lambda with signature `lambda(payload&)`
            */
            template <class TSegmentTopology, class TPayload>
            void set_raw_factory_value(TSegmentTopology& topology, atom_t key, NodeData& node, TPayload&& value)
            {
                payload_manager_t::raw(topology, node._value, std::forward<TPayload>(value));
                _value_presence.set(key);
                ++_version;
            }

            /**
            *
            *   \tparam TPayload - allowed be either explicit value to assign or lambda with signature `lambda(payload&)`
            */
            template <class TSegmentTopology, class TPayload>
            void set_value(TSegmentTopology& topology, atom_t key, TPayload&& value)
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                NodeData* node_data = container->get(key);
                assert(node_data); //there we have only valid pointers
                set_raw_factory_value(topology, key, *node_data, std::forward<TPayload>(value));
            }

            /**@return first position where child or value exists, may return dim_nil_c if node empty*/
            inline NullableAtom first() const noexcept
            {
                return NullableAtom{ this->presence_first_set() };
            }

            /**@return last position where child or value exists, may return dim_nil_c if node empty*/
            inline NullableAtom last() const noexcept
            {
                return NullableAtom{ this->presence_last_set() };
            }

            /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
            NullableAtom next(atom_t previous) const noexcept
            {
                return NullableAtom{ this->presence_next_set(previous) };
            }

            /**@return next or the same position where child or value exists, may return dim_nil_c if no more entries*/
            NullableAtom next_or_this(atom_t previous) const noexcept
            {
                return NullableAtom{ this->presence_next_set_or_this(previous) };
            }

            /**
            * Move entry from this specified by 'key' node that is started on 'in_stem_pos' to another one
            * specified by 'target' address
            *
            * \test
            *   - *case 1*
            *       given: [a->stem(bc)],
            *       when insert: (axy),
            *       expected: [a->stem(null), child[b(stem(c)), x(stem(y))]]
            *   - *case 2*
            *       given: [a->stem(bc)],
            *       when insert: (abd),
            *       expected: [a->stem(b), ->child[c, d]]
            *   - *case 3*
            *       given: [a->stem(bcd)],
            *       when insert: (abxy),
            *       expected: [a->stem(b), child[c(d), x(y)]]
            * @return number of characters moved out to the `target_node`
            */
            template <class TSegmentTopology>
            fast_dim_t move_to(TSegmentTopology& topology, atom_t key, fast_dim_t in_stem_pos,
                vtm::WritableAccess<this_t>& target_node)
            {
                wrap_key_value_t src_container;
                kv_container(topology, src_container); //resolve correct instance implemented by this node

                auto* src_node_data = src_container->get(key);
                assert(src_node_data);
                return move_from_entry(topology, key, *src_node_data, in_stem_pos, target_node);
            }

            /**
            * @return number of characters moved out to the `target_node`
            */ 
            template <class TSegmentTopology>
            fast_dim_t move_from_entry(TSegmentTopology& topology, atom_t source_key, NodeData& source, fast_dim_t in_stem_pos,
                vtm::WritableAccess<this_t>& target_node)
            {
                assert(!source._stem.is_nil()); //call move_to assumes valid stem

                wrap_key_value_t target_container;
                target_node->kv_container(topology, target_container);
                //take stem to memory
                vtm::StringMemoryManager str_manager(topology);
                atom_string_t new_stem_buf;
                str_manager.append_to(
                    source._stem, 
                    new_stem_buf, 
                    in_stem_pos 
                );
                assert(!new_stem_buf.empty()); // empty buffer means nothing to move to new node
                atom_t new_key = new_stem_buf[0]; //new node will consume this character
                if (in_stem_pos > 0)
                { //old stem truncated
                    str_manager.truncate(/*[in, out]*/source._stem, in_stem_pos);
                }
                else
                { // no need for old stem
                    str_manager.destroy(source._stem);//remove previous
                    source._stem = {};
                }

                atom_string_view_t carry_over_stem = OP::utils::subview<atom_string_view_t>(new_stem_buf, 1/*, till the end */);

                target_container->insert(new_key,
                    [&](NodeData& target_data) -> void {
                        target_data = {};
                        if (!carry_over_stem.empty())
                        {
                            target_data._stem = str_manager.smart_insert(carry_over_stem);
                        }

                        //copy data/address to target
                        target_data._child = source._child;
                        target_node->_child_presence.assign(new_key, _child_presence.get(source_key));
                        source._child = target_node.address();
                        _child_presence.set(source_key); //override for a case prev wasn't set
                        if (_value_presence.get(source_key))
                        {
                            //as soon _value_presence cleared, twice destructor wouldn't called
                            target_data._value = source._value;
                            _value_presence.clear(source_key);
                            target_node->_value_presence.set(new_key);
                        }
                        else
                        { // status of 'no-data' must keep the same
                            assert(!target_node->_value_presence.get(new_key));
                        }
                    }
                );

                ++_version;
                ++target_node->_version;
                return static_cast<fast_dim_t>(new_stem_buf.size());
            }

            dim_t capacity() const
            {
                return _capacity;
            }

            FarAddress reindex_table() const
            {
                return this->_hash_table;
            }

            inline std::uint_fast8_t presence(atom_t key) const
            {
                return (_child_presence.get(key) ? Terminality::term_has_child : 0) 
                    | (_value_presence.get(key) ? Terminality::term_has_data : 0);
            }

            inline fast_dim_t presence_first_set() const
            {
                fast_dim_t ch_res = _child_presence.first_set();
                fast_dim_t dt_res = _value_presence.first_set();
                return std::min(dt_res, ch_res);
            }

            /**@return last position where child or value exists, may return dim_nil_c if node empty*/
            inline fast_dim_t presence_last_set() const
            {
                fast_dim_t ch_res = _child_presence.last_set();
                fast_dim_t dt_res = _value_presence.last_set();
                if (vtm::is_nil(ch_res))
                    return dt_res;
                if (vtm::is_nil(dt_res))
                    return ch_res;
                return std::max(dt_res, ch_res); //there strongly non nil
            }

            /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
            inline fast_dim_t presence_next_set(atom_t previous) const
            {
                fast_dim_t ch_res = _child_presence.next_set(previous);
                fast_dim_t dt_res = _value_presence.next_set(previous);
                return std::min(dt_res, ch_res);
            }
            
            /**@return next or the same position where child or value exists, may return dim_nil_c if no more entries*/
            inline fast_dim_t presence_next_set_or_this(atom_t previous) const
            {
                fast_dim_t ch_res = _child_presence.next_set_or_this(previous);
                fast_dim_t dt_res = _value_presence.next_set_or_this(previous);
                return std::min(dt_res, ch_res);
            }


        private:
            template <fast_dim_t size>
            using indexing_table_t = containers::BitIndexedVector<size, NodeData, this_t>;

            using anti_hash_table_t = containers::AntiHashTable<NodeData, this_t>;

            using wrap_key_value_t = Multiimplementation<
                key_value_t, anti_hash_table_t,
                indexing_table_t<4>, indexing_table_t<8>, indexing_table_t<16>, 
                indexing_table_t<32>, indexing_table_t<64>, indexing_table_t<128>
            >;

            template <class TSegmentTopology>
            [[maybe_unused]] key_value_t* select_kv_container_instance(
                TSegmentTopology& topology, wrap_key_value_t& out, dim_t capacity, const FarAddress& residence) const
            {
                switch (capacity)
                {
                case 4:
                    return &out.template construct<indexing_table_t<4>>(topology, residence);
                case 8:
                    return &out.template construct<indexing_table_t<8>>(topology, residence);
                case 16:
                    return &out.template construct<indexing_table_t<16>>(topology, residence);
                case 32:
                    return &out.template construct<indexing_table_t<32>>(topology, residence);
                case 64:
                    return &out.template construct<indexing_table_t<64>>(topology, residence);
                default:
                    assert(false);
                    [[fallthrough]]; //for release build
                case 128:
                    return &out.template construct<indexing_table_t<128>>(topology, residence);
                case 256:
                    return &out.template construct<anti_hash_table_t>(topology, *this, capacity);
                }
            }

            template <class TSegmentTopology>
            [[maybe_unused]] key_value_t* kv_container(TSegmentTopology& topology, wrap_key_value_t& out) const
            {
                return select_kv_container_instance(topology, out, _capacity, _hash_table);
            }

            template <class TSegmentTopology>
            void grow(TSegmentTopology& topology, key_value_t& from_container)
            {
                fast_dim_t new_capacity = _capacity << 1;

                // Hash-table may not be able to grow in one step because of key collision, so place
                // grow code to loop to give several tries (other data-structures should succeed in 1 step).
                for(;; new_capacity <<= 1 )
                {
                    assert(new_capacity <= 256); //must never over grow 256
                    wrap_key_value_t new_container;
                    select_kv_container_instance(topology, new_container, new_capacity, FarAddress{});
                    FarAddress dest_tbl = new_container->create(); //start new container
                    if (new_container->grow_from(from_container))
                    {//success
                        from_container.destroy(_hash_table);
                        _hash_table = dest_tbl;
                        break;
                    }
                    new_container->destroy(dest_tbl); //temp container unsuccess insert, need destroy
                }
                _capacity = new_capacity;
                ++_version;
            }

        };
    } //ns:trie
}//ns:OP

#endif //_OP_TRIE_TRIENODE__H_
