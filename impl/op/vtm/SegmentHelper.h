#pragma once

#ifndef _OP_VTM_SEGMENTHELPER__H_
#define _OP_VTM_SEGMENTHELPER__H_

#include <cassert>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <op/vtm/typedefs.h>
#include <op/common/Utils.h>

namespace OP::vtm
{

    struct SegmentDef
    {
        constexpr static const segment_idx_t null_block_idx_c = OP::vtm::null_block_idx_c;
        constexpr static const segment_pos_t eos_c = OP::vtm::eos_c;
        constexpr static const far_pos_t far_null_c = OP::vtm::far_null_c;
        enum
        {
            align_c = 16
        };
    };


    /**
    *   Structure that resides at beginning of the each segment
    */
    struct SegmentHeader
    {
        enum
        {
            align_c = SegmentDef::align_c
        };
        
        constexpr SegmentHeader() noexcept
            : _segment_size{ OP::vtm::eos_c }
        {
        }

        constexpr SegmentHeader(segment_pos_t segment_size) noexcept
            : _segment_size(segment_size)
        {
        }

        constexpr segment_pos_t segment_size() const noexcept
        {
            return _segment_size;
        }

        bool check_signature() const noexcept
        {
            return 0 == memcmp(signature, SegmentHeader::seal(), sizeof(signature));
        }

        constexpr static char signature[4] = {'m', 'g', 't', 'r'};
        
        static const char* seal() noexcept
        {
            static const char* signature = "mgtr";
            return signature;
        }

    private:
        segment_pos_t _segment_size;

    };

    namespace details
    {
        using namespace boost::interprocess;

        struct SegmentHelper
        {
            friend struct SegmentManager;

            SegmentHelper(file_mapping& mapping, offset_t offset, std::size_t size) :
                _mapped_region(mapping, read_write, offset, size),
                _avail_bytes(0)
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
                //offset += align_on(s_i_z_e_o_f(Segment), Segment::align_c);
                return reinterpret_cast<T*>(addr + offset);
            }

            std::uint8_t* raw_space() const noexcept
            {
                return at<std::uint8_t>(OP::utils::aligned_sizeof<SegmentHeader>(SegmentHeader::align_c));
            }
            
            segment_pos_t available() const noexcept
            {
                return this->_avail_bytes;
            }

            segment_pos_t to_offset(const void* memblock)
            {
                if (!check_pointer(memblock))
                    throw trie::Exception(trie::er_invalid_block);
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
                    throw trie::Exception(trie::er_invalid_block);
                return reinterpret_cast<std::uint8_t*>(this->_mapped_region.get_address()) + offset;
            }

            void flush(bool assync = true)
            {
                _mapped_region.flush(0, 0, assync);
            }

            void _check_integrity()
            {
                //guard_t l(_free_map_lock);
                //MemoryBlockHeader *first = at<MemoryBlockHeader>(aligned_sizeof<Segment>(Segment::align_c));
                //size_t avail = 0, occupied = 0;
                //size_t free_block_count = 0;
                //MemoryBlockHeader *prev = nullptr;
                //for (;;)
                //{
                //    assert(first->prev_block_offset() < 0);
                //    if (prev)
                //        assert(prev == first->prev());
                //    else
                //        assert(SegmentDef::eos_c == first->prev_block_offset());
                //    prev = first;
                //    first->_check_integrity();
                //    if (first->is_free())
                //    {
                //        avail += first->size();
                //        assert(_free_blocks.end() != find_by_ptr(first));
                //        free_block_count++;
                //    }
                //    else
                //    {
                //        occupied += first->size();
                //        assert(_free_blocks.end() == find_by_ptr(first));
                //    }
                //    if (first->has_next())
                //        first = first->next();
                //    else break;
                //}
                //assert(avail == this->_avail_bytes);
                //assert(free_block_count == _free_blocks.size());
                ////occupied???
            }
        private:
            mapped_region _mapped_region;

            segment_pos_t _avail_bytes;

            /** validate pointer against mapped region range*/
            bool check_pointer(const void* ptr)
            {
                auto byte_ptr = reinterpret_cast<const std::uint8_t*>(ptr);
                auto base = reinterpret_cast<const std::uint8_t*>(_mapped_region.get_address());
                return byte_ptr >= base
                    && byte_ptr < (base + _mapped_region.get_size());
            }
        };

    }//ns::details
}   //ns: OP::vtm

namespace std
{
    /** Define specialization of std::has for FarAddress */
    template<>
    struct hash<OP::vtm::FarAddress>
    {
        std::size_t operator()(const OP::vtm::FarAddress& addr) const noexcept
        {
            return std::hash< OP::vtm::far_pos_t >{}(addr.address);
        }
    };

} //ns: std
#endif //_OP_VTM_SEGMENTHELPER__H_

