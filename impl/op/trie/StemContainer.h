#ifndef _OP_TRIE_STEMCONTAINER__H_
#define _OP_TRIE_STEMCONTAINER__H_
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <op/trie/Utils.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Containers.h>
#include <op/trie/PersistedReference.h>

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
                max_entries_c = 256,
                max_stem_length_c = 255
            };

            /**
            * Describe result of stem compare
            *    |   src   |     stem    |
            *  1    ""          ""         duplicate
            *  2    ""           x         split stem on length of src, so terminal is for src and for x
            *  3     x          ""         add x to page pointed by stem
            *  4     x           y         create child with 2 entries: x, y (check if followed of y can be optimized)
            */
            enum class StemCompareResult
            {
                /**Source fully string matches to the existing stem*/
                equals,
                /**String fully fit to stem, but stem is longer*/
                string_end,
                /**Stem part is fully equal to string, but string is longer*/
                stem_end,
                /**Stem and string not fully matches*/
                unequals
            };
            template <dim_t Max = max_stem_length_c>
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

            /**Abstraction that takes noded and allows iterate over stem contained in it. */
            struct StemOfNode
            {
                typedef StemOfNode this_t;
            
                StemOfNode(atom_t key, dim_t offset, const atom_t* begin)
                    : _key(key)
                    , _offset(offset)
                    , _begin(begin)
                {
                }
            
                atom_t operator *() const
                {
                    if (_offset == 0)
                    {
                        return _key;
                    }
                    return _begin[_offset - 1];
                }
                this_t& operator ++()
                {
                    ++_offset;
                    return *this;
                }
                this_t operator ++(int)
                {
                    auto zhis = *this;
                    ++_offset;
                    return zhis;
                }
                bool operator == (const this_t& other) const
                {
                    return _key == other._key
                        && _offset == other._offset
                        && _begin == other._begin;
                }
                bool operator != (const this_t& other) const
                {
                    return !operator == (other);
                }
                
            private:
                atom_t _key;
                dim_t _offset;
                const atom_t *_begin;
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
                    assert(height <= max_stem_length_c);
                }
                const dim_t width;
                const dim_t height;
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
                typedef PersistedReference<StemData> ref_stems_t;
                StemStore(SegmentTopology& topology)
                    : _topology(topology)
                {}

                OP_CONSTEXPR(OP_EMPTY_ARG) static dim_t memory_requirements(dim_t width, dim_t str_max_height)
                {
                    return memory_requirement<StemData>::requirement
                        + sizeof(atom_t) * (width * str_max_height);
                }

                inline std::tuple<ref_stems_t, StemData*> create(dim_t width, dim_t str_max_height)
                {
                    //size_t expected = memory_requirements<Tuple>();
                    auto& memmngr = _topology.slot<MemoryManager>();
                    //OP::vtm::TransactionGuard g(_topology.segment_manager().begin_transaction());
                    //query data enough for StemData and stems strings
                    auto mem_size = memory_requirements(width, str_max_height);
                    auto addr = memmngr.allocate(mem_size);
                    auto mem_block = _topology.segment_manager().writable_block(addr, mem_size);
                    return std::make_tuple(
                        ref_stems_t(addr), new (mem_block.pos()) StemData(width, str_max_height));
                    //g.commit();
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
                inline void accommodate(const ref_stems_t& st_address, atom_t key, T& begin, T &&end)
                {
                    assert(begin != end);
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    //write-lock header part
                    auto data_header = _topology.segment_manager().wr_at<StemData>(
                        st_address.address);
                    assert(key < data_header->width);
                    auto address = st_address.address + static_cast<segment_pos_t>( memory_requirement<StemData>::requirement
                        + data_header->height * key );
                    auto f_str = _topology.segment_manager().writable_block(
                        address, data_header->height).at<atom_t>(0);

                    auto& size = data_header->stem_length[key];
                    assert(size == 0);

                    for (; begin != end && size < data_header->height; ++begin, ++size)
                    {
                        f_str[size] = *begin;
                    }
                    data_header->summary_length += size;
                    ++data_header->count;
                    //g.commit();
                }
                typedef std::tuple<StemCompareResult, dim_t> prefix_result_t;
                /**
                *   Detect substr return processed characters number of stem
                */
                template <class T>
                inline prefix_result_t prefix_of(const ref_stems_t& st_address, atom_t key, T& begin, T end) const
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto ro_access = _topology.segment_manager()
                        .readonly_access<StemData>(st_address.address);
                    auto &data_header = *ro_access;

                    assert(key < data_header.width);
                    assert(begin != end);
                    auto address = st_address.address + segment_pos_t{ memory_requirement<StemData>::requirement
                        + sizeof(atom_t)*data_header.height * key };
                    auto str_block = _topology.segment_manager().readonly_block(address, sizeof(atom_t)*data_header.height);
                    auto f_str = str_block.at<atom_t>(0);
                    dim_t i = 0;
                    auto stem_len = data_header.stem_length[key];
                    for (; i < stem_len && begin != end; ++i, ++begin)
                    {
                        if (f_str[i] != *begin)
                        { //difference in sequence mean that stem should be splitted
                            return std::make_tuple(StemCompareResult::unequals, i);
                        }
                    }
                    return std::make_tuple(
                        i < stem_len
                        ? (StemCompareResult::string_end)
                        : (begin == end ? StemCompareResult::equals : StemCompareResult::stem_end)
                        , i);
                }
                /**Cut already existing string*/
                inline void trunc_str(const ref_stems_t& st_address, atom_t key, dim_t shorten) const
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = _topology.segment_manager().wr_at<StemData>(st_address.address);
                    trunc_str(data_header, key, shorten);
                    //g.commit();
                }
                inline void trunc_str(StemData& data, atom_t key, dim_t shorten) const
                {
                    assert(key < data.width);
                    //assert that can't make str longer
                    assert(data.stem_length[key] > shorten);

                    data.summary_length = data.summary_length - data.stem_length[key] + shorten;
                    data.stem_length[key] = shorten;
                }
                /**
                *   Resolve stem matched to specified key
                *   @return tuple of:
                *   -# const pointer to first atom of stem
                *   -# reference (can be modified) to length of stem. 
                *   -# StemData - the header of all stems contained by `address`
                */
                std::tuple<const atom_t*, dim_t, StemData& > stem(const ref_stems_t& st_address, atom_t key)
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = _topology.segment_manager().wr_at<StemData>(st_address.address);

                    assert(key < data_header->width);
                    auto address = st_address.address + segment_pos_t{ memory_requirement<StemData>::requirement
                        + sizeof(atom_t)*data_header->height * key };
                    auto f_str = _topology.segment_manager().writable_block(address, sizeof(atom_t)*data_header->height);
                    
                    return std::make_tuple((atom_t*)f_str.pos(), /*std::ref*/(data_header->stem_length[key]), std::ref(*data_header));
                }
                /**
                *   @param previous - in/out holder of address
                *   @!@ @todo current impl is velocity efficient, but must be implmented as space efficient - instead of taking max stem, need base on average length
                */
                std::tuple<ref_stems_t, StemData*> grow(const trie::PersistedReference<StemData>& previous, dim_t new_size)
                {
                    auto ro_access = _topology.segment_manager()
                        .readonly_access<StemData>(previous.address);
                    auto max_height = *std::max_element(ro_access->stem_length, ro_access->stem_length + ro_access->width);
                    //it is possible that all set to 0, so 
                    //@!@ @todo allow nil-stem container
                    if (max_height < 2)
                        max_height = 2;
                    return this->create(new_size, max_height);
                }
                void move_item(const ref_stems_t& st_address, dim_t from_idx, StemData& to, dim_t to_idx)
                {
                    auto ro_header = _topology.segment_manager()
                        .readonly_access<StemData>(st_address.address);
                    auto from_addr = st_address.address + segment_pos_t{ memory_requirement<StemData>::requirement
                        + sizeof(atom_t)*ro_header->height * from_idx };
                    auto ro_block = _topology.segment_manager().readonly_block(from_addr, sizeof(atom_t)*ro_header->height);
                    atom_t* dest = reinterpret_cast<atom_t*>(&to) + memory_requirement<StemData>::requirement
                        + sizeof(atom_t) * (to_idx * ro_header->height);
                    assert(ro_header->stem_length[from_idx] <= to.height);
                    memcpy(dest, ro_block.pos(), ro_header->stem_length[from_idx]);
                    to.stem_length[to_idx] = ro_header->stem_length[from_idx];
                    to.summary_length += ro_header->stem_length[from_idx];
                    ++to.count;
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
