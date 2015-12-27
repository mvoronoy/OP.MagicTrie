#ifndef _OP_TRIE_STEMCONTAINER__H_
#define _OP_TRIE_STEMCONTAINER__H_
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <algorithm>
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

            struct StemData
            {
                typedef atom_t* data_ptr_t;
                typedef const atom_t* cont_data_ptr_t;
                StemData(dim_t awidth, dim_t aheight)
                    : width(awidth)
                    , height(aheight)
                    , summary_length(0)
                    , count(0)
                    , stem_length{}
                {

                }
                dim_t width;
                dim_t height;
                /**Sums-up all lengts of stored strings*/
                dim_t summary_length;
                /**Number of strings assigned, not really reflects count of string sequentally folowed one by one
                * but just counts number of slots totally occupied, used to evaluate average string length.
                * This number is never exceeds the `width`
                */
                dim_t count;
                /**Store length of all strings in stem container*/
                dim_t stem_length[max_entries_c];
            };
            template <class SegmentTopology>
            struct StemStore
            {
                StemStore(SegmentTopology& topology)
                    : _topology(topology)
                {}

                OP_CONSTEXPR(OP_EMPTY_ARG) static dim_t memory_requirements(dim_t width, dim_t str_max_height)
                {
                    return memory_requirement<StemData>::requirement 
                        + sizeof(atom_t) * (width * str_max_height);
                }

                inline FarAddress create(dim_t width, dim_t str_max_height)
                {
                    //size_t expected = memory_requirements<Tuple>();
                    auto& memmngr = _topology.slot<MemoryManager>();
                    OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    auto mem_size = memory_requirements(width, str_max_height);
                    auto addr = memmngr.allocate( mem_size );
                    auto mem_block = _topology.segment_manager().writable_block(addr, mem_size);
                    new (mem_block.pos()) StemData(width, str_max_height);
                    g.commit();
                    return addr;
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
                template <class T>
                inline void accommodate(FarAddress address, atom_t key, T& begin, T && end)
                {
                    assert(key < data.width);
                    assert(begin != end);
                    OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    //write-lock header part
                    auto data_header = _topology.segment_manager().writable_block(address, 
                        memory_requirement<StemData>::requirement).at<StemData>(0);
                    address += memory_requirement<StemData>::requirement 
                        + sizeof(atom_t)*data_header.height * key;
                    auto f_str = _topology.segment_manager().writable_block(
                        address, data_header.height).at<atom_t>(0);

                    auto& size = data_header.stem_length[key];
                    assert(size == 0);
                    
                    for (; begin != end && size < data_header.height; ++f_str, ++begin, ++size, ++data.summary_length)
                    {
                        *f_str = *begin;
                    }
                    ++data.count;
                    g.commit();
                }
                /**
                *   Detect substr return processed characters number of stem
                */
                template <class T>
                inline dim_t prefix_of(FarAddress address, atom_t key, T& begin, T && end) const
                {
                    //start tran over readonly operation to grant data consistence
                    OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = _topology.segment_manager().ro_at<StemData>(address);

                    assert(key < data_header.width);
                    assert(begin != end);
                    address += memory_requirement<StemData>::requirement 
                        + sizeof(atom_t)*data_header.height * key;
                    auto f_str = _topology.segment_manager().ro_at<atom_t>(address);
                    dim_t i = 0;
                    for (; i < data_header.stem_length[key] && begin != end; ++f_str, ++begin)
                    {
                        if (*f_str != *begin)
                        { //difference in sequence mean that stem should be splitted
                            return i;
                        }
                    }
                    return i;
                    //here rollback goes
                }
                /**Cut already existing string*/
                inline void trunc_str(StemData& data, atom_t key, dim_t shorten) const
                {
                    OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = _topology.segment_manager().wr_at<StemData>(address);

                    assert(key < data_header.width);
                    //assert that can't make str longer
                    assert(data_header.stem_length[key] > shorten);
                    data_header.stem_length[key] = shorten;
                    g.commit();
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
