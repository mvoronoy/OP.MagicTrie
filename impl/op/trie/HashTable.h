#ifndef _OP_TRIE_HASHTABLE__H_
#define _OP_TRIE_HASHTABLE__H_

#include <op/common/typedefs.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>
#include <op/trie/Containers.h>
#include <unordered_map>
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
                /**Define how many */
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
                /*struct Content
                {
                    Content() {flag = 0; }
                    atom_t key;
                    std::uint8_t flag;
                };*/
                typedef std::uint16_t content_t;
                /**Set bits of the specified flag*/
                static void set_flag(WritableAccess<HashTableData::content_t>& acc, fast_dim_t index, atom_t flag)
                {
                    auto &v = acc[index];
                    v = v | flag;
                }
                /**Clear bits of the specified flag*/
                static void reset_flag(WritableAccess<HashTableData::content_t>& acc, fast_dim_t index, atom_t flag)
                {
                    auto &v = acc[index];
                    v = v & ~static_cast<content_t>(flag);
                }
                /**Clear bits of the specified flag*/
                template <class Accessor>
                static bool has_flag(Accessor& acc, fast_dim_t index, atom_t flag)
                {
                    auto &v = acc[index];
                    return (v & flag) != 0;
                }
                /**Set value and fpresence_c flag */
                static void set_value(WritableAccess<HashTableData::content_t>& acc, fast_dim_t index, atom_t value)
                {
                    auto &v = acc[index];
                    v = (v & 0xFF) | (((std::uint16_t)value) << 8) | fpresence_c;
                }
                /**\tparam Accessor - either ReadonlyAccess<HashTableData::content_t> or WritableAccess<HashTableData::content_t> */
                template <class Accessor>
                static atom_t get_flag(Accessor& acc, fast_dim_t index)
                {
                    auto &v = acc[index];
                    return static_cast<atom_t>(v & 0xFF);
                }
                template <class Accessor>
                static atom_t get_value(Accessor& acc, fast_dim_t index)
                {
                    auto &v = acc[index];
                    return static_cast<atom_t>(v >> 8);
                }
                const dim_t capacity;
                const dim_t neighbor_width;
                dim_t size;
            };


            template <class SegmentTopology>
            struct PersistedHashTable
            {
                using this_t = PersistedHashTable<SegmentTopology>;
                enum : dim_t
                {
                    nil_c = dim_t(-1)

                };
                PersistedHashTable(SegmentTopology& topology)
                    : _topology(topology)
                {}
                SegmentTopology& topology()
                {
                    return _topology;
                }
                /**
                * Create HashTableData<Payload> in dynamic memory using HeapManagerSlot slot
                * @return far-address that point to allocated table
                */
                FarAddress create(HashTableCapacity capacity)
                {
                    assert((dim_t)capacity < 256);

                    auto& memmngr = _topology.OP_TEMPL_METH(slot)<HeapManagerSlot>();
                    
                    auto byte_size = memory_requirement<HashTableData>::requirement +
                        memory_requirement<HashTableData::content_t>::requirement * (dim_t)capacity;

                    auto result = memmngr.allocate(byte_size);
                    auto table_block = _topology.segment_manager().writable_block(result, byte_size);
                    
                    auto data_block = std::move(table_block.subset(memory_requirement<HashTableData>::requirement));
                    
                    WritableAccess<HashTableData> table_head(std::move(table_block));
                    table_head.make_new(capacity);
                    WritableAccess<HashTableData::content_t> table_data(std::move(data_block));
                    table_data.make_array((dim_t)capacity);
                    
                    return result;
                }
                void destroy(const trie::PersistedReference<HashTableData>& htbl)
                {
                    auto& memmngr = _topology.OP_TEMPL_METH(slot)<HeapManagerSlot>();
                    memmngr.deallocate(htbl.address);
                }
                /**
                *   @return insert position or #end() if no more capacity
                */
                std::pair<dim_t, bool> insert(const trie::PersistedReference<HashTableData>& ref_data, atom_t key)
                {
                    //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());

                    auto table_head = accessor<HashTableData>(_topology, ref_data.address);
                    auto hash_data = array_accessor<HashTableData::content_t>(_topology,
                        content_item_address(ref_data.address, 0),
                        table_head->capacity);
                    return do_insert(table_head, hash_data, key);
                }

                /**
                * @tparam OnMoveCallback - functor that is called if internal space of hash-table is optimized (shrinked). Argumnets (from, to)
                * @return position where data has been erased (or not found if no such key)
                */
                template <class OnMoveCallback>
                unsigned erase(const trie::PersistedReference<HashTableData>& ref_data, atom_t key, OnMoveCallback& on_move_callback)
                {
                    auto table_head = accessor<HashTableData>(_topology, ref_data.address);
                    unsigned hash = static_cast<unsigned>(key) & (table_head->capacity - 1); //assuming that capacity is ^ 2
                    auto hash_data = array_accessor<HashTableData::content_t>(_topology,
                        content_item_address(ref_data.address, 0),
                        table_head->capacity);

                    for (unsigned i = 0; i < table_head->neighbor_width; ++i)
                    {
                        if (0 == (fpresence_c & HashTableData::get_flag(hash_data, hash)))
                        { //nothing at this pos
                            return hash;
                        }
                        if (HashTableData::get_value(hash_data, hash) == key)
                        {//make erase
                            table_head->size--;
                            //may be rest of neighbors sequence may be shifted by 1, so scan in backward

                            return restore_on_erase(table_head, hash_data, hash, on_move_callback);
                        }
                        ++hash %= table_head->capacity; //keep in boundary
                    }
                    return hash;
                }

                ReadonlyAccess<HashTableData> table_head(const trie::PersistedReference<HashTableData>& ref_data) const
                {
                    return view<HashTableData>(_topology, ref_data.address);
                }
                /**Find index of key entry or #end() if nothing found*/
                dim_t find(const trie::PersistedReference<HashTableData>& ref_data, atom_t key) const
                {
                    return find(table_head(ref_data), key);
                }
                /**Find index of key entry or #end() if nothing found*/
                dim_t find(const ReadonlyAccess<HashTableData>& head, atom_t key) const
                {
                    auto data_table = array_view<HashTableData::content_t>(
                        _topology,
                        content_item_address(head.address(), 0),
                        head->capacity);
                    return do_find(head, data_table, key);
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
                struct GrowResult
                {
                    HashTableCapacity old_capacity;
                    HashTableCapacity new_capacity;
                    FarAddress new_address;
                    std::function<fast_dim_t(atom_t)> old_map_function;
                    std::function < fast_dim_t(atom_t)> new_map_function;
                };
                /**
                *   @param from [in/out] origin hash table that will be changed during grow. When table exceeds 128, this became nil since no table should be used above 128
                * @return tuple of new dimension and functor that can be used as ruler how key is converted to new indexes 
                */
                template <class KeyIterator>
                GrowResult grow(trie::PersistedReference<HashTableData> from, KeyIterator begin, KeyIterator end)
                {
                    GrowResult retval;
                    auto prev_tbl_head = view<HashTableData>(_topology, from.address, ReadonlyBlockHint::ro_keep_lock);
                    auto prev_tbl_data = array_view<HashTableData::content_t>(
                        _topology,
                        content_item_address(from.address),
                        prev_tbl_head->capacity,
                        ReadonlyBlockHint::ro_keep_lock);
                    retval.old_capacity = (HashTableCapacity)prev_tbl_head->capacity;
                    //before c++14 there was no way to move scope var into lambda, so use trick with shared_ptr
                    auto shared_prev_tbl_head = std::make_shared<decltype(prev_tbl_head)>(std::move(prev_tbl_head));
                    auto shared_prev_tbl_data = std::make_shared<decltype(prev_tbl_data)>(std::move(prev_tbl_data));
                    retval.old_map_function = [this, shared_prev_tbl_head, shared_prev_tbl_data](atom_t key)->fast_dim_t {
                        return this->do_find(*shared_prev_tbl_head, *shared_prev_tbl_data, key);
                    };
                    
                    for (retval.new_capacity = details::grow_size(retval.old_capacity);
                        retval.new_capacity != HashTableCapacity::_256;
                        retval.new_capacity = details::grow_size(retval.new_capacity))
                    {
                        
                        //create new grown table
                        retval.new_address = this->create(retval.new_capacity);

                        auto table_head = accessor<HashTableData>(_topology, retval.new_address);
                        auto hash_data = array_accessor<HashTableData::content_t>(_topology,
                            retval.new_address + memory_requirement<HashTableData>::requirement,
                            table_head->capacity );
                        auto i = begin;
                        //copy prev table with respect to the new size
                        for (; i != end; ++i)
                        {
                            assert(nil_c != retval.old_map_function(static_cast<atom_t>(*i)));
                            
                            auto ins_res = do_insert(table_head, hash_data, static_cast<atom_t>(*i));
                            if (!ins_res.second)//bad case - after grow conflict is still there
                            {
                                this->destroy(trie::PersistedReference<HashTableData>(retval.new_address));
                                retval.new_address = FarAddress();
                                break; //go to new loop of grow
                            }
                        }
                        if (i == end) //copy succeeded (there wasn't break)
                        {
                            //cannot pass table_head, hash_data by right-reference, so use trick with shared_ptr
                            auto shared_table_head = std::make_shared<decltype(table_head)>(std::move(table_head));
                            auto shared_hash_data = std::make_shared<decltype(hash_data)>(std::move(hash_data));
                            retval.new_map_function = [this, shared_table_head, shared_hash_data](atom_t key)->fast_dim_t {
                                return this->do_find(*shared_table_head, *shared_hash_data, key);
                            };
                            return retval;
                        }

                    } //end for(new_capacity)
                    //there since reached HashTableCapacity::_256
                    retval.new_map_function = std::move(std::function<fast_dim_t(atom_t)>([](atom_t key)->fast_dim_t {
                        //when no hash table just return identity of key
                        return key;
                    }));
                    return retval;
                }
                /**Evaluate FarAddress of hashtable entry
                * @param base - the address of HashTable header
                * @param idx - index of entry in hash table
                */
                static FarAddress content_item_address(FarAddress base, dim_t idx = 0)
                {
                    return base + (memory_requirement<HashTableData>::requirement +
                        idx * memory_requirement<HashTableData::content_t>::requirement
                        );
                }
            private:
                
                std::pair<dim_t, bool> do_insert(
                    WritableAccess<HashTableData>& head, WritableAccess<HashTableData::content_t>& hash_data, atom_t key)
                {
                    unsigned hash = static_cast<unsigned>(key) & details::bitmask((HashTableCapacity)head->capacity); //assuming that capacity is ^ 2
                    for (unsigned i = 0; i < head->neighbor_width && head->size < head->capacity; ++i)
                    {
                        if (!HashTableData::has_flag(hash_data, hash, fpresence_c))
                        { //nothing at this pos
                            HashTableData::set_value(hash_data, hash, key);
                            head->size++;
                            return std::make_pair(hash, true);
                        }
                        if (HashTableData::get_value(hash_data, hash) == key)
                            return std::make_pair(hash, false); //already exists
                        ++hash %= head->capacity; //keep in boundary
                    }
                    return std::make_pair(dim_nil_c, false); //no capacity
                }
                /** Optimize space before some item is removed
                * @tparam OnMoveCallback - functor that is called if internal space of hash-table is optimized (shrinked). Argumnets (from, to)
                * @return - during optimization this method may change origin param 'erase_pos', so to real erase use index returned
                */
                template <class OnMoveCallback>
                unsigned restore_on_erase(WritableAccess<HashTableData>& table_head,
                    WritableAccess<HashTableData::content_t>& hash_data, unsigned erase_pos, OnMoveCallback& on_move_callback)
                {
                    const unsigned bitmask = details::bitmask((HashTableCapacity)table_head->capacity);//assuming that capacity is ^ 2

                    unsigned erased_hash = static_cast<unsigned>(HashTableData::get_value(hash_data, erase_pos)) & bitmask;
                    unsigned limit = (erase_pos + table_head->neighbor_width) % table_head->capacity; //start from last available neighbor

                    for (unsigned i = (erase_pos + 1) % table_head->capacity; i != limit; ++i %= table_head->capacity)
                    {
                        if (!HashTableData::has_flag(hash_data, i, fpresence_c))
                            break; //stop optimization and erase item at pos
                        unsigned local_hash =
                            static_cast<unsigned>(HashTableData::get_value(hash_data, i)) & bitmask;
                        bool item_in_right_place = i == local_hash;
                        if (item_in_right_place)
                            continue;
                        unsigned x = less_pos(erased_hash, erase_pos, table_head->capacity) ? erase_pos : erased_hash;
                        if (!less_pos(x, local_hash, table_head->capacity)/*equivalent of <=*/)
                        {
                            copy_to(hash_data, erase_pos, i);
                            on_move_callback(i, erase_pos);
                            erase_pos = i;
                            erased_hash = local_hash;
                            limit = (erase_pos + table_head->neighbor_width) % table_head->capacity;
                        }
                    }
                    HashTableData::reset_flag(hash_data, erase_pos, fpresence_c);
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
                static bool less_pos(unsigned tst_min, unsigned tst_max, unsigned capacity) 
                {
                    int dif = static_cast<int>(tst_min) - static_cast<int>(tst_max);
                    unsigned a = std::abs(dif);
                    if (a > (static_cast<unsigned>(capacity) / 2)) //use inversion of signs
                        return dif > 0;
                    return dif < 0;
                }
                template <class Accessor>
                static void copy_to(Accessor &acc, unsigned to, unsigned src)
                {
                    acc[to] = acc[src];
                }
                /**
                * \tparam HeaderView either WritableAccess<HashTableData> or ReadonlyAccess<HashTableData> to discover hashtable header
                * \tparam TableData either WritableAccess<HashTableData::Content> or ReadonlyAccess<HashTableData::Content> to access table bunches
                */
                template <class HeaderView, class TableData>
                fast_dim_t do_find(const HeaderView& head, const TableData& hash_data, atom_t key) const
                {
                    unsigned hash = static_cast<unsigned>(key) & details::bitmask((HashTableCapacity)head->capacity);
                    for (unsigned i = 0; i < head->neighbor_width; ++i)
                    {
                        if (!HashTableData::has_flag(hash_data, hash, fpresence_c))
                        { //nothing at this pos
                            return nil_c;
                        }
                        if (HashTableData::get_value(hash_data, hash) == key)
                            return hash;
                        ++hash %= head->capacity; //keep in boundary
                    }
                    return nil_c;
                }
            private:
                SegmentTopology& _topology;
            };
            template <class T>
            inline SegmentManager& resolve_segment_manager(PersistedHashTable<T>& htbl)
            {
                return htbl.topology().segment_manager();
            }
        } //ns:containers
    }//ns: trie
}//ns: OP


#endif //_OP_TRIE_HASHTABLE__H_
