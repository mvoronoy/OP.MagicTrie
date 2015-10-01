#ifndef _OP_TRIE_MEMORYMANAGER__H_
#define _OP_TRIE_MEMORYMANAGER__H_

#include <unordered_map>

#include <OP/trie/SegmentManager.h>
#include <OP/trie/MemoryBlock.h>

namespace OP
{
    namespace trie
    {
        struct MemoryManager: public Slot
        {
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
            virtual std::uint8_t* allocate(segment_idx_t segment_idx, segment_pos_t size)
            {
                guard_t l(_free_map_lock);
                if (_avail_bytes < to_alloc)
                    throw trie::Exception(trie::er_no_memory);
                auto found = _free_blocks.lower_bound(&MemoryBlockHeader(to_alloc));
                if (found == _free_blocks.end()) //there is free blocks, but compression is needed
                    throw trie::Exception(trie::er_memory_need_compression);
                auto current_pair = *found;
                //before splittng remove block from map
                free_block_erase(found);
                auto new_block = current_pair->split_this(to_alloc);
                if (new_block != current_pair) //new block was allocated
                {
                    free_block_insert(current_pair);
                    _avail_bytes -= new_block->real_size();
                }
                else //when existing block is used it is not needed to use 'real_size' - because header alredy counted
                    _avail_bytes -= new_block->size();
                return unchecked_to_offset(new_block->memory());
            }
            segment_pos_t available(segment_idx_t segment_idx) const
            {
                return 0;// segment->available();
            }
            virtual void deallocate(std::uint8_t* addr)
            {
            }

            /**Allocate memory and create object of specified type
            *@return far-offset, to get real pointer use #from_far<T>()
            */
            template<class T, class... Types>
            T* make_new(segment_idx_t segment_idx, Types&&... args)
            {
                auto result = allocate(segment_idx, sizeof(T));
                return new (result) T(std::forward<Types>(args)...);
            }
            /**Combines together allocate+construct for array*/
            template<class T, class... Types>
            far_pos_t make_array(segment_idx_t segment_idx, segment_pos_t items_count, Types&&... args)
            {
                auto result = allocate(segment_idx, sizeof(T)*items_count);
                auto p = from_far<T>(result);
                auto retval = p;
                //use placement constructor for each item
                for (; items_count; --items_count, ++p)
                    new (p)T(std::forward<Types>(args)...);
                return result;
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
                _segment_manager = &manager;
                segment_pos_t first_block_pos = offset;
                if (segment_idx == 0)
                {//only single instance of free-space list
                    first_block_pos += aligned_sizeof<free_blocks_t>(SegmentHeader::align_c);
                    MemoryRange free_blocks_mem = _segment_manager->writable_block(offset, sizeof(free_blocks_t));
                    _free_bloks = new (free_blocks_mem.pos()) free_blocks_t(FreeMemoryBlockTraits(_segment_manager));
                }
                const segment_pos_t mbh_size_align = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c)
                    + sizeof(FreeMemoryBlock);
                segment_pos_t byte_size = _segment_manager->segment_size() - first_block_pos;
                MemoryRange zero_header = _segment_manager->writable_block(first_block_pos, mbh_size_align);
                MemoryBlockHeader * block = new (zero_header.pos()) MemoryBlockHeader(emplaced_t());
                block
                    ->size(byte_size - mbh_size_align)
                    ->set_free(true)
                    ->set_has_next(false)
                    ;
                //just created block is free, so place FreeMemoryBlock info inside it
                FreeMemoryBlock *span_of_free = new (block->memory()) FreeMemoryBlock(emplaced_t());
                _free_bloks->insert(manager.to_far(segment_idx, span_of_free));
                Description desc;
                guard_t l(_map_lock);
                _opened_segments.emplace(opened_segment_t::value_type(
                    segment_idx,
                    desc.avail_bytes(block->size()).offset(first_block_pos)
                ));
                //
                //MemoryBlockHeader
            }
            void open(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) override
            {
                /*
                _segment_manager = &manager;
                Description desc;
                desc.offset(offset);
                guard_t g(manager);

                //load all free blocks to ordered map
                segment_pos_t fbloff = offset;
                auto mem_limit = base.segment_size - fbloff;
                while (fbloff < mem_limit)
                {
                    MemoryBlockHeader* fbl = at<MemoryBlockHeader>(fbloff);
                    if (!fbl->check_signature())
                        throw trie::Exception(trie::er_invalid_signature, "File corrupted, invalid MemoryBlockHeader signature");
                    fbloff += fbl->real_size();
                    if (!fbl->is_free())
                        continue;
                    desc._avail_bytes += fbl->size();
                    free_block_insert(fbl);
                }
                _opened_segments.emplace(opened_segment_t::value_type(
                    segment_idx, desc
                ));
                */
            }
            void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
            {
                guard_t l(_map_lock);
                _opened_segments.erase(segment_index);
            }
        private:
            typedef std::mutex map_lock_t;
            typedef std::unique_lock<map_lock_t> guard_t;

            struct Description
            {
                Description():
                    _offset(0), _avail_bytes(0)
                {
                }
                Description& offset(segment_pos_t v)
                {
                    _offset = v;
                    return *this;
                }
                Description& avail_bytes(segment_pos_t v)
                {
                    _avail_bytes = v;
                    return *this;
                }
                segment_pos_t _offset;
                segment_pos_t _avail_bytes;
            };
            typedef std::unordered_map<segment_idx_t, Description> opened_segment_t;
            map_lock_t _map_lock;
            opened_segment_t _opened_segments;
            typedef Log2SkipList<far_pos_t, FreeMemoryBlockTraits> free_blocks_t;
            free_blocks_t *_free_bloks;
            SegmentManager* _segment_manager;
        };


    }//namespace trie
}//namespace OP
#endif //_OP_TRIE_MEMORYMANAGER__H_