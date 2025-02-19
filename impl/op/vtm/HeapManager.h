#pragma once
#ifndef _OP_VTM_MEMORYMANAGER__H_
#define _OP_VTM_MEMORYMANAGER__H_

#include <vector>
#include <op/common/IoFlagGuard.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/SegmentTopology.h>
#include <op/vtm/MemoryBlock.h>
#include <op/vtm/Skplst.h>

#include <op/vtm/integrity/Stream.h>

namespace OP::vtm
{
    /**
    * \brief Heap management for dynamic memory allocations
    *  Has following structure (heap always resides at the end of each segment): \code
    * [Segment 0: ..{other slots}..., [Log2SkipList of ForwardListBase ordered by size] [HeapHeader]{HeapBlockHeader......}
    * [Segment 1: ..{other slots}..., [HeapHeader]{HeapBlockHeader......}
    * [Segment 2: ..{other slots}..., [HeapHeader]{HeapBlockHeader......}
    * ...
    * \endcode
    */ 
    struct HeapManagerSlot : public Slot
    {
        explicit HeapManagerSlot(SegmentManager& manager) noexcept
            :Slot(manager)
        {
        }

        ~HeapManagerSlot() override
        {
        }

        /**
        * Allocate memory block without initialization that can accommodate `sizeof(T)*count` bytes. As usual result block little bigger (on allignment of Segment::align_c)
        * @throw trie::Exception When there is no memory on 2 possible reasons:
        * \li er_memory_need_compression - manager owns by too small chunks, that can be optimized by block movement
        * \li er_no_memory - there is no enough memory, so new segment must be allocated
        * @param size - byte size to allocate
        * @return memory position
        */
        virtual FarAddress allocate(segment_pos_t size)
        {
            size = OP::utils::align_on(size, SegmentHeader::align_c);

            //try pull existing blocks

            auto [header_pos, user_size] =
                _free_blocks->pull_not_less(size);

            if (header_pos.is_nil())
            { //no available free memory block, ask segment manager to allocate new                    
                auto avail_segments = segment_manager().available_segments(); //ask for number
                segment_manager().ensure_segment(avail_segments); //used number-of-segments as an index so don't need +1
                std::tie(header_pos, user_size) = _free_blocks->pull_not_less(size);
                if (header_pos.is_nil())
                    throw OP::trie::Exception(OP::trie::er_no_memory);
            }
            constexpr segment_pos_t mbh = OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c);
            segment_pos_t deposit = user_size;
            if(user_size > (size + HeapBlockHeader::minimal_space_c))
            { //block can be split onto 2 smaller
                HeapBlockHeader* to_spilt = segment_manager().wr_at<HeapBlockHeader>(header_pos);
                    //alloc new block at the end of current
                segment_pos_t offset = user_size - size - mbh; //offcut
                assert(offset >= HeapBlockHeader::minimal_space_c);
                //make available to alloc
                to_spilt
                    ->size(offset)
                    ->set_free(true)
                    ;
                _free_blocks->insert(header_pos, to_spilt);

                //allocate new block inside this memory but mark this as a free
                header_pos += offset + mbh;//(+mbh) since offset calculated inside user memory

                HeapBlockHeader* result = segment_manager().wr_at<HeapBlockHeader>(
                    header_pos, WritableBlockHint::new_c);

                ::new (result) HeapBlockHeader(std::in_place_t{});

                result
                    ->size(size)
                    ->set_free(false)
                    ;

                deposit = size + mbh;
            }

            std::lock_guard g(_segments_map_lock);
            auto& segment_info = _opened_segments[header_pos.segment()];
            auto heap_acc = segment_manager().wr_at<HeapHeader>(segment_info._heap_start);
            heap_acc->_size -= deposit;
            assert(heap_acc->_size < segment_manager().segment_size() ); //note works with unsigned
            segment_info._size = heap_acc->_size;

            return HeapBlockHeader::get_addr_by_header(header_pos);
        }

        /** @return true if merge two adjacent block during deallocation is allowed */
        virtual bool has_block_merging() const
        {
            return false;
        }

        /**\return number of bytes available for specific segment*/
        segment_pos_t available(segment_idx_t segment_idx) const
        {
            std::lock_guard l(_segments_map_lock);
            auto const& found = _opened_segments[segment_idx]; //should already exists
            assert(!found._heap_start.is_nil());
            return found._size;
        }

        /**
        *   Deallocate memory block previously obtained by #allocate method.
        *
        */
        void deallocate(FarAddress address)
        {
            forcible_deallocate(address);
        }

        void forcible_deallocate(FarAddress address)
        {
            if (!OP::utils::is_aligned(address.offset(), SegmentHeader::align_c)
                //|| !check_pointer(memblock)
                )
                throw trie::Exception(trie::er_invalid_block);

            FarAddress header(HeapBlockHeader::get_header_addr(address));

            auto header_block = segment_manager().accessor<HeapBlockHeader>(header);
            if (!header_block->check_signature() || header_block->is_free())
                throw trie::Exception(trie::er_invalid_block);

            do_deallocate(header_block);
        }

        /**Allocate memory and create object of specified type
        *@return far-offset, to get real pointer use #from_far<T>()
        */
        template<class T, class... Types>
        FarAddress make_new(Types&&... args)
        {
            auto result = allocate(memory_requirement<T>::requirement);
            auto mem = segment_manager().writable_block(
                result, memory_requirement<T>::requirement, WritableBlockHint::new_c);
            new (mem.pos()) T(std::forward<Types>(args)...);
            return result;
        }

        void _check_integrity(FarAddress segment_addr) override
        {
            auto& integrity = integrity::Stream::os();
            IoFlagGuard g(integrity);
            integrity << std::hex << std::boolalpha;
            integrity << "HeapManager at segment: 0x" << segment_addr.segment() << ", offset: 0x" << segment_addr.offset() <<"\n";
            if(segment_addr.segment() == 0 )
            {
                integrity << "\tSkip-list size = 0x" << _free_blocks->byte_size() << "\n";
                segment_addr += _free_blocks->byte_size();
            }
            auto heap_header = segment_manager().view<HeapHeader>(segment_addr);
            integrity << "\tSegment-header:{ _size = 0x" << heap_header->_size 
                << ", _total = 0x"<< heap_header->_total << "}\n";
            segment_addr += OP::utils::aligned_sizeof<HeapHeader>(SegmentHeader::align_c);
            size_t i = 0;
            for(auto offset = segment_addr.offset(); offset < heap_header->_total; ++i)
            {
                FarAddress mbh_addr{segment_addr.segment(), offset};
                integrity << "\tmbh #" << i << " (at: " << mbh_addr << "){";
                auto block_header = segment_manager().view<HeapBlockHeader>(mbh_addr);
                integrity << "free = " << static_cast<bool>(block_header->_free) 
                    << ", signature = 0x" << block_header->_signature 
                    << ", _size = 0x" << block_header->_size 
                    << ", _next = ";
                if( block_header->_next.is_nil() )
                    integrity << "(nil)";
                else
                    integrity << block_header->_next;
                integrity << "}\n";
                if( !block_header->check_signature() )
                    throw std::runtime_error("mbh signature check failed");
                offset += block_header->_size + OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c);
            }
        }

    protected:
        /**
        *   Memory manager always have residence in any segment
        */
        bool has_residence(segment_idx_t segment_idx) const override
        {
            return true;
        }
        /**
        *   @return all available memory after `offset` inside segment
        */
        segment_pos_t byte_size(FarAddress segment_address) const override
        {
            assert(segment_address.offset() < segment_manager().segment_size());
            return segment_manager().segment_size() - segment_address.offset();
        }
        /**
        *   Make initialization of slot in the specified segment as specified offset
        */
        void on_new_segment(FarAddress start_address) override
        {
            FarAddress first_block_pos = start_address;
            if (start_address.segment() == 0)
            {//only single instance of free-space list
                _free_blocks = free_blocks_t::create_new(segment_manager(), first_block_pos);
                first_block_pos += free_blocks_t::byte_size();
            }
            
            std::lock_guard g(_segments_map_lock);
            auto& segment_presence = ensure_index( start_address.segment() );
            segment_presence._heap_start = first_block_pos; //points to HeapHeader
            //Each segment has HeapHeader
            auto heap_header = segment_manager().accessor<HeapHeader>(first_block_pos, WritableBlockHint::new_c);
            first_block_pos += OP::utils::aligned_sizeof<HeapHeader>(SegmentHeader::align_c);
            //make first big memory block for this segment
            auto first_block = segment_manager().accessor<HeapBlockHeader>(first_block_pos, WritableBlockHint::new_c);
            
            segment_presence._size = //free size as the new block without header size
                segment_manager().segment_size() - 
                OP::utils::aligned_sizeof<HeapBlockHeader>(SegmentHeader::align_c) - 
                first_block_pos.offset();
            heap_header->_total = heap_header->_size = segment_presence._size;
            ::new (first_block.pos()) HeapBlockHeader{ segment_presence._size };
            //after allocation block is free, so add this to skiplist
            _free_blocks->insert(first_block.address(), &first_block);
        }

        void open(FarAddress start_address) override
        {
            std::lock_guard g(_segments_map_lock);
            auto& segment_presence = ensure_index(start_address.segment());
            auto blocks_pos = start_address;

            if (start_address.segment() == 0)
            {//only first segment has an instance of free-space list
                _free_blocks = free_blocks_t::open(segment_manager(), blocks_pos);
                blocks_pos += free_blocks_t::byte_size();
            }
            //Each segment has HeapHeader
            auto heap_header = segment_manager().view<HeapHeader>(blocks_pos);

            segment_presence._heap_start = blocks_pos;
            segment_presence._size = heap_header->_size;
        }

        void release_segment(segment_idx_t segment_index) override
        {
            std::lock_guard l(_segments_map_lock);
            _opened_segments[segment_index]._heap_start = {}; //indicate no info
        }

    private:

        /** In each segment this structure allows store number of bytes available */
        struct HeapHeader
        {
            segment_pos_t _total;
            segment_pos_t _size;
        };

        struct SegmentPresenceInfo
        {
            FarAddress _heap_start = {}; //nil indicates segment not opened
            segment_pos_t _size = 0; // just cache of HeapHeader
        };

        using opened_segment_t = std::vector<SegmentPresenceInfo>;
        using free_blocks_t = Log2SkipList<(sizeof(std::uint32_t) << 3)>;

        mutable std::recursive_mutex _segments_map_lock;
        opened_segment_t _opened_segments;
        std::unique_ptr<free_blocks_t> _free_blocks;

    private:

        /**Ensure that _opened_segments contains specific index. Call must be locked vefore with _segments_map_lock*/
        [[nodiscard]] SegmentPresenceInfo& ensure_index(segment_idx_t segment)
        {
            if (segment >= _opened_segments.size())
            {
                _opened_segments.resize(segment + 1);
            }
            return _opened_segments[segment];
        }

        void do_deallocate(WritableAccess<HeapBlockHeader>& block_header)
        {
            //Mark segment and memory for FreeMemoryBlock as available for write
            auto deposit = block_header->size();
            block_header->set_free(true);
            //from header address evaluate address of memory block
            _free_blocks->insert(block_header.address(), &block_header);

            std::lock_guard g(_segments_map_lock);
            auto& segment_info = _opened_segments[block_header.address().segment()];
            auto header_wr = segment_manager().accessor<HeapHeader>(segment_info._heap_start);
            assert(header_wr->_size <= segment_manager().segment_size());
            header_wr->_size += deposit;
            segment_info._size = header_wr->_size;
        }

    };



}//ns: OP:vtm

#endif //_OP_VTM_MEMORYMANAGER__H_
