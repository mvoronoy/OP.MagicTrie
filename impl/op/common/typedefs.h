#ifndef _OP_TRIE_TYPEDEFS__H_
#define _OP_TRIE_TYPEDEFS__H_
#include <cstdint>
#include <cassert>
#include <limits>
#include <cstddef>
#include <string>
#include <iomanip>

#if defined( _MSC_VER ) && defined(max)
#undef max
#endif

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

#define OP_TEMPL_METH(method) template method

#if __cplusplus >= 202002L
    #define OP_VIRTUAL_CONSTEXPR constexpr virtual
    #define OP_CONSTEXPR_CPP20 constexpr 
#else
    #define OP_VIRTUAL_CONSTEXPR virtual
    #define OP_CONSTEXPR_CPP20
#endif //c++20


namespace OP
{
    namespace trie
    {
        typedef std::uint8_t atom_t;
        typedef std::pair<bool, atom_t> nullable_atom_t;
        typedef std::basic_string<atom_t> atom_string_t;
        typedef std::basic_string_view<atom_t> atom_string_view_t;
        
        inline constexpr atom_t operator "" _atom(char c)
        {
            return (atom_t)c;
        }
        inline const atom_t* operator "" _atom(const char* str, size_t n)
        {
            return (const atom_t*)(str);
        }

        inline OP::trie::atom_string_t operator "" _astr(const char* str, size_t n)
        {
            return OP::trie::atom_string_t{reinterpret_cast<const atom_t*>(str), n};
        }

        typedef std::uint16_t dim_t;
        typedef std::uint_fast16_t fast_dim_t;
        enum : dim_t
        {
            dim_nil_c = dim_t(-1)
        };
        inline nullable_atom_t make_nullable(dim_t index)
        {
            return index > std::numeric_limits<atom_t>::max() 
                ? std::make_pair(false, std::numeric_limits<atom_t>::max()) 
                : std::make_pair(true, (atom_t)index);
        }
    
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
            FarAddress() noexcept:
                offset(SegmentDef::eos_c), segment(SegmentDef::eos_c){}
            explicit FarAddress(far_pos_t a_address) noexcept:
                address(a_address){}
            FarAddress(segment_idx_t a_segment, segment_pos_t a_offset) noexcept:
                segment(a_segment),
                offset(a_offset){}
            operator far_pos_t() const
            {
                return address;
            }
            FarAddress operator + (segment_pos_t pos) const
            {
                assert(offset <= (~segment_pos_t(0) - pos)); //test overflow
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
            bool is_nil() const
            {
                return address == SegmentDef::far_null_c;
            }
        };
        /**get segment part from far address*/
        inline segment_idx_t segment_of_far(far_pos_t pos)
        {
            return static_cast<segment_idx_t>(pos >> 32);
        }
        /**get offset part from far address*/
        inline segment_pos_t pos_of_far(far_pos_t pos)
        {
            return static_cast<segment_pos_t>(pos);
        }

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