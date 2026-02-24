#pragma once

#ifndef _OP_VTM_SEGMENTREGION__H_
#define _OP_VTM_SEGMENTREGION__H_

#include <cassert>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <op/vtm/typedefs.h>
#include <op/common/Utils.h>

namespace OP::vtm
{

    struct BaseSegmentManager;
    /**
    *   Header of each segment.
    */
    struct SegmentHeader
    {
        friend BaseSegmentManager;
        constexpr static std::uint32_t signature_value_c = (((std::uint32_t{ 'm' } << 8 | 'g') << 8 | 't') << 8) | 'r';
        constexpr static size_t align_c = SegmentDef::align_c;
        
        constexpr explicit SegmentHeader(segment_pos_t segment_size) noexcept
            : _segment_size(segment_size)
        {
        }

        constexpr segment_pos_t segment_size() const noexcept
        {
            return _segment_size;
        }

        bool check_signature() const noexcept
        {
            return _signature == signature_value_c;
        }

    private:
        SegmentHeader() noexcept : _segment_size(0) {}

        const std::uint32_t _signature = signature_value_c;
        segment_pos_t _segment_size;

    };

    namespace bip = boost::interprocess;

    struct SegmentRegion
    {
        friend struct SegmentManager;

        SegmentRegion(bip::file_mapping& mapping, bip::offset_t offset, std::size_t size) 
            : _mapped_region(mapping, bip::read_write, offset, size)
        {
        }

        SegmentHeader& get_header() const
        {
            return *at<SegmentHeader>(0);
        }
            
        template <class T>
        T* at(segment_pos_t offset) const
        {
            auto* addr = reinterpret_cast<std::uint8_t*>(_mapped_region.get_address());
            return reinterpret_cast<T*>(addr + offset);
        }

        std::uint8_t* raw_space() const noexcept
        {
            return at<std::uint8_t>(OP::utils::aligned_sizeof<SegmentHeader>(SegmentDef::align_c));
        }
            
        segment_pos_t to_offset(const void* memblock)
        {
            if (!check_pointer(memblock))
                throw Exception(vtm::ErrorCodes::er_invalid_block);
            return unchecked_to_offset(memblock);
        }

        segment_pos_t unchecked_to_offset(const void* memblock) noexcept
        {
            return static_cast<segment_pos_t> (
                reinterpret_cast<const std::uint8_t*>(memblock) - reinterpret_cast<const std::uint8_t*>(this->_mapped_region.get_address())
                );
        }

        std::uint8_t* from_offset(segment_pos_t offset)
        {
            if (offset >= this->_mapped_region.get_size())
                throw Exception(vtm::ErrorCodes::er_invalid_block);
            return reinterpret_cast<std::uint8_t*>(this->_mapped_region.get_address()) + offset;
        }

        void flush(bool async = true)
        {
            _mapped_region.flush(0, 0, async);
        }

        void _check_integrity()
        {
            if (!get_header().check_signature())
            {
                std::ostringstream os;
                os << "SegmentRegion::_check_integrity SegmentHeader signature check failed for file mapped:"
                    << std::hex
                    << " size=0x" << _mapped_region.get_size()
                    << ", mem-address=0x" << _mapped_region.get_address()
                    << ", mode=0x" << _mapped_region.get_mode();
                throw std::runtime_error(os.str().c_str());
            }
        }
    private:
        bip::mapped_region _mapped_region;

        /** validate pointer against mapped region range*/
        bool check_pointer(const void* ptr)
        {
            auto byte_ptr = reinterpret_cast<const std::uint8_t*>(ptr);
            auto base = reinterpret_cast<const std::uint8_t*>(_mapped_region.get_address());
            return byte_ptr >= base
                && byte_ptr < (base + _mapped_region.get_size());
        }
    };

}   //ns: OP::vtm


#endif //_OP_VTM_SEGMENTREGION__H_

