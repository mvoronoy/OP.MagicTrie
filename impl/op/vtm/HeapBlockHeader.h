#pragma once

#ifndef _OP_VTM_HEAPBLOCKHEADER__H_
#define _OP_VTM_HEAPBLOCKHEADER__H_

#include <op/vtm/SegmentManager.h>

namespace OP::vtm
{
    struct ForwardListBase
    {
        constexpr ForwardListBase() noexcept
            : _next{} //is_nil == true
        {
        }
        
        constexpr explicit ForwardListBase(FarAddress next) noexcept
            : _next{next}
        {
        }
        
        FarAddress _next;
    };

    /**
    * Describe block of memory
    * Each block is placed to list of all memory blocks and managed by `next`, `prev` navigation methods.
    */
    struct HeapBlockHeader
    {
        constexpr static inline std::uint32_t signature_c = 0x3757;
        constexpr static segment_pos_t minimal_space_c = 32;
        
        HeapBlockHeader() = delete;
        HeapBlockHeader(const HeapBlockHeader&) = delete;

        /** Special constructor for placement new */
        explicit constexpr HeapBlockHeader(std::in_place_t) noexcept
            : _free(1)
            , _signature{signature_c}
            , _size{SegmentDef::eos_c}
            , _next{}
        {
        }
        
        /** Constructor for find purpose only*/
        explicit constexpr HeapBlockHeader(segment_pos_t user_size) noexcept
            : HeapBlockHeader(std::in_place_t{})
        {
            _size = user_size;
        }

        constexpr bool check_signature() const noexcept
        {
            return _signature == signature_c;
        }
        
        constexpr segment_pos_t size() const noexcept
        {
            return _size;
        }

        /**interior size of block aligned to Segment::align */
        HeapBlockHeader* size(segment_pos_t byte_size) noexcept
        {
            _size = byte_size;
            return this;
        }

        /**@return real occupied bytes size()+sizeof(*this)*/
        segment_pos_t real_size() const noexcept
        {
            return size() + OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c);
        }

        FarAddress next() const noexcept
        {
            return _next;
        }

        HeapBlockHeader* next(FarAddress addr) noexcept
        {
            _next = addr;
            return this;
        }

        constexpr bool is_free() const noexcept
        {
            return this->_free;
        }

        HeapBlockHeader* set_free(bool flag) noexcept
        {
            this->_free = flag ? 1 : 0;
            return this;
        }

        /**
        *   Optionally split this block. This depends on available size of memory, if block big enough then
        * this operation makes 2 adjacent blocks  - one for queried bytes and another with rest of memory.
        * If current block is to small to fit queried `byte_size+ sizeof(HeapBlockHeader)` then entire this
        * should be used
        * @param byte_size - user needs byte size (not a real_size - without sizeof(HeapBlockHeader)) Must be alligned on SegmentDef::align_c
        * @return address of a writable memory for user purpose (just above of HeapBlockHeader)
        */
        FarAddress split_this(SegmentManager& segment_manager, FarAddress this_addr, segment_pos_t byte_size) noexcept
        {
            assert(_free);
            constexpr segment_pos_t header_size_c = 
                OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c);
            //alloc new block at the end of current
            segment_pos_t offset = size() - byte_size;
            assert(offset >= minimal_space_c);

            //allocate new block inside this memory but mark this as a free
            FarAddress new_block_addr = this_addr + offset;

            HeapBlockHeader* result = segment_manager.wr_at<HeapBlockHeader>(
                new_block_addr, WritableBlockHint::new_c);

            ::new (result) HeapBlockHeader(std::in_place_t{});

            result
                ->size(byte_size)
                ->set_free(false)
                ;

            this
                ->size(offset - header_size_c)
                ;
            //user memory block starts after access_pos + mbh
            return get_addr_by_header(new_block_addr);
        }
        /**
        *   Taking the far address of block convert it to the associated HeapBlockHeader
        */
        static FarAddress get_header_addr(FarAddress this_addr) noexcept
        {
            return this_addr + (-static_cast<segment_off_t>(OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c)));
        }

        /**
        *   Taking the far address of HeapBlockHeader convert it to user memory block
        */
        static FarAddress get_addr_by_header(FarAddress header_addr) noexcept
        {
            return header_addr + OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c);
        }

        void _check_integrity() const noexcept
        {
            assert(this->check_signature());
        }

        std::uint32_t _free : 1;
        std::uint32_t _signature : 30;

        segment_pos_t _size;
        FarAddress _next;
    };

    static_assert(std::is_standard_layout_v<HeapBlockHeader>, "HeapBlockHeader must have standard layout");


    /**
    *   When Memory block is released it have to be placed to special re-cycling container.
    *   This structure is placed inside HeapBlockHeader (so doesn't consume space) to support
    *   sorted navigation between free memory blocks.
    */
    struct FreeMemoryBlock : ForwardListBase
    {
        /**Special constructor for objects allocated in existing memory*/
        explicit FreeMemoryBlock(std::in_place_t) noexcept
        {
        }

        /**
        *   Taking the far address of block convert it to the associated HeapBlockHeader
        */
        static FarAddress get_header_addr(FarAddress this_addr) noexcept
        {
            return this_addr + (- static_cast<segment_off_t>(OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c)));
        }

        /**
        *   Taking the far address of HeapBlockHeader convert it to user memory block
        */
        static FarAddress get_addr_by_header(FarAddress header_addr) noexcept
        {
            return header_addr + OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c);
        }

    };

} //endof OP

#endif //_OP_VTM_HEAPBLOCKHEADER__H_
