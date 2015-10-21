#ifndef _OP_TRIE_MEMORYMANAGER__H_
#define _OP_TRIE_MEMORYMANAGER__H_

#include <unordered_map>

#include <OP/trie/SegmentManager.h>
#include <OP/trie/MemoryBlock.h>
#include <op/trie/Skplst.h>

namespace OP
{
    namespace trie
    {
        struct MemoryManager : public Slot
        {
            enum
            {
                /**set if memory management allows merging of free adjacent blocks*/
                merge_free_blocks_c = false
            };
            MemoryManager()
            {
            }
            /**
            * Allocate memory block without initialization that can accomodate sizeof(T)*count bytes. As usual result block little bigger (on allignment of Segment::align_c)
            * @throw trie::Exception When there is no memory on 2 possible reasons:
            * \li er_memory_need_compression - manager owns by too small chunks, that can be optimized by block movement
            * \li er_no_memory - there is no enough memory, so new segment must be allocated
            * @param size - byte size to allocate
            */
            virtual std::uint8_t* allocate(segment_pos_t size)
            {
                FreeMemoryBlockTraits free_traits(_segment_manager);
                //try do without locking
                
                far_pos_t free_block_pos = _free_blocks->pull_not_less(free_traits, size);
                if (free_traits.is_eos(free_block_pos))
                { //need lock - because may need allocate new segment
                    guard_t l(_segments_map_lock);
                    //check again
                    free_block_pos = _free_blocks->pull_not_less(free_traits, size);
                    if (free_traits.is_eos(free_block_pos))
                    {
                        auto avail_segments = _segment_manager->available_segments();
                        _segment_manager->ensure_segment(avail_segments);
                        free_block_pos = _free_blocks->pull_not_less(free_traits, size);
                        if (free_traits.is_eos(free_block_pos))
                            throw OP::trie::Exception(OP::trie::er_no_memory);
                    }
                }
                segment_idx_t segment_idx = segment_of_far(free_block_pos); 
                //auto current_free = _segment_manager->writable_block(free_block_pos, sizeof(FreeMemoryBlock)).at<FreeMemoryBlock>(0);
                auto current_mem_ptr = FarPosHolder(FreeMemoryBlock::get_header_addr(free_block_pos));
                auto current_mem =
                    _segment_manager->wr_at<MemoryBlockHeader>(current_mem_ptr);

                auto new_block = current_mem->split_this(*_segment_manager, current_mem_ptr, size);
                segment_pos_t deposited_size = 0;
                if (new_block != current_mem) //new block was allocated
                {  //old block was splitted, so place the rest back to free list
                    FreeMemoryBlock *free_block = _segment_manager->wr_at< FreeMemoryBlock >(FarPosHolder(free_block_pos));
                    deposited_size = new_block->real_size();
                    _free_blocks->insert(free_traits, free_block_pos, free_block);
                }
                else //when existing block is used it is not needed to use 'real_size' - because header alredy counted
                {
                    deposited_size = new_block->size();
                }
                get_available_bytes(segment_idx) -= deposited_size;
                return new_block->memory();
            }
            /**\return number of bytes available for specific segment*/
            segment_pos_t available(segment_idx_t segment_idx) const
            {
                guard_t l(_segments_map_lock);
                auto found = _opened_segments.find(segment_idx);
                assert(found != _opened_segments.end()); //should already exists
                FarPosHolder pos(segment_idx, (*found).second.avail_bytes_offset());
                //no need in lock anymore
                l.unlock();
                return *_segment_manager->ro_at<segment_pos_t>(pos);
            }
            virtual void deallocate(std::uint8_t* addr)
            {
                if (!is_aligned(addr, SegmentHeader::align_c)
                    //|| !check_pointer(memblock)
                    )
                    throw trie::Exception(trie::er_invalid_block);
                std::uint8_t* pointer = reinterpret_cast<std::uint8_t*>(addr);
                MemoryBlockHeader* header = reinterpret_cast<MemoryBlockHeader*>(
                    pointer - aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c));
                if (!header->check_signature() || header->is_free())
                    throw trie::Exception(trie::er_invalid_block);
                //retrieve file pos
                auto segment_idx = header->nav._my_segment;
                FarPosHolder header_far (_segment_manager->to_far(segment_idx, header));
                OP_CONSTEXPR(const) segment_pos_t mbh = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);

                auto free_block_pos = header_far+mbh;

                //Mark segment and memory for FreeMemoryBlock as available for write
                auto block = _segment_manager->writable_block(std::move(header_far), mbh + sizeof(FreeMemoryBlock));
                header = block.at<MemoryBlockHeader>(0);
                header->set_free(true);
                auto free_marker = block.at<void>(mbh);
                FreeMemoryBlock *span_of_free = new (free_marker) FreeMemoryBlock(emplaced_t());
                auto deposited = header->size();
                _free_blocks->insert(FreeMemoryBlockTraits(_segment_manager), free_block_pos, span_of_free);
                get_available_bytes(segment_idx) += deposited;
            }

            /**Allocate memory and create object of specified type
            *@return far-offset, to get real pointer use #from_far<T>()
            */
            template<class T, class... Types>
            T* make_new(Types&&... args)
            {
                auto result = allocate(sizeof(T));
                return new (result)T(std::forward<Types>(args)...);
            }
            /**Combines together allocate+construct for array*/
            template<class T, class... Types>
            far_pos_t make_array(segment_pos_t items_count, Types&&... args)
            {
                auto result = allocate(sizeof(T)*items_count);
                auto p = from_far<T>(result);
                auto retval = p;
                //use placement constructor for each item
                for (; items_count; --items_count, ++p)
                    new (p)T(std::forward<Types>(args)...);
                return result;
            }
            void _check_integrity(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset)
            {
                FarPosHolder first_block_pos (segment_idx, offset);
                if (segment_idx == 0)
                {//only single instance of free-space list
                    first_block_pos += free_blocks_t::byte_size();
                    //for zero segment check also table of free blocks
                    //@tbd
                }
                first_block_pos.offset = align_on(first_block_pos.offset, SegmentDef::align_c);
                //skip reserved 4bytes for segment size
                FarPosHolder avail_bytes_offset = first_block_pos;

                auto avail_space_mem = manager.readonly_block(avail_bytes_offset, sizeof(segment_pos_t));
                first_block_pos.offset = align_on(first_block_pos.offset +static_cast<segment_pos_t>(sizeof(segment_pos_t)), SegmentHeader::align_c);

                auto first = manager.ro_at<MemoryBlockHeader>(first_block_pos);
                size_t avail = 0, occupied = 0;
                size_t free_block_count = 0;
                
                const MemoryBlockHeader *prev = nullptr;
                for (;;)
                {
                    assert(first->prev_block_offset() < 0);
                    if (SegmentDef::eos_c != first->prev_block_offset())
                    {
                        auto prev_pos = first_block_pos + first->prev_block_offset();
                        auto check_prev = manager.ro_at<MemoryBlockHeader>(prev_pos);
                        assert(prev == check_prev);
                        assert(prev->my_segement() == first->my_segement());
                    }

                    prev = first;
                    first->_check_integrity();
                    if (first->is_free())
                    {
                        avail += first->size();
                        //assert(_free_blocks.end() != find_by_ptr(first));
                        free_block_count++;
                    }
                    else
                    {
                        occupied += first->size();
                        //assert(_free_blocks.end() == find_by_ptr(first));
                    }
                    if (first->has_next())
                    {
                        first_block_pos += first->real_size();
                        first = manager.ro_at<MemoryBlockHeader>(first_block_pos);
                    }
                    else break;
                }
                assert(avail == *avail_space_mem.at<segment_pos_t>(0));
                //assert(free_block_count == _free_blocks.size());
                //occupied???
            }
        protected:
            /**
            *   Memory manager always have residence in any segment
            */
            bool has_residence(segment_idx_t segment_idx, SegmentManager& manager) const override
            {
                return true;
            }
            /**
            *   @return all available memory after `offset` inside segment
            */
            segment_pos_t byte_size(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) const override
            {
                assert(offset < manager.segment_size());
                return manager.segment_size() - offset;
            }
            /**
            *   Make initialization of slot in the specified segment as specified offset
            */
            void emplace_slot_to_segment(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) override
            {
                segment_operations_guard_t op_g(&manager); //invoke begin/end write-op
                _segment_manager = &manager;
                FarPosHolder first_block_pos (segment_idx, offset);
                if (segment_idx == 0)
                {//only single instance of free-space list
                    _free_blocks = free_blocks_t::create_new(manager, first_block_pos);
                    first_block_pos.offset += free_blocks_t::byte_size();
                }
                first_block_pos.offset = align_on(first_block_pos.offset, SegmentDef::align_c);
                //reserve 4bytes for segment size
                FarPosHolder avail_bytes_offset = first_block_pos;
                MemoryRange avail_space_mem = _segment_manager->writable_block(avail_bytes_offset, sizeof(segment_pos_t), WritableBlockHint::new_c);
                first_block_pos.offset = align_on(first_block_pos.offset +static_cast<segment_pos_t>(sizeof(segment_pos_t)), SegmentHeader::align_c);

                segment_pos_t byte_size = ceil_align_on(_segment_manager->segment_size() - first_block_pos.offset, SegmentHeader::align_c);
                OP_CONSTEXPR(const) segment_pos_t mbh_size_align = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                MemoryRange zero_header = _segment_manager->writable_block(first_block_pos, mbh_size_align + sizeof(FreeMemoryBlock), WritableBlockHint::new_c);
                MemoryBlockHeader * block = new (zero_header.pos()) MemoryBlockHeader(emplaced_t());
                block
                    ->size(byte_size - mbh_size_align)
                    ->set_free(true)
                    ->set_has_next(false)
                    ->my_segement(segment_idx)
                    ;
                //just created block is free, so place FreeMemoryBlock info inside it
                FreeMemoryBlock *span_of_free = new (block->memory()) FreeMemoryBlock(emplaced_t());
                
                _free_blocks->insert(
                    FreeMemoryBlockTraits(_segment_manager), 
                    _segment_manager->to_far(segment_idx, span_of_free),
                    span_of_free);
                *avail_space_mem.at<segment_pos_t>(0) = block->size();
                
                Description desc;
                guard_t l(_segments_map_lock);
                auto insres = _opened_segments.emplace( std::piecewise_construct,
                     std::forward_as_tuple(segment_idx),
                     std::forward_as_tuple()
                );
                assert(insres.second); //should not exists such segment yet
                (*insres.first).second.avail_bytes_offset(avail_bytes_offset.offset);
                 
                //
                //MemoryBlockHeader
            }
            void open(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) override
            {
                segment_operations_guard_t op_g(&manager); //invoke begin/end write-op
                _segment_manager = &manager;
                segment_pos_t avail_bytes_offset = offset;
                
                if (segment_idx == 0)
                {//only single instance of free-space list
                    auto free_blocks_pos = FarPosHolder(segment_idx, offset);
                    //auto first_block_pos = offset + aligned_sizeof<free_blocks_t>(SegmentHeader::align_c);
                    _free_blocks = free_blocks_t::open(manager, free_blocks_pos);
                    avail_bytes_offset += free_blocks_t::byte_size();
                }
                avail_bytes_offset = align_on(avail_bytes_offset, SegmentDef::align_c);
                guard_t l(_segments_map_lock);
                auto insres = _opened_segments.emplace( std::piecewise_construct,
                     std::forward_as_tuple(segment_idx),
                     std::forward_as_tuple()
                );
                assert(insres.second); //should not exists such segment yet
                (*insres.first).second.avail_bytes_offset(avail_bytes_offset);
                
            }
            void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
            {
                guard_t l(_segments_map_lock);
                _opened_segments.erase(segment_index);
            }
        private:
            typedef std::recursive_mutex map_lock_t;
            typedef std::unique_lock<map_lock_t> guard_t;

            struct Description
            {
                Description():
                    _avail_bytes_offset(0)
                {
                }
                Description(const Description&) = delete;
                
                /**Offset where 'available-bytes' variable begins*/
                Description& avail_bytes_offset(segment_pos_t v)
                {
                    _avail_bytes_offset = v;
                    return *this;
                }
                segment_pos_t avail_bytes_offset() const
                {
                    return _avail_bytes_offset;
                }
            private:
                segment_pos_t _avail_bytes_offset;
            };
            typedef std::unordered_map<segment_idx_t, Description> opened_segment_t;
            mutable map_lock_t _segments_map_lock;
            opened_segment_t _opened_segments;
            typedef Log2SkipList<FreeMemoryBlockTraits> free_blocks_t;

            std::unique_ptr<free_blocks_t> _free_blocks;
            SegmentManager* _segment_manager;
        private:
            /**Return modifyable reference to variable that keep available memory in segment */
            segment_pos_t& get_available_bytes(segment_idx_t segment)
            {
                guard_t l(_segments_map_lock);
                auto found = _opened_segments.find( segment );
                assert(found != _opened_segments.end()); //should already exists
                FarPosHolder pos(segment, (*found).second.avail_bytes_offset());
                //no need in lock anymore
                l.unlock();
                return *_segment_manager->wr_at<segment_pos_t>(pos);
            }
            /**Merge together 2 adjacent memory blocks
            * @param merge_with_right block that may have 
            */
            FreeMemoryBlock* merge_free_blocks(far_pos_t merge_with_right)
            {
                
            }
        };


    }//namespace trie
}//namespace OP
#endif //_OP_TRIE_MEMORYMANAGER__H_