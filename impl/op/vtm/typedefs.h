#pragma once

#ifndef _OP_VTM_TYPEDEFS__H_
#define _OP_VTM_TYPEDEFS__H_

#include <cstdint>
#include <cassert>
#include <limits>
#include <cstddef>
#include <string>
#include <iomanip>

#include <op/common/astr.h>


namespace OP::vtm
{
    using dim_t = std::uint16_t;
    using fast_dim_t = std::uint_fast16_t;

    constexpr static inline const dim_t dim_nil_c = ~dim_t{0};

    using header_idx_t = std::uint32_t;
    using segment_idx_t = std::uint32_t;
    /**position inside segment*/
    using segment_pos_t = std::uint32_t;
    using segment_off_t = std::int32_t;
    /**Combines together segment_idx_t (high part) and segment_pos_t (low part)*/
    using far_pos_t = std::uint64_t;

    constexpr static inline const size_t        offset_bits_c = sizeof(segment_pos_t) << 3;
    constexpr static inline const segment_idx_t null_block_idx_c = ~segment_idx_t{0};
    constexpr static inline const segment_pos_t eos_c           = ~segment_pos_t{0};
    constexpr static inline const far_pos_t     far_null_c      = ~far_pos_t{0};

    struct SegmentDef
    {
        constexpr static segment_idx_t null_block_idx_c = OP::vtm::null_block_idx_c;
        constexpr static segment_pos_t eos_c = OP::vtm::eos_c;
        constexpr static far_pos_t far_null_c = OP::vtm::far_null_c;
        constexpr static size_t align_c = alignof(std::max_align_t);//16;
    };


    /** Define unsigned character that has an unassigned indicator (nullable) */
    struct NullableAtom
    {
        using atom_t = OP::common::atom_t;
        constexpr static inline fast_dim_t bit_empty_c = static_cast<fast_dim_t>(1 << (sizeof(atom_t) << 3));

        constexpr NullableAtom() noexcept
            : _value{bit_empty_c}
        {
        }

        explicit constexpr NullableAtom(fast_dim_t value) noexcept
            : _value( 
                value > std::numeric_limits<atom_t>::max()
                ? bit_empty_c
                : static_cast<atom_t>(value) )
        {
        }
        
        constexpr bool operator!() const noexcept
        {
            return (bit_empty_c & _value);
        }

        explicit constexpr operator bool() const noexcept
        {
            return !operator!();
        }

        constexpr atom_t value() const noexcept
        {
            assert(!operator!());
            return static_cast<atom_t>(_value & (bit_empty_c-1));
        }

        void reset() noexcept
        {
            _value = bit_empty_c;
        }

    private:
        fast_dim_t _value;
    };

    /**get segment part from far address*/
    constexpr inline segment_idx_t segment_of_far(far_pos_t pos) noexcept
    {
        return static_cast<segment_idx_t>(pos >> offset_bits_c);
    }

    /**get offset part from far address*/
    constexpr inline segment_pos_t pos_of_far(far_pos_t pos) noexcept
    {
        return static_cast<segment_pos_t>(pos);
    }


    /** Represent composite address of VTM segment + offset inside single segment */  
    struct FarAddress
    {
        far_pos_t address;

        constexpr FarAddress() noexcept 
            : address{OP::vtm::far_null_c}
        {}

        constexpr explicit FarAddress(far_pos_t a_address) noexcept 
            : address(a_address) 
        {}

        constexpr FarAddress(segment_idx_t a_segment, segment_pos_t a_offset) noexcept 
            : address((static_cast<far_pos_t>(a_segment) << offset_bits_c) | a_offset)
        {}

        constexpr operator far_pos_t() const noexcept
        {
            return address;
        }

        constexpr FarAddress operator + (segment_pos_t pos) const noexcept
        {
            segment_pos_t offset = pos_of_far(address);
            assert(offset <= (eos_c - pos)); //test overflow
            // as soon as operation doesn't cause overflow + is safe to apply explicitly to address
            return FarAddress(address + pos);
        }

        /**Signed operation*/
        constexpr FarAddress operator + (segment_off_t a_offset) const noexcept
        {
            segment_pos_t offset = pos_of_far(address);
            assert(
                ((a_offset < 0) && (static_cast<segment_pos_t>(-a_offset) < offset))
                || ((a_offset >= 0) && (offset <= (eos_c - a_offset)))
            );

            // as soon as operation doesn't cause overflow + is safe to apply explicitly to address
            return FarAddress(address + a_offset);
        }

        FarAddress& operator += (segment_pos_t pos) noexcept
        {
            segment_pos_t offset = pos_of_far(address);
            assert(offset <= (eos_c - pos)); //test overflow
            // as soon as operation doesn't cause overflow += is safe to apply explicitly to address
            //offset += pos;
            address += pos;
            return *this;
        }

        /**Find signable distance between to holders on condition they belong to the same segment*/
        segment_off_t diff(const FarAddress& other) const noexcept
        {
            segment_pos_t offset = pos_of_far(address);
            assert(segment_of_far(address) == segment_of_far(other.address));
            return static_cast<segment_off_t>(offset - pos_of_far(other.address));
        }
        
        constexpr bool is_nil() const noexcept
        {
            return address == far_null_c;
        }

        constexpr segment_pos_t offset() const noexcept
        {
            assert(address != far_null_c);
            return pos_of_far(address);
        }

        constexpr void set_offset(segment_pos_t a_offset) noexcept
        {
            address = (address & (static_cast<far_pos_t>(null_block_idx_c) << offset_bits_c)) | a_offset;
        }

        constexpr segment_idx_t segment() const noexcept
        {
            assert(address != far_null_c);
            return segment_of_far(address);
        }

        friend constexpr bool operator == (const FarAddress& left, const FarAddress& right) noexcept
        {
            return left.address == right.address;
        }

        friend constexpr bool operator != (const FarAddress& left, const FarAddress& right) noexcept
        {
            return left.address != right.address;
        }
    };

    template <typename ch, typename char_traits>
    std::basic_ostream<ch, char_traits>& operator <<(std::basic_ostream<ch, char_traits>& os, FarAddress const& addr)
    {
        auto old_flags = os.flags(); 
        os << std::setw(8) << std::setbase(16) << std::setfill(os.widen('0')) 
           << addr.segment() << ':' << addr.offset();
        os.flags(old_flags); 
        
        return os;
    }


}//ns:OP::vtm

namespace std
{
    /** Define specialization of std::has for FarAddress */
    template<>
    struct hash<OP::vtm::FarAddress>
    {
        std::size_t operator()(const OP::vtm::FarAddress& addr) const noexcept
        {
            return std::hash<OP::vtm::far_pos_t>{}(addr.address);
        }
    };

} //ns: std
#endif //_OP_VTM_TYPEDEFS__H_
