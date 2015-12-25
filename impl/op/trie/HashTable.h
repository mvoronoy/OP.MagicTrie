#ifndef _OP_TRIE_HASHTABLE__H_
#define _OP_TRIE_HASHTABLE__H_

#include <OP/trie/typedefs.h>
#include <OP/trie/SegmentManager.h>

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
                Content memory_block[1];
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
                    assert((dim_t)capacity <= 256);

                    auto& memmngr = _topology.slot<MemoryManager>();
                    OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto byte_size = memory_requirements((dim_t)capacity);
                    auto result = memmngr.allocate(byte_size);
                    auto mem = _topology.segment_manager().writable_block(result, byte_size);
                    auto table = new (mem.pos()) HashTableData(capacity);
                    for (auto i = 0; i < (dim_t)capacity; ++i)
                    { //reset all flags
                        table->memory_block[i].flag = 0;
                    }
                    g.commit();
                    return result;
                }
                
                OP_CONSTEXPR(OP_EMPTY_ARG) static dim_t memory_requirements(dim_t capacity)
                {
                    typedef typename std::aligned_storage<sizeof(HashTableData)>::type head_type;
                    typedef typename std::aligned_storage<sizeof(HashTableData::Content)>::type item_type;
                    return sizeof(head_type) + sizeof(item_type) * (capacity - 1);
                }
                /**
                *   @return insert position or #end() if no more capacity
                */
                std::pair<dim_t, bool> insert(FarAddress address, atom_t key)
                {
                    OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto mem = _topology.segment_manager().writable_block(
                        result,  memory_requirements(capacity));
                    auto hash_data = mem.at<HashTableData<Payload> >(0);

                    unsigned hash = static_cast<unsigned>(key) & (hash_data.capacity - 1); //assume that capacity is ^ 2
                    for (unsigned i = 0; i < hash_data.neighbor_width; ++i)
                    {
                        if (0 == (fpresence_c & hash_data.memory_block[hash]._flag))
                        { //nothing at this pos
                            hash_data.memory_block[hash].flag |= fpresence_c;
                            hash_data.memory_block[hash].key = key;
                            hash_data.count++;
                            g.commit();
                            return std::make_pair(hash, true);
                        }
                        if (hash_data.memory_block[hash].key == key)
                            return std::make_pair(hash, false); //already exists
                        ++hash %= hash_data.capacity; //keep in boundary
                    }
                    return std::make_pair(~dim_t(0), false); //no capacity
                }
                /**@return number of erased - 0 or 1*/
                unsigned erase(atom_t key)
                {
                    const unsigned key_hash = static_cast<unsigned>(key)& bitmask_c;
                    unsigned hash = key_hash;
                    for (unsigned i = 0; i < neighbor_width_c; ++i)
                    {
                        if (0 == (fpresence_c & container()[hash]._flag))
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
                dim_t find(atom_t key) const
                {
                    unsigned hash = static_cast<unsigned>(key)& bitmask_c;
                    for (unsigned i = 0; i < neighbor_width_c; ++i)
                    {
                        if (0 == (fpresence_c & container()[hash]._flag))
                        { //nothing at this pos
                            return nil_c;
                        }
                        if (container()[hash]._key == key)
                            return hash;
                        ++hash %= capacity(); //keep in boundary
                    }
                    return nil_c;
                }
                
                /** Erase the entry associated with key
                *   *@throws std::out_of_range exception if key is not exists
                */
                void remove(atom_t key)
                {
                    auto n = erase(key);
                    if (n == 0)
                        std::out_of_range("no such key");
                }

            private:
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
