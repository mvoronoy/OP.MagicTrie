#ifndef _OP_TRIE_STEMCONTAINER__H_
#define _OP_TRIE_STEMCONTAINER__H_
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <op/trie/Bitset.h>
#include <op/trie/Utils.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Containers.h>

namespace OP
{
    namespace trie
    {
        namespace stem
        {
            typedef std::uint8_t atom_t;
            typedef std::uint_fast8_t atom_fast_t;
            typedef std::uint16_t dim_t;
            typedef std::uint_fast16_t dim_fast_t;
            typedef std::int16_t offset_t;
            /**Integral constants for stem module*/
            enum : dim_t
            {
                /**Mean infinit length*/
                nil_c = dim_t(~0u),
                /**Max enties allowed in stem container*/
                max_entries_c = 256
            };

            struct StemStatistic
            {
                dim_t sum_length = 0;
            };
            template <dim_t Max = 255>
            struct StemString
            {
                typedef atom_t* data_ptr_t;
                typedef const atom_t* cont_data_ptr_t;
                enum
                {
                    max_length_c = Max
                };
                data_ptr_t begin()
                {
                    return _str;
                }
                data_ptr_t end()
                {
                    return _str + _length;
                }
                data_ptr_t max_end()
                {
                    return _str + max_length_c;
                }
                void set_length(dim_fast_t l)
                {
                    _length = l;
                }
            private:
                atom_t _length = 0;
                atom_t _str[max_length_c];
            };
            using StemTable8x32 = NodeHashTable<StemString<32>, 16>;

            template <class SegmentTopology>
            struct StemStore
            {
                typedef atom_t* data_ptr_t;
                typedef const atom_t* cont_data_ptr_t;
                typedef StemTable8x32 stem_table_t;
                
                StemStore(SegmentTopology& topology)
                    : _topology(topology)
                {}

                template < class Tuple >
                OP_CONSTEXPR(OP_EMPTY_ARG) static size_t memory_requirements()
                {
                    return memory_requirement<Tuple>::requirement;
                }

                template < class Tuple >
                inline FarAddress create(Tuple && t = Tuple() )
                {
                    //size_t expected = memory_requirements<Tuple>();
                    auto& memmngr = _toplogy.slot<MemoryManager>();
                    OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());

                    auto addr = memmngr.allocate( memory_requirements<Tuple>()::requirement );
                    auto mem_block = toplogy.segment_manager().writable_block(addr, memory_requirements<Tuple>());
                    Helper<Tuple>::move_into< std::tuple_size<Tuple>::value >(mem_block.pos(), std::forward<Tuple>(t));
                    g.commit();
                    return addr;
                }
                template < class Tuple >
                dim_t size( Tuple&t ) 
                {
                    return tuple_ref<StemLength>(t).size();
                }
                /**Place new sequence specified by [begin-end) range as new item to this storage
                *   @param begin - [in/out] start of range to insert, at [out] points where range insertion was stopped (for
                *                   example if #height() was exceeded.
                *   @return entry index where sequence was placed.
                *   \test 
                *       Have to test against 5 scenarious:
                *           -# Insert brand-new string to this conatiner with enough capacity;
                *           -# Insert string that is wider than existing one;
                *           -# Insert string that is shorter than existing one;
                *           -# Insert string just the same as existing one;
                *           -# Insert string to container that is full.
                */
                template <class Tuple, class T>
                inline void accommodate(Tuple& tuple, atom_t key, T& begin, T && end)
                {
                    assert(begin != end);
                    
                    stem_table_t& stem_table = tuple_ref_by_inheritance<stem_table_t>(tuple);
                    StemStatistic& stat = tuple_ref_by_inheritance<StemStatistic>(tuple);
                    
                    auto insres = stem_table.insert(key);

                    if (insres.second)
                    { //new entry created
                        auto& f_str = stem_table.value(insres.first);
                        dim_fast_t n = 0;
                        for (auto p = f_str.begin(); begin != end && p != f_str.end(); ++p, ++begin, ++n)
                        {
                            *p = *begin;
                        }
                        f_str.set_length(n);
                        stat.sum_length += n;
                    }
                    else if (insres.first == stem_table.end() )
                    { //reach the limit of hashtable, need extend it
                        auto col_begin = mem.begin(column);
                    }
                    else
                    { //there since string already exists, let's detect how many atoms are in common
                        auto& f_str = stem_table.value(insres.first);
                        dim_fast_t n = 0;
                        for (auto p = f_str.begin(); p != f_str.end() && begin != end; ++p, ++begin)
                        {
                            if (*p != *begin)
                            { //difference in sequence mean that stem should be splitted
                                
                            }
                        }
                        stat.sum_length += n;
                    }
                    return result;
                }
                inline void erase(dim_fast_t entry_index)
                {
                    assert(entry_index < _width);
                    //just set length to 0
                    set_length(entry_index) = 0;
                    --_size;
                    //make _first_free leftmost 
                    _first_free = std::min<dim_t>(_first_free, entry_index);
                }

            protected:
            private:
                template <class Tuple>
                struct Helper
                {
                    template <size_t N>
                    static inline std::uint8_t* move_into(std::uint8_t* memory, Tuple && source)
                    {
                        *reinterpret_cast<T*>(memory) = std::move(std::get< std::tuple_size<Tuple>::value - N >(source));
                        memory += memory_requirement<std::tuple_element< std::tuple_size<Tuple>::value - N >::type>::requirement;
                        return move_into<N - 1, Tuple>(memory, source);
                    }
                    template <>
                    static inline std::uint8_t* move_into<0>(std::uint8_t* memory, Tuple && source)
                    {
                        return memory;
                    }
                };
                SegmentTopology& _topology;
            };
        } //ns: stem
    } //ns: trie
}  //ns: OP
#endif //_OP_TRIE_STEMCONTAINER__H_