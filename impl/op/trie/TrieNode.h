#ifndef _OP_TRIE_TRIENODE__H_
#define _OP_TRIE_TRIENODE__H_

#include <op/common/Bitset.h>
#include <op/common/StackAlloc.h>

#include <op/trie/HashTable.h>
#include <op/trie/AntiHashTable.h>
#include <op/trie/TriePosition.h>
#include <op/vtm/PersistedReference.h>
#include <op/vtm/StringMemoryManager.h>

namespace OP
{
    namespace trie
    {

        /** Represent single node of Trie*/
        template <class Payload>
        struct TrieNode
        {

            using payload_t = Payload;
            using this_t = TrieNode<payload_t> ;

            /*declare 256-bit presence bitset*/
            using presence_t = Bitset<4, std::uint64_t> ;

            /** Smart memory allocator for plain payload. When payload small
            * it uses same storage space as FarAddress, when it doesn't fit 
            * then HeapManager used to allocate memory
            */
            struct DataStorage
            {
                static_assert(std::is_standard_layout_v<Payload>, 
                    "only standart-layout alllowed in TrieNode");
                constexpr static bool big_payload_c = sizeof(payload_t) > sizeof(FarAddress);
                constexpr static size_t byte_size_c = sizeof(FarAddress);
                constexpr static size_t align_size_c = std::max(alignof(payload_t), alignof(FarAddress));

                alignas(align_size_c) std::byte _data[byte_size_c];
                

                void allocate(HeapManagerSlot& heap_manager) 
                {
                    if constexpr( big_payload_c )
                    {
                        //auto& address = heap_manager.make_new<payload_t>(std::forward<Args>(args)...);
                        auto& address = heap_manager.allocate(memory_requirement<payload_t>::requirement);
                        *std::launder(reinterpret_cast<FarAddress*>(_data)) = address;
                    }
                    else
                    {
                        //nothing to do, memory already available
                        //::new (_data) payload_t(std::forward<Args>(args)...);
                    }
                }

                template <class TSegmentTopology>
                payload_t& get(TSegmentTopology& tsegment) 
                {
                    if constexpr( big_payload_c )
                    {
                        auto& address = std::launder(reinterpret_cast<FarAddress*>(_data));
                        return 
                            resolve_segment_manager(tsegment)
                                .writable_block(address, 
                                    memory_requirement<payload_t>::requirement)
                                .template at<payload_t>(0);
                    }
                    else
                    {
                        return *std::launder(reinterpret_cast<payload_t*>(_data));
                    }
                }

                template <class TSegmentTopology>
                void destroy(TSegmentTopology& topology)
                {
                    if constexpr( big_payload_c )
                    {
                        auto& heap_manager = topology.OP_TEMPL_METH(slot)<HeapManagerSlot>();
                        auto& address = std::launder(reinterpret_cast<FarAddress*>(_data));
                        auto& val = resolve_segment_manager(topology).writable_block(address,
                            memory_requirement<payload_t>::requirement).template at<payload_t>(0);
                        val.~payload_t();
                        heap_manager.deallocate(address);
                    }
                    else
                    {
                        auto* val = std::launder(reinterpret_cast<payload_t*>(_data));
                        val->~payload_t();
                    }
                }

            };

            struct NodeData
            {
                FarAddress _child = {};
                smm::SmartStringAddress _stem = {};
                DataStorage _data;
            };

            using key_value_t = containers::KeyValueContainer< NodeData, this_t >;
            
            const dim_t magic_word_c = 0x55AA;
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
                //capacity must be pow of 2 and lay in range [8-256]
                assert( _capacity >= 8 && _capacity <= 256 && ((_capacity - 1) & _capacity) == 0 );
            }

            template <class Toplogy>
            void create_interior(Toplogy& topology)
            {
                wrap_key_value_t wrapper;
                kv_container(topology, wrapper);
                _hash_table = wrapper->create();
            }

            template <class Toplogy>
            void destroy_interior(Toplogy& topology)
            {
                assert(presence_first_set() == presence_t::nil_c);//all interior must be already deleted
                wrap_key_value_t wrapper;
                kv_container(topology, wrapper);
                wrapper->destroy(_hash_table);
                _hash_table = {};
            }

            
            template <class TSegmentTopology, class AtomIterator, class FProducePayload>
            void insert(TSegmentTopology& topology, atom_t key, 
                AtomIterator begin, const AtomIterator end, FProducePayload&& payload_factory)
            {
                for(;;)
                {
                    wrap_key_value_t container;
                    kv_container(topology, container); //resolve correct instance implemented by this node

                    auto [hash, success] = container->insert(key, 
                        [&](NodeData& to_construct) {
                            ::new (&to_construct)NodeData;
                            auto& heap_manager = topology.OP_TEMPL_METH(slot) < HeapManagerSlot > ();
                            to_construct._data.allocate(heap_manager);
                            auto& ref = to_construct._data.get(heap_manager);
                            ref = payload_factory();
                            _value_presence.set(key);
                            if( begin != end )
                            {
                                StringMemoryManager str_manager(topology);
                                auto size = end - begin;
                                assert(size < 
                                    std::numeric_limits<dim_t>::max() - 1);
                                to_construct._stem = str_manager.smart_insert(begin, end);
                                begin = end;
                            }
                        });

                    if (success)
                        break;
                    
                    assert(hash == dim_nil_c);//only possible reason to be there - capacity is over
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
                smm::SmartStringAddress stem_addr;
                if (erase_data)
                {
                    assert(presence(key));
                    NodeData* node = container->get(key);
                    stem_addr = node->_stem;
                    assert(node);

                    if (_child_presence.get(key))
                    {
                        //erase data only
                        node->_data.destroy(
                            topology.OP_TEMPL_METH(slot) < HeapManagerSlot > ()
                        );
                    }
                    else //entry totally removed
                        container->erase(key);
                    _value_presence.clear(key);
                }

                if (presence(key))
                { //when value/child presented need keep sequence in this node
                    return false;
                }
                
                if( !stem_addr.is_nil() )
                {
                    StringMemoryManager smm(topology);
                    smm.destroy(stem_addr);
                }
                    
                //_value_presence.clear(key); //it may look dupplicate, but isn't
                //@! think to reduce space of hashtable
                return presence_first_set() == presence_t::nil_c; //erase entire node if no more entries
            }

            /** 
            *   Entirely remove this node and content and give a caller address of all descendant children.
            *   So without recursion caller is responsible to destroy children as well, otherwise appears memory 
            *   leaking.
            *   Node is not destroyed.
            *   @return number of data-slots destroyed
            */
            template <class TSegmentTopology>
            size_t erase_interior(TSegmentTopology& topology, std::stack<FarAddress>& child_process)
            {
                size_t data_slots = 0;
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                StringMemoryManager string_memory_manager(topology);

                for(auto i = presence_first_set(); dim_nil_c != i; 
                    i = presence_next_set(static_cast<atom_t>(i)))
                {
                    NodeData* node = container->get(static_cast<atom_t>(i));
                    assert(node);
                    if(_value_presence.get(i))
                    {//wipe-out data
                        node->_data.destroy(
                            topology.OP_TEMPL_METH(slot) < HeapManagerSlot > ());
                        _value_presence.clear(i);
                        ++data_slots;
                    }
                    if(_child_presence.get(i))
                    {//wipe children
                        assert(!node->_child.is_nil());
                        child_process.push(node->_child);
                        if(!node->_stem.is_nil())
                        {
                            string_memory_manager.destroy(node->_stem);
                            node->_stem = {};
                        }
                        _child_presence.clear(i);
                    }
                }
                return data_slots;
            }
            
            /**
            \tparam F has signature `{user-type} (const NodeData&)`
            */
            template <class TSegmentTopology, class F>
            auto rawc(TSegmentTopology& topology, atom_t key, F callback) const
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto node_data = container->cget(key);
                assert(node_data); //must already be allocated
                return callback(*node_data);
            }

            template <class TSegmentTopology, class F>
            auto raw(TSegmentTopology& topology, atom_t key, F callback) const
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto* node_data = container->get(key);
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
                node_data->_child = address;
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
                NodeData* node_data = container->get(key);
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
                if(!_value_presence.get(key) )
                    throw std::invalid_argument("key doesn't contain data");
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                auto node = container->cget(key);
                return node->_data.get(topology);
            }

            template <class TSegmentTopology, class TData>
            void set_raw_value(TSegmentTopology& topology, atom_t key, NodeData& node, TData&& value)
            {
                node._data.get(topology) = std::forward<TData>(value);
                _value_presence.set(key);
                ++_version;
            }

            template <class TSegmentTopology, class TData>
            void set_value(TSegmentTopology& topology, atom_t key, TData&& value)
            {
                wrap_key_value_t container;
                kv_container(topology, container); //resolve correct instance implemented by this node
                NodeData* node_data = container->get(key);
                assert(node_data); //there we have only valid pointers
                set_raw_value(topology, key, *node_data, std::forward<TData>(value));
            }

            /** @return [if_child presence, if_data presence] */
            std::pair<bool, bool> get_presence(atom_t key) const
            {
                return std::make_pair(_child_presence.get(key), _value_presence.get(key));
            }

            /**@return first position where child or value exists, may return dim_nil_c if node empty*/
            inline nullable_atom_t first() const
            {
                return make_nullable( this->presence_first_set() );
            }

            /**@return last position where child or value exists, may return dim_nil_c if node empty*/
            inline nullable_atom_t last() const
            {
                return make_nullable(this->presence_last_set());
            }
            
            /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
            nullable_atom_t next(atom_t previous) const
            {
                return make_nullable( this->presence_next_set(previous) );
            }
            /**@return next or the same position where child or value exists, may return dim_nil_c if no more entries*/
            nullable_atom_t next_or_this(atom_t previous) const
            {
                return make_nullable(this->presence_next_set_or_this(previous));
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

            */
            template <class TSegmentTopology>
            void move_to(TSegmentTopology& topology, atom_t key, dim_t in_stem_pos, 
                WritableAccess<this_t>& target_node)
            {
                wrap_key_value_t src_container;
                kv_container(topology, src_container); //resolve correct instance implemented by this node

                auto* src_node_data = src_container->get(key);
                assert(src_node_data);
                assert( !src_node_data->_stem.is_nil()); //call move_to assumes valid stem
                
                wrap_key_value_t target_container;
                target_node->kv_container(topology, target_container);
                //take stem to memory
                StringMemoryManager str_manager(topology);
                atom_string_t stem_buf;
                str_manager.get(src_node_data->_stem, std::back_inserter(stem_buf));
                assert(in_stem_pos < stem_buf.size());
                atom_string_view_t left_stem(stem_buf.data(), in_stem_pos);
                atom_t new_key = stem_buf[in_stem_pos++];
                atom_string_view_t cary_over_stem(
                    stem_buf.data() + in_stem_pos, stem_buf.size() - in_stem_pos);
                str_manager.destroy(src_node_data->_stem);//remove previous
                src_node_data->_stem = {};
                if(!left_stem.empty())
                    src_node_data->_stem = str_manager.smart_insert(left_stem);

                target_container->insert(new_key,
                    [&](NodeData& target_data) -> void {
                        target_data = {};
                        if (!cary_over_stem.empty())
                        {
                            target_data._stem = str_manager.smart_insert(cary_over_stem);
                        }
                        
                        //copy data/address to target
                        target_data._child = src_node_data->_child;
                        target_node->_child_presence.assign(new_key, _child_presence.get(key));
                        src_node_data->_child = target_node.address();
                        _child_presence.set(key); //override for a case prev wasn't set
                        if (_value_presence.get(key))
                        {
                            //as soon _value_presence cleared, twice destructor wouldn't called
                            target_data._data = src_node_data->_data;
                            _value_presence.clear(key);
                            target_node->_value_presence.set(new_key);
                        }
                        else
                        { //no-data must be same
                            assert(!target_node->_value_presence.get(new_key));
                        }
                    }
                );

                ++_version;
                ++target_node->_version;
            }
            /**
            *   Taken a byte key, return index where corresponding key should reside for stem_manager and value_manager
            *   @return reindexed key (value is in range [_8, _256) ). 
            */
            template <class TSegmentTopology>
            atom_t reindex(TSegmentTopology& topology, atom_t key) const
            {
                wrap_key_value_t src_container;
                kv_container(topology, src_container); //resolve correct instance implemented by this node
                return src_container->reindex(_hash_table, _capacity, key);
            }
            dim_t capacity() const
            {
                return _capacity;
            }
            FarAddress reindex_table() const
            {
                return this->_hash_table;
            }
            inline bool presence(atom_t key) const
            {
                return _child_presence.get(key) || _value_presence.get(key);
            }
            inline dim_t presence_first_set() const
            {
                dim_t ch_res = _child_presence.first_set();
                dim_t dt_res = _value_presence.first_set();
                if (dim_nil_c == ch_res)
                    return dt_res;
                if (dim_nil_c == dt_res)
                    return ch_res;
                return std::min(dt_res, ch_res);
            }
            /**@return last position where child or value exists, may return dim_nil_c if node empty*/
            inline dim_t presence_last_set() const
            {
                dim_t ch_res = _child_presence.last_set();
                dim_t dt_res = _value_presence.last_set();
                if (dim_nil_c == ch_res)
                    return dt_res;
                if (dim_nil_c == dt_res)
                    return ch_res;
                return std::max(dt_res, ch_res);
            }

            /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
            inline dim_t presence_next_set(atom_t previous) const
            {
                dim_t ch_res = _child_presence.next_set(previous);
                dim_t dt_res = _value_presence.next_set(previous);
                if (dim_nil_c == ch_res)
                    return dt_res;
                if (dim_nil_c == dt_res)
                    return ch_res;
                return std::min(dt_res, ch_res);
            }
            /**@return next or the same position where child or value exists, may return dim_nil_c if no more entries*/
            inline dim_t presence_next_set_or_this(atom_t previous) const
            {
                dim_t ch_res = _child_presence.next_set_or_this(previous);
                dim_t dt_res = _value_presence.next_set_or_this(previous);
                if (dim_nil_c == ch_res)
                    return dt_res;
                if (dim_nil_c == dt_res)
                    return ch_res;
                return std::min(dt_res, ch_res);
            }


        private:
            using hash_table_t = containers::PersistedHashTable< NodeData, this_t >;
            using anti_hash_table_t = containers::AntiHashTable< NodeData, this_t>;
            
            using wrap_key_value_t = Multiimplementation<key_value_t, hash_table_t, anti_hash_table_t>;

            template <class TSegmentTopology>
            auto kv_container(TSegmentTopology& topology, wrap_key_value_t &out, dim_t capacity = dim_nil_c) const
            {
                if( capacity == dim_nil_c )
                    capacity = _capacity; 
                return (capacity < 256) 
                    ? out.template construct<hash_table_t>(topology, *this, capacity)
                    : out.template construct<anti_hash_table_t>(topology, *this, capacity)
                    ;
            }

            /**
            * Take some part of string specified by [begin ,end) and place inside this node
            * @return origin index that matches to accomodated key (reindexed key)
            */
            template <class TSegmentTopology, class Atom>
            void insert_stem(TSegmentTopology& topology, NodeData& node, Atom& begin, Atom end)
            {
                assert(node._stem.is_nil());//please check that you don't override existing stem
                if(end != begin) //first letter considered in `presence`
                {
                    StringMemoryManager str_manager(topology);
                    node._stem = str_manager.insert(begin, end);
                    begin = end;
                }
                else
                {
                    node._stem = {};
                }
            }
            
            /**
            *   Detects substr contained in stem.
            * @return pair of comparison result and length of overlapped string.
            *
            */
            template <class TSegmentTopology, class T>
            inline std::tuple<StemCompareResult, dim_t> prefix_of(
                TSegmentTopology& topology,
                FarAddress st_address, T& begin, T end) const
            {
                typename StringMemoryManager::persisted_char_array_t strmngr(st_address);

                auto result = StemCompareResult::unequals;
                auto stem = strmngr.cref(resolve_segment_manager(topology));
                auto* stem_data = stem->data;
                size_t count = 0;
                for (; count < stem->size; ++count, ++begin)
                {
                    if (begin == end) //source string is over
                    {
                        result = StemCompareResult::string_end;
                        break;
                    }
                    if (stem_data[count] != *begin)
                    {//difference in sequence mean that stem should be splitted
                        break;
                    }
                }
                if (count == stem->size)
                {//stem scan completed
                    result = (begin == end 
                        ? StemCompareResult::equals 
                        : StemCompareResult::stem_end);
                }
                assert(count < std::numeric_limits<dim_t>::max());
                return std::make_tuple(result, static_cast<dim_t>(count));
            }

            template <class TSegmentTopology>
            void grow(TSegmentTopology& topology, key_value_t& from_container)
            {
                dim_t new_capacity;
                for (
                    new_capacity = OP::trie::containers::details::grow_size(_capacity);
                    true;
                    new_capacity = OP::trie::containers::details::grow_size(new_capacity))
                {
                    wrap_key_value_t new_container;

                    kv_container(topology, new_container, new_capacity);
                    FarAddress dest_tbl;
                    if (new_container->grow_from(from_container, dest_tbl))
                    {
                        from_container.destroy(_hash_table);
                        _hash_table = dest_tbl;
                        break;
                    }
                }
                _capacity = new_capacity;
                ++_version;
            }
            
        };
    } //ns:trie
}//ns:OP
    
#endif //_OP_TRIE_TRIENODE__H_
