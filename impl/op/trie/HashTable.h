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
                    }
                };
            }//ns:details
            struct AbstractHashTableData
            {
                const HashTableCapacity capacity;
                const dim_t neighbor_width;
                dim_t size;
            };

            template <class Payload>
            struct HashTableData : public AbstractHashTableData
            {
                struct Content
                {
                    atom_t key;
                    std::uint8_t flag;
                    Payload data;
                };
                Content memory_block[1];
            };


            template <class SegmentTopology>
            struct PersistedHashTable
            {
                PersistedHashTable(SegmentTopology& topology)
                    : _topology(topology)
                {}

                template<class Payload>
                FarAddress create(HashTableCapacity capacity)
                {
                    assert(capacity <= 256);
                    typedef HashTableData<Payload> data_t;
                    auto& memmngr = _toplogy.slot<MemoryManager>();
                    OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto byte_size = memory_requirements<Payload>(capacity);
                    auto result = memmngr.allocate(byte_size);
                    auto mem = _topology.segment_manager().writable_block(result, byte_size);
                    auto table = new (mem.pos()) data_t{ 
                        .capacity = capacity, 
                        .neighbor_width = details::max_hash_neighbors(capacity), 
                        .size = 0 };
                    for (auto i = 0; i < capacity; ++i)
                    { //reset all flags
                        table->memory_block[i].flag = 0;
                    }
                    g.commit();
                    return result;
                }
                template<class Payload>
                static size_t memory_requirements(dim_t capacity)
                {
                    typedef HashTableData<Payload> data_t;
                    typedef typename std::aligned_storage<sizeof(AbstractHashTableData)>::type head_type;
                    typedef typename std::aligned_storage<sizeof(data_t)>::type concrete_type;
                    OP_CONSTEXPR(const) size_t content_size = sizeof(concrete_type) - sizeof(head_type); //how big Content memory_block[1] is
                    return sizeof(head_type) + content_size * capacity;
                }
                /**
                *   @return insert position or #end() if no more capacity
                */
                template <class Payload>
                std::pair<dim_t, bool> insert(FarAddress address, atom_t key, Payload && value)
                {
                    OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto mem = _topology.segment_manager().writable_block(result,  
                        memory_requirements<Payload>(capacity));
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
                Payload& value(const iterator& index)
                {
                    //static_assert(!std::is_same(Payload, EmptyPayload), "No value array for this container");
                    if (index._offset >= capacity_c
                        || !(container()[index._offset]._flag & fpresence_c))
                        throw std::out_of_range("invalid index or no value associated with key");
                    return container()[index._offset]._data;
                }
                /**Find index of key entry or #end() if nothing found*/
                iterator find(atom_t key) const
                {
                    unsigned hash = static_cast<unsigned>(key)& bitmask_c;
                    for (unsigned i = 0; i < neighbor_width_c; ++i)
                    {
                        if (0 == (fpresence_c & container()[hash]._flag))
                        { //nothing at this pos
                            return end();
                        }
                        if (container()[hash]._key == key)
                            return iterator(this, hash);
                        ++hash %= capacity(); //keep in boundary
                    }
                    return end();
                }
                atom_t key(const iterator& i) const
                {
                    return container()[i._offset]._key;
                }
                iterator begin() const
                {
                    if (size() == 0)
                        return end();
                    for (unsigned i = 0; i < capacity(); ++i)
                        if (container()[i]._flag & fpresence_c)
                            return iterator(this, i);
                    return end();
                }
                iterator end() const
                {
                    return iterator(this, ~0u);
                }
                iterator& next(iterator& i, typename iterator::distance_type offset = 1) const
                {
                    //note following 'for' plays with unsigned-byte arithmetic
                    iterator::distance_type inc = offset > 0 ? 1 : -1;
                    for (i._offset += inc; offset && i._offset < capacity(); i._offset += inc)
                        if (container()[i._offset]._flag & fpresence_c)
                        {
                            offset -= inc;
                            if (!offset)
                                return i;
                        }
                    if (i._offset >= capacity())
                        i._offset = ~0u;
                    return i;
                }
                ///
                /// Overrides
                ///
                atom_t allocate_key() override
                {
                    for (node_size_t i = 0; i < capacity(); ++i)
                        if (!(container()[i]._flag & fpresence_c))
                        {//occupy this
                            atom_t k = static_cast<atom_t>(i);
                            container()[i]._flag = fpresence_c;
                            container()[i]._key = k;
                            return k;
                        }
                    throw std::out_of_range("table is full");
                }
                /**
                *   Get value placeholder for the key
                *   *@throws std::out_of_range exception if key is not exists
                */
                payload_t& value(atom_t key)  override
                {
                    auto i = find(key);
                    if (i == end())
                        std::out_of_range("no such key");
                    return value(i);
                }
                /** Erase the entry associated with key
                *   *@throws std::out_of_range exception if key is not exists
                */
                void remove(atom_t key)  override
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
