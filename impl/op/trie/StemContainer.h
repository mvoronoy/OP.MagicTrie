#ifndef _OP_TRIE_STEMCONTAINER__H_
#define _OP_TRIE_STEMCONTAINER__H_
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <op/common/typedefs.h>
#include <op/common/Utils.h>
#include <op/vtm/SegmentManager.h>
#include <op/trie/Containers.h>
#include <op/vtm/PersistedReference.h>

namespace OP
{
    namespace trie
    {
        namespace stem
        {
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
                unequals,
                no_entry
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
                void set_length(atom_t l)
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
                typedef StemStore<SegmentTopology> this_t;
                typedef PersistedReference<StemData> ref_stems_t;
                StemStore(SegmentTopology& topology)
                    : _topology(topology)
                {}

                /**
                * Create new stem container.
                * @return tuple of:
                * \li address of just created 
                * \li 
                */
                inline std::tuple<ref_stems_t, WritableAccess<StemData>, WritableAccess<atom_t> > create(dim_t width, dim_t str_max_height)
                {
                    auto& memmngr = _topology.slot<HeapManagerSlot>();
                    //query data enough for StemData and stems strings
                    auto header_size = memory_requirement<StemData>::requirement;
                    auto mem_size = header_size + width * str_max_height;
                    auto addr = memmngr.allocate(mem_size);
                    auto mem_block = _topology.segment_manager().writable_block(addr, mem_size);
                    WritableAccess<StemData> header(std::move(mem_block));
                    header.make_new(width, str_max_height); //constructs header
                    auto stems_data_block = mem_block.subset(header_size);
                    return std::make_tuple(
                        ref_stems_t(addr),
                        std::move(header),
                        std::move(WritableAccess<atom_t>(std::move(stems_data_block)))
                        );
                        
                }
                /**Destroy previously allocated by #create stem container*/
                void destroy(const ref_stems_t& stems)
                {
                    auto& memmngr = _topology.slot<HeapManagerSlot>();
                    memmngr.deallocate(stems.address);
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
                    //assert(begin != end);
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    //write-lock header part
                    auto data_header = accessor<StemData>(_topology, st_address.address);
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
                /**
                *   Detect substr contained in stem. 
                * @param rest_of_str - optional string that is poupalated when return StemCompareResult::string_end - that means tail of string that goes after common part
                * @return pair of comparison result and length of overlapped string.
                *
                */
                template <class T>
                inline std::tuple<StemCompareResult, dim_t> prefix_of(
                    const ref_stems_t& st_address, atom_t key, T& begin, T end, atom_string_t * rest_of_str = nullptr) const
                {
                    auto result = std::make_tuple(StemCompareResult::unequals, 0);
                    stem(st_address, key, [&](const atom_t *f_str, const atom_t *f_str_end, const StemData& stem_header) {
                        for (; f_str != f_str_end && begin != end; ++std::get<1>(result), ++begin, ++f_str)
                        {
                            if (*f_str != *begin)
                            { //difference in sequence mean that stem should be splitted
                                return;//stop at: tuple(StemCompareResult::unequals, i);
                            }
                        }
                        //correct result type as one of string_end, equals, stem_end
                        std::get<0>(result) = f_str != f_str_end
                            ? (StemCompareResult::string_end)
                            : (begin == end ? StemCompareResult::equals : StemCompareResult::stem_end);
                        if (rest_of_str)
                        {
                            rest_of_str->append(f_str, f_str_end);
                        }
                    });
                    return result;
                }
                /**Cut already existing string*/
                inline void trunc_str(const ref_stems_t& st_address, atom_t key, dim_t shorten) const
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = _topology.segment_manager().wr_at<StemData>(st_address.address);
                    trunc_str(*data_header, key, shorten);
                    //g.commit();
                }
                inline void trunc_str(StemData& data, atom_t key, dim_t shorten) const
                {
                    assert(key < data.width);
                    //assert that can't make str longer
                    assert(data.stem_length[key] >= shorten);

                    data.summary_length = data.summary_length - data.stem_length[key] + shorten;
                    data.stem_length[key] = shorten;
                }
                /**
                *   Resolve stem matched to specified key
                *   @return tuple of:
                *   -# const pointer to first atom of stem
                *   -# reference (can be modified) to length of stem. 
                *   -# StemData - the header of all stems contained by `address`
                *   @tparam callback - functor with signature `callback(const atom_t* begin, const atom_t* end, const StemData& stem_header)`
                */
                template <class FBack>
                void stem(const ref_stems_t& st_address, atom_t key, FBack& callback) const
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = view<StemData>(_topology, st_address.address);

                    assert(key < data_header->width);
                    auto address = st_address.address + segment_pos_t{ memory_requirement<StemData>::requirement
                        + sizeof(atom_t)*data_header->height * key };
                    auto f_str = array_view<atom_t>(_topology, address, data_header->height);
                    const atom_t * begin = f_str;
                    callback(begin, begin + data_header->stem_length[key], *data_header);
                }
                /** access stem for writing */
                template <class FBack>
                void stemw(const ref_stems_t& st_address, atom_t key, FBack& callback) const
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto data_header = accessor<StemData>(_topology, st_address.address);

                    assert(key < data_header->width);
                    auto address = st_address.address + segment_pos_t{ memory_requirement<StemData>::requirement
                        + sizeof(atom_t)*data_header->height * key };
                    auto f_str = array_view<atom_t>(_topology, address, data_header->height);
                    const atom_t * begin = f_str;
                    callback(begin, begin + data_header->stem_length[key], *data_header);
                }
                inline dim_t stem_length(const ref_stems_t& st_address, atom_t key) const
                {
                    //OP::vtm::TransactionGuard g(_toplogy.segment_manager().begin_transaction());
                    auto ro_access =
                        view<StemData>(_topology, st_address.address);
                    return ro_access->stem_length[key];
                }
                struct MoveProcessor
                {
                    friend struct this_t;
                    void move(dim_t from, dim_t to)
                    {
                        assert(from < _from_header->width);
                        assert(to < _to_header->width);
                        auto len = _from_header->stem_length[from];
                        assert(len <= _to_header->height);
                        
                        _to_header->stem_length[to] = len;
                        _to_data.byte_copy(&_from_data[from * _from_header->height], len, to * _to_header->height);

                        _to_header->summary_length += len;
                        _to_header->count++;
                    }
                    const ref_stems_t& to_address() const
                    {
                        return _to_address;
                    }
                private:
                    MoveProcessor(
                        ReadonlyAccess<StemData>&& from_header, ReadonlyAccess<std::uint8_t>&& from_data,
                        WritableAccess<StemData>&& to_header, WritableAccess<std::uint8_t>&& to_data,
                        ref_stems_t to_address)
                        : _from_header(std::move(from_header))
                        , _from_data(std::move(from_data))
                        , _to_header(std::move(to_header))
                        , _to_data(std::move(to_data))
                        , _to_address(std::move(to_address))
                    {
                        
                    }
                    ReadonlyAccess<StemData> _from_header;
                    ReadonlyAccess<std::uint8_t> _from_data;
                    WritableAccess<StemData> _to_header;
                    WritableAccess<std::uint8_t> _to_data;
                    ref_stems_t _to_address;
                };
                /**
                *   @param previous - in/out holder of address
                *   @!@ @todo current impl is velocity efficient, but must be implmented as space efficient - instead of taking max stem, need base on average length
                */
                MoveProcessor grow(const trie::PersistedReference<StemData>& previous, dim_t new_size)
                {
                    auto ro_access = std::move(view<StemData>(_topology, previous.address));
                    
                    auto ro_data = std::move(array_view<std::uint8_t>(_topology, 
                        previous.address + memory_requirement<StemData>::requirement,
                        ro_access->width * ro_access->height));
                    auto max_height = *std::max_element(ro_access->stem_length, ro_access->stem_length + ro_access->width);
                    //it is possible that all set to 0, so 
                    //@!@ @todo allow nil-stem container
                    if (max_height < 2)
                        max_height = 2;
                    auto new_stem = this->create(new_size, max_height);
                    return MoveProcessor(
                        std::move(ro_access), std::move(ro_data),
                        std::move(tuple_ref<WritableAccess<StemData> >(new_stem)),
                        std::move(tuple_ref<WritableAccess<atom_t> >(new_stem)),
                        tuple_ref<trie::PersistedReference<StemData> >(new_stem)
                        );
                }
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
