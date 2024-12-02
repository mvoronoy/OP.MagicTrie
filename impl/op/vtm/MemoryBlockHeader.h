#ifndef _OP_TRIE_MEMORYBLOCKHEADER__H_
#define _OP_TRIE_MEMORYBLOCKHEADER__H_

#include <op/vtm/SegmentManager.h>

namespace OP::vtm
{

    /**
    * Describe block of memory
    * Each block is placed to list of all memory blocks and managed by `next`, `prev` navigation methods.
    */
    struct MemoryBlockHeader
    {
        constexpr static inline std::uint32_t signature_c = 0x3757;
        
        MemoryBlockHeader() = delete;
        MemoryBlockHeader(const MemoryBlockHeader&) = delete;

        /** Special constructor for placement new */
        explicit constexpr MemoryBlockHeader(emplaced_t) noexcept
            : _free(1)
            , _has_next(0)
            , _signature(signature_c)
        {
            nav._size = SegmentDef::eos_c;
            nav._prev_block_offset = SegmentDef::eos_c;
        }
        
        /** Constructor for find purpose only*/
        explicit constexpr MemoryBlockHeader(segment_pos_t user_size) noexcept
            : MemoryBlockHeader(emplaced_t{})
        {
            nav._size = user_size;
        }

        constexpr bool check_signature() const noexcept
        {
            return _signature == signature_c;
        }
        
        constexpr segment_pos_t size() const noexcept
        {
            return nav._size;
        }

        /**interior size of block aligned to Segment::align */
        MemoryBlockHeader* size(segment_pos_t byte_size) noexcept
        {
            nav._size = (byte_size);
            return this;
        }

        /**@return real occupied bytes size()+sizeof(*this)*/
        segment_pos_t real_size() const noexcept
        {
            return size() + OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
        }

        segment_idx_t my_segement() const noexcept
        {
            return nav._my_segment;
        }

        MemoryBlockHeader* my_segement(segment_idx_t segment) noexcept
        {
            nav._my_segment = segment;
            return this;
        }

        segment_off_t prev_block_offset() const noexcept
        {
            return nav._prev_block_offset;
        }

        MemoryBlockHeader* prev_block_offset(segment_off_t offset) noexcept
        {
            nav._prev_block_offset = offset;
            return this;
        }

        constexpr bool is_free() const noexcept
        {
            return this->_free;
        }

        MemoryBlockHeader* set_free(bool flag) noexcept
        {
            this->_free = flag ? 1 : 0;
            return this;
        }

        constexpr bool has_next() const noexcept
        {
            return this->_has_next;
        }

        MemoryBlockHeader* set_has_next(bool flag) noexcept
        {
            this->_has_next = flag ? 1 : 0;
            return this;
        }

        /**
        *   Optionally split this block. This depends on available size of memory, if block big enough then
        * this operation makes 2 adjacent blocks  - one for queried bytes and another with rest of memory.
        * If current block is to small to fit queried `byte_size+ sizeof(MemoryBlockHeader)` then entire this
        * should be used
        * @param byte_size - user needs byte size (not real_size - without sizeof(MemoryBlockHeader)) Must be alligned on SegmentDef::align_c
        * @return  a writable memory range for user purpose (it is what about of MemoryBlockHeader)
        */
        FarAddress split_this(SegmentManager& segment_manager, far_pos_t this_pos, segment_pos_t byte_size) noexcept
        {
            assert(_free);
            OP_CONSTEXPR(const) segment_pos_t mbh_size_align = 
                OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
            assert(size() >= byte_size);
            //alloc new segment at the end of current
            segment_pos_t offset = size() - byte_size;
            //allocate new block inside this memory but mark this as a free
            FarAddress access_pos(this_pos + (/*mbh_size_align + */offset));
            auto access = segment_manager
                .writable_block(
                    access_pos, 
                    mbh_size_align/*byte_size - don't need write entire block*/, 
                    WritableBlockHint::new_c);

            MemoryBlockHeader* result = new (access.pos()) MemoryBlockHeader(emplaced_t{});
            result
                ->size(byte_size)
                ->prev_block_offset(FarAddress(this_pos).diff(access_pos))
                ->set_free(false)
                ->set_has_next(this->has_next())
                ->my_segement(segment_of_far(this_pos))
                ;
            if (this->has_next())
            {
                FarAddress next_pos(this_pos + real_size());
                MemoryBlockHeader* next = segment_manager.wr_at<MemoryBlockHeader>(next_pos);
                next->prev_block_offset(access_pos.diff(next_pos));
            }
            this
                ->size(offset - mbh_size_align)
                ->set_has_next(true)
                ;
            //user memory block starts after access_pos + mbh
            return access_pos + mbh_size_align;
        }

        constexpr std::uint8_t* memory() const noexcept
        {
            return ((std::uint8_t*)this) + OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
        }

        void _check_integrity() const noexcept
        {
            assert(this->check_signature());
        }

        std::uint32_t _free : 1;
        std::uint32_t _has_next : 1;
        std::uint32_t _signature : 30;

        struct data_t
        {
            segment_idx_t _my_segment;
            segment_pos_t _size;
            segment_off_t _prev_block_offset;
        };

        data_t nav{};
        //mutable std::uint8_t mem[1] = { 0 };
    };

    static_assert(std::is_standard_layout_v<MemoryBlockHeader>, "MemoryBlockHeader must have standard layout");

    struct ForwardListBase
    {
        constexpr ForwardListBase() noexcept
            : next(SegmentDef::far_null_c) 
        {}
        far_pos_t next;
    };

    /**
    *   When Memory block is released it have to be placed to special re-cycling container.
    *   This structure is placed inside MemoryBlockHeader (so doesn't consume space) to support
    *   sorted navigation between free memory blocks.
    */
    struct FreeMemoryBlock : public OP::vtm::ForwardListBase
    {
        /**Special constructor for objects allocated in existing memory*/
        explicit FreeMemoryBlock(emplaced_t) noexcept
        {
        }

        /**
        *   Taking this far address convert it to far address of associated MemoryBlockHeader
        */
        static far_pos_t get_header_addr(far_pos_t this_addr) noexcept
        {
            return this_addr - OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
        }
        /**
        *   Taking this far address convert it to far address of associated MemoryBlockHeader
        */
        static far_pos_t get_addr_by_header(far_pos_t header_addr) noexcept
        {
            return header_addr + OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
        }

    };

} //endof OP
#endif //_OP_TRIE_MEMORYBLOCKHEADER__H_
