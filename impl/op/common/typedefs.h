#ifndef _OP_TRIE_TYPEDEFS__H_
#define _OP_TRIE_TYPEDEFS__H_
#include <cstdint>
#include <cassert>

#define OP_EMPTY_ARG
#ifdef _MSC_VER
#if _MSC_VER <= 1800
#define OP_CONSTEXPR(alt) alt 
#define OP_NOEXCEPT
#else
#define OP_CONSTEXPR(alt) constexpr
#define OP_NOEXCEPT noexcept 
#endif
#else
#define OP_CONSTEXPR(alt) constexpr
#define OP_NOEXCEPT noexcept 
#endif //


namespace OP
{
    namespace trie
    {
        typedef std::uint8_t atom_t;
        typedef std::uint16_t dim_t;
        typedef std::uint_fast16_t fast_dim_t;
        enum : dim_t
        {
            dim_nil_c = dim_t(-1)
        };
    
        typedef std::uint32_t header_idx_t;
        typedef std::uint32_t segment_idx_t;
        /**position inside segment*/
        typedef std::uint32_t segment_pos_t;
        typedef std::int32_t segment_off_t;
        /**Combines together segment_idx_t (high part) and segment_pos_t (low part)*/
        typedef std::uint64_t far_pos_t;
        struct SegmentDef
        {
            static const segment_idx_t null_block_idx_c = ~0u;
            static const segment_pos_t eos_c = ~0u;
            static const far_pos_t far_null_c = ~0ull;
            enum
            {
                align_c = 16
            };
        };
        union FarAddress
        {
            far_pos_t address;
            struct
            {
                segment_pos_t offset;
                segment_idx_t segment;
            };
            FarAddress():
                offset(SegmentDef::eos_c), segment(SegmentDef::eos_c){}
            explicit FarAddress(far_pos_t a_address) :
                address(a_address){}
            FarAddress(segment_idx_t a_segment, segment_pos_t a_offset) :
                segment(a_segment),
                offset(a_offset){}
            operator far_pos_t() const
            {
                return address;
            }
            FarAddress operator + (segment_pos_t pos) const
            {
                assert(offset <= (~0u - pos)); //test overflow
                return FarAddress(segment, offset + pos);
            }
            /**Signed operation*/
            FarAddress operator + (segment_off_t a_offset) const
            {
                assert(
                    ( (a_offset < 0)&&(static_cast<segment_pos_t>(-a_offset) < offset) )
                    || ( (a_offset >= 0)&&(offset <= (~0u - a_offset) ) )
                    );

                return FarAddress(segment, offset + a_offset);
            }
            FarAddress& operator += (segment_pos_t pos)
            {
                assert(offset <= (~0u - pos)); //test overflow
                offset += pos;
                return *this;
            }
            /**Find signable distance between to holders on condition they belong to the same segment*/
            segment_off_t diff(const FarAddress& other) const
            {
                assert(segment == other.segment);
                return offset - other.offset;
            }
        };
        template <typename ch, typename char_traits>
        std::basic_ostream<ch, char_traits>& operator<<(std::basic_ostream<ch, char_traits> &os, FarAddress const& addr)
        {

            const typename std::basic_ostream<ch, char_traits>::sentry ok(os);
            if (ok) 
            {
                os << std::setw(8) << std::setbase(16) << std::setfill(os.widen('0')) << addr.segment << ':' << addr.offset;
            }
            return os;
        }

    }//ns:trie
}//ns:OP

#endif //_OP_TRIE_TYPEDEFS__H_