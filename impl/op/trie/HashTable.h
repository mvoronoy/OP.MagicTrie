#ifndef _OP_TRIE_HASHTABLE__H_
#define _OP_TRIE_HASHTABLE__H_

#include <OP/trie/typedefs.h>
#include <OP/trie/SegmentManager.h>
#include <OP/trie/TypeHelper.h>

namespace OP
{
    namespace trie
    {
        namespace containers
        {
            
            enum class HashTableCapacity : dim_t
            {
                _8 = 8, 
                _16 = 16, 
                _32 = 32, 
                _64 = 64, 
                _128 = 128, 
                _256 = 256
            };
            enum class HashTableFlag
            {
                uf_0 = 0,
            };
            namespace details
            {
                inline dim_t max_hash_neighbors(HashTableCapacity capacity)
                {
                    switch (capacity)
                    {
                    case HashTableCapacity::_8:
                    case HashTableCapacity::_128:
                        return 2;
                    case HashTableCapacity::_16:
                    case HashTableCapacity::_32:
                        return 3;
                    case HashTableCapacity::_64:
                        return 4;
                    case HashTableCapacity::_256:
                        return 1;
                    default:
                        assert(false);
                        throw std::invalid_argument("capacity");
                    }
                };
                inline HashTableCapacity grow_size(HashTableCapacity capacity)
                {
                    if (capacity < HashTableCapacity::_128)
                        return (HashTableCapacity)((dim_t)capacity * 2);
                    else
                    {
                        assert(capacity < HashTableCapacity::_256); //must never grow to _256
                        return HashTableCapacity::_256;
                    }
                };
                /**function forms bit-mask to evaluate hash. It is assumed that param capcity is ^2 */
                OP_CONSTEXPR(OP_EMPTY_ARG) inline dim_t bitmask(HashTableCapacity capacity)
                {
                    return (dim_t)capacity - 1 /*on condition capacity is power of 2*/;
                }
            }//ns:details

            struct HashTableData 
            {
                HashTableData(HashTableCapacity acapacity)
                    : capacity((dim_t)acapacity)
                    , neighbor_width(details::max_hash_neighbors(acapacity))
                    , size(0)
                {

                }
                struct Content
                {
                    atom_t key;
                    std::uint8_t flag;
                };
                const dim_t capacity;
                const dim_t neighbor_width;
                dim_t size;
            };


            template <class SegmentTopology>
            struct PersistedHashTable
            {
                enum : dim_t
                {
                    nil_c = dim_t(-1)

                };
                PersistedHashTable(SegmentTopology& topology)
                    : _topology(topology)
                {}
                /**
                * Create HashTableData<Payload> in dynamic memory using MemoryManager slot
                * @return far-address that point to allocated table
                */
                FarAddress create(HashTableCapacity capacity)
                {
                    assert((dim_t)capacity < 256);

                    auto& memmngr = _topology.slot<MemoryManager>();
                    //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto byte_size = memory_requirement<HashTableData>::requirement +
                        memory_requirement<HashTableData::Content>::requirement * (dim_t)capacity;

                    auto result = memmngr.allocate(byte_size);
                    auto mem = _topology.segment_manager().writable_block(result, byte_size);
                    auto table_head = new (mem.pos()) HashTableData(capacity);
                    auto table = new (mem.pos() + memory_requirement<HashTableData>::requirement) HashTableData::Content[(dim_t)capacity];
                    for (auto i = 0; i < (dim_t)capacity; ++i)
                    { //reset all flags
                        table[i].flag = 0;
                    }
                    //g.commit();
                    return result;
                }
                
                /**
                *   @return insert position or #end() if no more capacity
                */
                std::pair<dim_t, bool> insert(FarAddress address, atom_t key)
                {
                    //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    
                    auto table_head = _topology.segment_manager().wr_at<HashTableData>(address);
                    auto data_block = _topology.segment_manager().writable_block(
                        address + memory_requirement<HashTableData>::requirement,
                        table_head->capacity * memory_requirement<HashTableData::Content>::requirement);
                    auto hash_data = data_block.at<HashTableData::Content>(0);
                    return do_insert(*table_head, hash_data, key);
                }

                /**@return number of erased - 0 or 1*/
                unsigned erase(atom_t key)
                {
                    const unsigned key_hash = static_cast<unsigned>(key)& bitmask_c;
                    unsigned hash = key_hash;
                    for (unsigned i = 0; i < neighbor_width_c; ++i)
                    {
                        if (0 == (fpresence_c & container()[hash].flag))
                        { //nothing at this pos
                            return 0;
                        }
                        if (container()[hash]._key == key)
                        {//make erase
                            _count--;
                            //may be rest of neighbors sequence may be shifted by 1, so scan in backward

                            hash = restore_on_erase(hash);
                            //just release pos
                            container()[hash]._flag = 0;
                            return 1;
                        }
                        ++hash %= capacity(); //keep in boundary
                    }
                    return ~0u;
                }
                void clear()
                {
                    std::for_each(container(), container() + size(), [](Content& c){ c._flag = 0; });
                    _count = 0;
                }
               

                /**Find index of key entry or #end() if nothing found*/
                dim_t find(FarAddress address, atom_t key) const
                {
                    //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto head_block = _topology.segment_manager().readonly_block(
                        address, memory_requirement<HashTableData>::requirement );
                    auto head = head_block.at<HashTableData>(0);
                    auto data_block = _topology.segment_manager().readonly_block(
                        address + memory_requirement<HashTableData>::requirement,  
                        head->capacity * memory_requirement<HashTableData::Content>::requirement);
                    auto data_table = data_block.at<HashTableData::Content>(0);
                    unsigned hash = static_cast<unsigned>(key)& details::bitmask((HashTableCapacity)head->capacity);
                    for (unsigned i = 0; i < head->neighbor_width; ++i)
                    {
                        if (0 == (fpresence_c & data_table[hash].flag))
                        { //nothing at this pos
                            return nil_c;
                        }
                        if (data_table[hash].key == key)
                            return hash;
                        ++hash %= head->capacity; //keep in boundary
                    }
                    return nil_c;
                }
                //bool get_user_flag(atom_t key, NodePersense flag)
                //{
                //    assert(flag >= _f_userdefined_);
                //    auto idx = find(key);
                //    assert(idx != nil_c);
                //    return 0 != (flag & container()[hash].flag);
                //}
                //void set_user_flag(atom_t key, NodePresence flag, bool new_val)
                //{
                //    assert(flag >= _f_userdefined_);
                //    auto idx = find(key);
                //    assert(idx != nil_c);
                //    if (new_val)
                //        container()[idx].flag |= flag;
                //    else
                //        container()[idx].flag &= ~flag;
                //}
                /** Erase the entry associated with key
                *   *@throws std::out_of_range exception if key is not exists
                */
                void remove(atom_t key)
                {
                    auto n = erase(key);
                    if (n == 0)
                        std::out_of_range("no such key");
                }
                /**
                *   @param from [in/out] origin hash table that will be changed during grow. When table exceeds 128, this became nil since no table should be used above 128
                *   @tparam callback - functor with signature void(atom_t from, dim_t to)
                *   @tparam prepare - functor with signature void(HashTableCapacity new_capacity)
                */
                template <class FPepareCallback, class FReindexCallback>
                void grow(trie::PersistedReference<HashTableData>& from, FPepareCallback& prepare, FReindexCallback&callback)
                {
                    auto prev_tbl_head = _topology.segment_manager().readonly_access<HashTableData>(from.address);
                    auto prev_tbl_data = _topology.segment_manager().readonly_access<HashTableData::Content>(
                        from.address + memory_requirement<HashTableData>::requirement
                        );

                    auto new_capacity = details::grow_size((HashTableCapacity)prev_tbl_head->capacity);
                    prepare(new_capacity);
                    FarAddress new_address; //got default value nil
                    std::function<dim_t(atom_t)> remap;
                    if (new_capacity == HashTableCapacity::_256) //check if limit is reached - must remove table at all
                    {
                        remap = [](atom_t prev_key) -> dim_t{ //when no table there is no need to modify index
                            return prev_key;
                        };
                    }
                    else
                    { //create new grown table
                        new_address = this->create(new_capacity);
                        
                        auto table_head = _topology.segment_manager().wr_at<HashTableData>(new_address);
                        auto data_block = _topology.segment_manager().writable_block(
                            new_address + memory_requirement<HashTableData>::requirement,
                            table_head->capacity * memory_requirement<HashTableData::Content>::requirement);
                        auto hash_data = data_block.at<HashTableData::Content>(0);
                        remap = [&](atom_t prev_key) ->dim_t { //lambda inserts key to new table and returns just created index
                            auto ins_res = do_insert(*table_head, hash_data, prev_key);
                            assert(ins_res.second && ins_res.first != ~dim_t(0)); //bigger table cannot fail on grow operation
                            return ins_res.first;
                        };
                    }
                    //iterate through existing entries and apply reindexing rule
                    for (unsigned i = 0; i < prev_tbl_head->capacity; ++i)
                    {
                        if (fpresence_c & prev_tbl_data[i].flag)
                        {
                            //tell other that position was remaped 
                            callback(i, remap(prev_tbl_data[i].key));
                        }
                    }

                    //free old table memory
                    auto& memmngr = _topology.slot<MemoryManager>();
                    memmngr.deallocate(from.address);
                    from.address = new_address; //may be nil
                }

            private:
                std::pair<dim_t, bool> do_insert(HashTableData& head, HashTableData::Content * hash_data, atom_t key)
                {
                    unsigned hash = static_cast<unsigned>(key) & (head.capacity - 1); //assume that capacity is ^ 2
                    for (unsigned i = 0; i < head.neighbor_width && head.size < head.capacity; ++i)
                    {
                        if (0 == (fpresence_c & hash_data[hash].flag))
                        { //nothing at this pos
                            hash_data[hash].flag |= fpresence_c;
                            hash_data[hash].key = key;
                            head.size++;
                            return std::make_pair(hash, true);
                        }
                        if (hash_data[hash].key == key)
                            return std::make_pair(hash, false); //already exists
                        ++hash %= head.capacity; //keep in boundary
                    }
                    return std::make_pair(~dim_t(0), false); //no capacity
                }
                /** Optimize space before some item is removed
                * @return - during optimization this method may change origin param 'erase_pos', so to real erase use index returned
                */
                unsigned restore_on_erase(unsigned erase_pos)
                {
                    unsigned erased_hash = static_cast<unsigned>(container()[erase_pos]._key) & bitmask_c;
                    unsigned limit = (erase_pos + neighbor_width_c) % capacity(); //start from last available neighbor

                    for (unsigned i = (erase_pos + 1) % capacity(); i != limit; ++i %= capacity())
                    {
                        if (0 == (fpresence_c & container()[i]._flag))
                            return erase_pos; //stop optimization and erase item at pos
                        unsigned local_hash = (static_cast<unsigned>(container()[i]._key)&bitmask_c);
                        bool item_in_right_place = i == local_hash;
                        if (item_in_right_place)
                            continue;
                        unsigned x = less_pos(erased_hash, erase_pos) ? erase_pos : erased_hash;
                        if (!less_pos(x, local_hash)/*equivalent of <=*/)
                        {
                            copy_to(erase_pos, i);
                            erase_pos = i;
                            erased_hash = local_hash;
                            limit = (erase_pos + neighbor_width_c) % capacity();
                        }
                    }
                    return erase_pos;
                }
                /** test if tst_min is less than tst_max on condition of cyclyng nature of hash buffer, so (page_c = 8):
                    For page-size = 16:
                    less(0xF, 0x1) == true
                    less(0x1, 0x2) == true
                    less(0x1, 0xF) == false
                    less(0x2, 0x1) == false
                    less(0x5, 0xF) == true
                    less(0xF, 0x5) == false
                */
                bool less_pos(unsigned tst_min, unsigned tst_max) const
                {
                    int dif = static_cast<int>(tst_min)-static_cast<int>(tst_max);
                    unsigned a = std::abs(dif);
                    if (a > (static_cast<unsigned>(capacity()) / 2)) //use inversion of signs
                        return dif > 0;
                    return dif < 0;
                }
                void copy_to(unsigned to, unsigned src)
                {
                    container()[to] = container()[src];
                }

            private:
                SegmentTopology& _topology;
    };
        } //ns:containers
    }//ns: trie
}//ns: OP


#endif //_OP_TRIE_HASHTABLE__H_
