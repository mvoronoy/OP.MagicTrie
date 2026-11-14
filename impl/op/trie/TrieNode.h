#ifndef _OP_TRIE_TRIENODE__H_
#define _OP_TRIE_TRIENODE__H_

#include <op/common/Bitset.h>
#include <op/common/StackAlloc.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>

#include <op/trie/StringMemoryManager.h>
#include <op/trie/BitIndexedVector.h>
#include <op/trie/AntiHashTable.h>
#include <op/trie/TriePosition.h>
#include <op/trie/TrieOptions.h>

namespace OP::trie
{

    /** Represent single node of Trie*/
    template <class PayloadManager>
    struct TrieNode
    {
        using payload_manager_t = PayloadManager;
        using this_t = TrieNode<payload_manager_t>;
        using atom_t = OP::common::atom_t;
        using fast_atom_t = OP::common::fast_atom_t;
        using NullableAtom = OP::vtm::NullableAtom;
        using dim_t = OP::vtm::dim_t;
        using fast_dim_t = OP::vtm::fast_dim_t;
        using FarAddress = OP::vtm::FarAddress;
        using atom_string_t = OP::common::atom_string_t;
        using atom_string_view_t = OP::common::atom_string_view_t;
        using payload_t = typename payload_manager_t::payload_t;
        using data_storage_t = typename payload_manager_t::data_storage_t;
        using stem_str_address_t = smm::SmartStringAddress<>;

        /*declare 256-bit presence bitset*/
        //using presence_t = common::Bitset<4, std::uint64_t>;
        //static_assert(std::is_standard_layout_v<presence_t>,
        //    "self-control of common::Bitset failed - result structure is not plain and cannot be safely persisted");


        struct NodeData
        {
            vtm::FarAddress _child = {};
            stem_str_address_t _stem = {};
            data_storage_t _value = {};
        };

        static_assert(std::is_standard_layout_v<NodeData>,
            "self-control of NodeData failed - result structure is not plain and cannot be safely persisted");

        using key_value_t = containers::KeyValueContainer<NodeData, this_t>;
        
        constexpr static dim_t expected_magic_word = 0x55AA;
        constexpr static dim_t presence_table_size_c = 4;

        const dim_t magic_word_c = expected_magic_word;
        std::uint64_t 
            _child_presence[presence_table_size_c] = {},
            _value_presence[presence_table_size_c] = {};

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

        void create_interior(TrieOptions& options)
        {
            wrap_key_value_t wrapper;
            kv_container(options, wrapper);
            _hash_table = wrapper->create();
        }

        void destroy_interior(TrieOptions& options)
        {
            assert(presence_first_set() == OP::rawbits::nil_c);//all interior must be already deleted
            wrap_key_value_t wrapper;
            kv_container(options, wrapper);
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
        template <class FInterior>
        void insert(TrieOptions& options, atom_t key, FInterior&& make_interior)
        {
            for (;;)
            {
                wrap_key_value_t container;
                kv_container(options, container); //resolve correct instance implemented by this node

                auto ins_res = container->insert(key,
                    [&](NodeData& to_construct) {
                        ::new (&to_construct)NodeData;
                        bool has_data = false, has_child = false;
                        make_interior(to_construct, has_data, has_child);
                        if(has_data)
                            OP::rawbits::set(_value_presence, key); 
                        if(has_child)
                            OP::rawbits::set(_child_presence, key);
                    });

                if (ins_res == containers::KvInsert::ok)
                    break;

                assert(ins_res != containers::KvInsert::already_exists);//only possible reason to be there - capacity is over
                grow(options, *container);
            }
            ++_version;
        }

        template <class TContainer>
        FarAddress erase_entry(
            TrieOptions& options,
            TContainer* container,
            NodeData& entry, fast_atom_t key, bool erase_data, bool erase_child)
        {
            FarAddress result{};
            size_t votes_for_delete = 0;
            if (erase_data)
            {
                assert(OP::rawbits::get(_value_presence, key));
                payload_manager_t::destroy(options, entry._value);
                ++votes_for_delete;
            }
            else
                votes_for_delete += OP::rawbits::get(_value_presence, key) ? 0 : 1; // vote if no value in this position

            if(erase_child)
            {
                assert(OP::rawbits::get(_child_presence, key));
                std::swap(entry._child, result);
                ++votes_for_delete;
            }
            else
                votes_for_delete += OP::rawbits::get(_child_presence, key) ? 0 : 1; // vote if no child in this position

            if(votes_for_delete == 2)
            {
                if (!entry._stem.is_nil())
                {
                    StringMemoryManager smm(options);
                    smm.destroy(entry._stem);
                }
                if(container)
                    container->erase(key);
            }
            return result;
        }

        /**
        * @return true if entire node should be deleted
        */
        template <class OutIter>
        bool erase(TrieOptions& options, atom_t key, bool erase_data, bool erase_child, OutIter& child_to_remove)
        {
            ++_version;
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            NodeData& entry = container->get(key);
            auto child_addr = 
                erase_entry(options, &(*container), entry, key, erase_data, erase_child);
            if(!child_addr.is_nil())
                *child_to_remove++ = std::move(child_addr);
            if (erase_data)
                OP::rawbits::clear(_value_presence, key);//must be after `container->erase`
            if (erase_child)
                OP::rawbits::clear(_child_presence, key);//must be after `container->erase`
            return presence_first_set() == OP::rawbits::nil_c; //erase entire node if no more entries
        }

        /**
        *   Frees content of this node and give a caller addresses of all descendant children.
        *   So without recursion caller can destroy children after all.
        *   Node is not destroyed.
        *   @return number of data-slots destroyed
        */
        template <class OutIter>
        size_t erase_all(TrieOptions& options, OutIter& children_to_remove)
        {
            size_t data_slots = 0;
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node

            auto erase_node_interior = [&](fast_atom_t key, NodeData& node) {
                bool has_value = OP::rawbits::get(_value_presence, key);
                if (has_value)
                    ++data_slots;
                auto child_addr =
                    erase_entry(options, static_cast<key_value_t*>(nullptr), node, key,
                        has_value,
                        OP::rawbits::get(_child_presence, key));
                
                if (!child_addr.is_nil())
                {//postpone wipe children
                    *children_to_remove++ = std::move(child_addr);
                }
            };

            using local_callback_t = decltype(&erase_node_interior);
            container->foreach(foreach_callback_adapter<local_callback_t>, &erase_node_interior);
            OP::rawbits::clear_all(_child_presence, _value_presence);
            return data_slots;
        }
        
        /**
        \tparam F has signature `{user-type} (const NodeData&)`
        */
        template <class F>
        decltype(auto) rawc(TrieOptions& options, atom_t key, F&& callback) const
        {
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            auto node_data = container->cget(key);
            return callback(*node_data);
        }

        template <class F>
        decltype(auto) raw(TrieOptions& options, atom_t key, F&& callback) const
        {
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            NodeData& node_data = container->get(key);
            return callback(node_data);
        }

        constexpr bool has_child(std::uint_fast8_t key) const
        {
            return OP::rawbits::get(_child_presence, key);
        }

        constexpr bool has_value(std::uint_fast8_t key) const
        {
            return OP::rawbits::get(_value_presence, key);
        }


        /** Similar to pair of has_value/has_child but immediately returns Terminality::term_XXX combination */
        constexpr std::uint_fast8_t get_state(std::uint_fast8_t key) const
        {
            return (OP::rawbits::get(_child_presence, key) ? Terminality::term_has_child : 0)
                | (OP::rawbits::get(_value_presence, key) ? Terminality::term_has_data : 0);
        }

        void set_child(TrieOptions& options, atom_t key, FarAddress address)
        {
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            NodeData& node_data = container->get(key);
            assert(!OP::rawbits::get(_child_presence, key));
            set_raw_child(key, node_data, address);
        }

        void set_raw_child(atom_t key, NodeData& node_data, FarAddress address)
        {
            node_data._child = address;
            OP::rawbits::set(_child_presence, key);
            ++_version;
        }

        /**Get child address if present, otherwise return null-pos*/
        FarAddress get_child(TrieOptions& options, atom_t key) const
        {
            if (!OP::rawbits::get(_child_presence, key))
                return FarAddress{};
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            auto node_data = container->cget(key);
            return node_data->_child;
        }

        /**Get associated data if present, otherwise exception is thrown*/
        payload_t get_value(TrieOptions& options, atom_t key) const
        {
            if (!OP::rawbits::get(_value_presence, key))
                throw std::invalid_argument("key doesn't contain data");
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            auto node_data = container->cget(key);
            return payload_manager_t::rawc(options, node_data->_value,
                [](const payload_t& raw) -> payload_t { return raw; });
        }

        template <class FValueCallback>
        decltype(auto) get_value(TrieOptions& options, atom_t key, FValueCallback&& callback) const
        {
            if (!OP::rawbits::get(_value_presence, key))
                throw std::invalid_argument("key doesn't contain data");
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            auto node_data = container->cget(key);
            return payload_manager_t::rawc(options, node_data->_value, std::forward<FValueCallback>(callback));
        }

        /**
        *
        *   \tparam TPayload - allowed be either explicit value to assign or lambda with signature `lambda(payload&)`
        */
        template <class TPayload>
        void set_raw_factory_value(TrieOptions& options, atom_t key, NodeData& node, TPayload&& value)
        {
            payload_manager_t::raw(options, node._value, std::forward<TPayload>(value));
            OP::rawbits::set(_value_presence, key);
            ++_version;
        }

        /**
        *
        *   \tparam TPayload - allowed be either explicit value to assign or lambda with signature `lambda(payload&)`
        */
        template <class TPayload>
        void set_value(TrieOptions& options, atom_t key, TPayload&& value)
        {
            wrap_key_value_t container;
            kv_container(options, container); //resolve correct instance implemented by this node
            NodeData& node_data = container->get(key);
            set_raw_factory_value(options, key, node_data, std::forward<TPayload>(value));
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
            return (OP::rawbits::get(_child_presence, key) ? Terminality::term_has_child : 0) 
                | (OP::rawbits::get(_value_presence, key) ? Terminality::term_has_data : 0);
        }

        inline fast_dim_t presence_first_set() const
        {
            return OP::rawbits::first_any_set(_child_presence, _value_presence);
        }

        /**@return last position where child or value exists, may return dim_nil_c if node empty*/
        inline fast_dim_t presence_last_set() const
        {
            return OP::rawbits::last_any_set(_child_presence, _value_presence);
        }

        /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
        inline fast_dim_t presence_next_set(atom_t previous) const
        {
            return OP::rawbits::next_any_set(previous, _child_presence, _value_presence);
        }
        
        /** Implement pop-count algorithm to evaluate number of presence before specified key */
        inline fast_dim_t count_bits_before(atom_t key) const
        {
            constexpr fast_dim_t bits_c = std::numeric_limits<std::decay_t<decltype(_child_presence[0])>>::digits; //=64

            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = (1ULL << (key % bits_c)) - 1;

            // calculate rank: how many bits are set before this one?
            size_t pos = 0;
            for (size_t i = 0; i < word_idx; ++i) 
            {
                pos += std::popcount(_child_presence[i] | _value_presence[i]);
            }
            pos += std::popcount((_child_presence[word_idx] | _value_presence[word_idx]) & bit_mask);

            return static_cast<fast_dim_t>(pos);
        }
        
        /** evaluate size of existing items by counting bits in presence arrays.*/
        inline fast_dim_t size() const
        {
            fast_dim_t result = 0;
            
            for(size_t i = 0; i < presence_table_size_c; ++i)
                result += std::popcount(_child_presence[i] | _value_presence[i]);

            return result;
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

        template <class FCallback>
        static bool foreach_callback_adapter(fast_atom_t key, NodeData& value, void* user_data)
        {
            (*reinterpret_cast<FCallback>(user_data))(key, value);
            return true;
        }

        [[maybe_unused]] key_value_t* select_kv_container_instance(
            TrieOptions& options, wrap_key_value_t& out, dim_t capacity, const FarAddress& residence) const
        {
            switch (capacity)
            {
            case 4:
                return &out.template construct<indexing_table_t<4>>(options, *this);
            case 8:
                return &out.template construct<indexing_table_t<8>>(options, *this);
            case 16:
                return &out.template construct<indexing_table_t<16>>(options, *this);
            case 32:
                return &out.template construct<indexing_table_t<32>>(options, *this);
            case 64:
                return &out.template construct<indexing_table_t<64>>(options, *this);
            default:
                assert(false);
                [[fallthrough]]; //for release build
            case 128:
                return &out.template construct<indexing_table_t<128>>(options, *this);
            case 256:
                return &out.template construct<anti_hash_table_t>(options, *this);
            }
        }

        [[maybe_unused]] key_value_t* kv_container(TrieOptions& options, wrap_key_value_t& out) const
        {
            return select_kv_container_instance(options, out, _capacity, _hash_table);
        }

        void grow(TrieOptions& options, key_value_t& from_container)
        {
            fast_dim_t new_capacity = _capacity << 1;

            // Hash-table may not be able to grow in one step because of key collision, so place
            // grow code to loop to give several tries (other data-structures should succeed in 1 step).
            for(;; new_capacity <<= 1 )
            {
                assert(new_capacity <= 256); //must never over grow 256
                wrap_key_value_t new_container;
                select_kv_container_instance(options, new_container, new_capacity, FarAddress{});
                FarAddress dest_tbl = new_container->create_from(from_container); //start new container
                if (!dest_tbl.is_nil())
                {//success
                    from_container.destroy(_hash_table);
                    _hash_table = dest_tbl;
                    break;
                }
                new_container->destroy(dest_tbl); //temp container unsuccess insert, need destroy and retry with bigger capacity
            }
            _capacity = new_capacity;
            ++_version;
        }

    };

}//ns:OP::trie

#endif //_OP_TRIE_TRIENODE__H_
