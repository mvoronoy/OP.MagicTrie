#ifndef _OP_TRIE_MEMORYMANAGER__H_
#define _OP_TRIE_MEMORYMANAGER__H_

#include <unordered_map>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryBlock.h>
#include <op/vtm/Skplst.h>

namespace OP
{
    namespace trie
    {
        struct HeapManagerSlot : public Slot
        {
            HeapManagerSlot()
            {
            }
            /**
            * Allocate memory block without initialization that can accomodate sizeof(T)*count bytes. As usual result block little bigger (on allignment of Segment::align_c)
            * @throw trie::Exception When there is no memory on 2 possible reasons:
            * \li er_memory_need_compression - manager owns by too small chunks, that can be optimized by block movement
            * \li er_no_memory - there is no enough memory, so new segment must be allocated
            * @param size - byte size to allocate
            * @return memory position
            */
            virtual FarAddress allocate(segment_pos_t size)
            {
                size = OP::utils::align_on(size, SegmentHeader::align_c);

                FreeMemoryBlockTraits free_traits(_segment_manager);
                OP::vtm::TransactionGuard g(_segment_manager->begin_transaction());
                //try do without locking
                
                far_pos_t free_block_pos = OP::vtm::transactional_yield_retry_n<10>([this](
                    FreeMemoryBlockTraits& free_traits, segment_pos_t size){
                        return _free_blocks->pull_not_less(free_traits, size);
                    }, free_traits, size);
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
                
                auto current_mem_pos = FarAddress(FreeMemoryBlock::get_header_addr(free_block_pos));
                auto current_mem_block = _segment_manager->writable_block(current_mem_pos, sizeof(MemoryBlockHeader));
                auto current_mem = current_mem_block.at<MemoryBlockHeader>(0);
                
                OP_CONSTEXPR(const) segment_pos_t mbh = 
                    OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                auto free_addr = FarAddress(free_block_pos);
                segment_pos_t deposited_size = current_mem->real_size();
                FarAddress retval;
                if (current_mem->size() <= (size + mbh + SegmentHeader::align_c)) //existing block can fit only queried bytes
                {
                    current_mem->set_free(false);
                    //when existing block is used it is not needed to use 'real_size' - because header alredy counted
                    deposited_size -= mbh;
                    MemoryChunk result = 
                        _segment_manager->writable_block(free_addr, size, WritableBlockHint::new_c | WritableBlockHint::allow_block_realloc);
                    retval = result.address();
                }
                else
                { //existing block too big, so place some memory back
                    retval = current_mem->split_this(*_segment_manager, current_mem_pos, size);
                    
                    //old block was splitted, so place the rest back to free list
                    FreeMemoryBlock *free_block = _segment_manager->wr_at< FreeMemoryBlock >(free_addr);
                    
                    _free_blocks->insert(free_traits, free_block_pos, free_block, current_mem);
                    deposited_size -= current_mem->real_size();
                }
                
                get_available_bytes(segment_idx) -= deposited_size;
                /*If transaction started inside this method then after commit 
                memory will be changed. */
                g.commit();
                return retval;
            }
            /** @return true if merge two adjacent block during deallocation is allowed */
            virtual bool has_block_merging() const
            {
                return false;
            }
            /**\return number of bytes available for specific segment*/
            segment_pos_t available(segment_idx_t segment_idx) const
            {
                guard_t l(_segments_map_lock);
                auto const& found = _opened_segments[segment_idx]; //should already exists
                assert(!found.is_null()); 
                FarAddress pos(segment_idx, found.avail_bytes_offset());
                //no need in lock anymore
                l.unlock();
                auto ro_block = _segment_manager->readonly_block(pos, sizeof(segment_pos_t));
                return *ro_block.at<segment_pos_t>(0);
            }
            /**
            *   Deallocate memory block previously obtained by #allocate method.
            *  
            */
            void deallocate(FarAddress address)
            {
                if (!OP::utils::is_aligned(address.offset, SegmentHeader::align_c)
                    //|| !check_pointer(memblock)
                    )
                    throw trie::Exception(trie::er_invalid_block);
                OP_CONSTEXPR(const) segment_pos_t mbh = 
                    OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                FarAddress header_far (FreeMemoryBlock::get_header_addr(address));
                
                OP::vtm::transaction_ptr_t tr;
                OP::vtm::TransactionGuard g(tr = _segment_manager->begin_transaction());
                if (tr.get() != nullptr)
                {
                    //obtain RO block of memory header to validate address
                    auto ro_block =
                        _segment_manager->readonly_block(header_far, mbh,
                            OP::trie::ReadonlyBlockHint::ro_keep_lock/*retain this block's header in scope of transaction*/);
                    const MemoryBlockHeader* header = ro_block.at<MemoryBlockHeader>(0);
                    if (!header->check_signature() || header->is_free())
                        throw trie::Exception(trie::er_invalid_block);
                    //postpone deletion untill transaction end
                    tr->register_handle(std::make_unique<PostponDeallocToTransactionEnd>(this, std::move(ro_block)));
                }
                else //non-transacted 
                {
                    auto wr_block = _segment_manager->writable_block(header_far, mbh);
                    MemoryBlockHeader* header = wr_block.at<MemoryBlockHeader>(0);
                    if (!header->check_signature() || header->is_free())
                        throw trie::Exception(trie::er_invalid_block);
                    do_deallocate(wr_block);
                }
                g.commit();
            }
            
            void forcible_deallocate(FarAddress address)
            {
                if (!OP::utils::is_aligned(address.offset, SegmentHeader::align_c)
                    //|| !check_pointer(memblock)
                    )
                    throw trie::Exception(trie::er_invalid_block);

                OP_CONSTEXPR(const) segment_pos_t mbh = 
                    OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                FarAddress header_far (FreeMemoryBlock::get_header_addr(address));
                OP::vtm::TransactionGuard g(_segment_manager->begin_transaction());

                //need separate 2 wr_at to avoid overlapped block exception
                auto header_block = _segment_manager->writable_block(header_far, mbh);
                MemoryBlockHeader* header = _segment_manager->wr_at<MemoryBlockHeader>(header_far);
                if (!header->check_signature() || header->is_free())
                    throw trie::Exception(trie::er_invalid_block);

                do_deallocate(header_block);
                g.commit();
            }

            /**Allocate memory and create object of specified type
            *@return far-offset, to get real pointer use #from_far<T>()
            */
            template<class T, class... Types>
            FarAddress make_new(Types&&... args)
            {
                OP::vtm::TransactionGuard g(_segment_manager->begin_transaction());
                auto result = allocate(sizeof(T));
                auto mem = this->_segment_manager->writable_block(result, sizeof(T), WritableBlockHint::new_c);
                new (mem.pos()) T(std::forward<Types>(args)...);
                g.commit();
                return result;
            }
            /**Combines together allocate+construct for array*/
            template<class T, class... Types>
            FarAddress make_array(segment_pos_t items_count, Types&&... args)
            {
                OP::vtm::TransactionGuard g(_segment_manager->begin_transaction());
                const auto mem_req = memory_requirement<T>::requirement * items_count;
                auto result = allocate(mem_req);
                auto mem_block = this->_segment_manager->writable_block(result, 
                    mem_req, WritableBlockHint::new_c);
                //use placement constructor for each item
                for (auto p = mem_block.at<memory_requirement<T>::type>(0); items_count; --items_count, ++p)
                    new (p) T(std::forward<Types>(args)...);
                g.commit();
                return result;
            }
            void _check_integrity(FarAddress segment_addr, SegmentManager& manager)
            {
                FarAddress first_block_pos = segment_addr;
                if (segment_addr.segment == 0)
                {//only single instance of free-space list
                    first_block_pos += free_blocks_t::byte_size();
                    //for zero segment check also table of free blocks
                    //@tbd
                }
                first_block_pos.offset = 
                    OP::utils::align_on(first_block_pos.offset, SegmentDef::align_c);
                //skip reserved 4bytes for segment size
                FarAddress avail_bytes_offset = first_block_pos;

                auto avail_space_mem = manager.readonly_block(avail_bytes_offset, sizeof(segment_pos_t));
                first_block_pos.offset = 
                    OP::utils::align_on(first_block_pos.offset +static_cast<segment_pos_t>(sizeof(segment_pos_t)), SegmentHeader::align_c);
                auto first_ro_block = manager.readonly_block(first_block_pos, sizeof(MemoryBlockHeader));
                const MemoryBlockHeader* first = first_ro_block.at<MemoryBlockHeader>(0);
                size_t avail = 0, occupied = 0;
                size_t free_block_count = 0;
                
                const MemoryBlockHeader *prev = nullptr;
                for (;;)
                {
                    assert(first->prev_block_offset() < 0);
                    if (SegmentDef::eos_c != first->prev_block_offset())
                    {
                        auto prev_pos = first_block_pos + first->prev_block_offset();
                        auto check_prev_block = manager.readonly_block(prev_pos, sizeof(MemoryBlockHeader));
                        auto check_prev = check_prev_block.at<MemoryBlockHeader>(0);
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
                        first_ro_block = manager.readonly_block(first_block_pos, sizeof(MemoryBlockHeader));
                        first = first_ro_block.at<MemoryBlockHeader>(0);
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
            segment_pos_t byte_size(FarAddress segment_address, SegmentManager& manager) const override
            {
                assert(segment_address.offset < manager.segment_size());
                return manager.segment_size() - segment_address.offset;
            }
            /**
            *   Make initialization of slot in the specified segment as specified offset
            */
            void on_new_segment(FarAddress start_address, SegmentManager& manager) override
            {
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                _segment_manager = &manager;
                FarAddress first_block_pos (start_address);
                if (start_address.segment == 0)
                {//only single instance of free-space list
                    _free_blocks = free_blocks_t::create_new(manager, first_block_pos);
                    first_block_pos.offset += free_blocks_t::byte_size();
                }
                first_block_pos.offset = 
                    OP::utils::align_on(first_block_pos.offset, SegmentDef::align_c);
                //reserve 4bytes for segment size
                FarAddress avail_bytes_offset = first_block_pos;
                auto avail_space_mem = accessor<segment_pos_t>(*_segment_manager, avail_bytes_offset, WritableBlockHint::new_c);
                first_block_pos.offset = 
                    OP::utils::align_on(first_block_pos.offset +static_cast<segment_pos_t>(sizeof(segment_pos_t)), SegmentHeader::align_c);

                segment_pos_t byte_size = 
                    OP::utils::ceil_align_on(_segment_manager->segment_size() - first_block_pos.offset, SegmentHeader::align_c);
                OP_CONSTEXPR(const) segment_pos_t mbh_size_align = 
                    OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                MemoryChunk zero_header = _segment_manager->writable_block(first_block_pos, mbh_size_align + sizeof(FreeMemoryBlock), WritableBlockHint::new_c);
                MemoryBlockHeader * block = new (zero_header.pos()) MemoryBlockHeader(emplaced_t());
                block
                    ->size(byte_size - mbh_size_align)
                    ->set_free(true)
                    ->set_has_next(false)
                    ->my_segement(start_address.segment)
                    ;
                //just created block is free, so place FreeMemoryBlock info inside it
                FreeMemoryBlock *span_of_free = new (block->memory()) FreeMemoryBlock(emplaced_t());
                FarAddress user_memory_pos = zero_header.address() + mbh_size_align;
                
                _free_blocks->insert(
                    FreeMemoryBlockTraits(_segment_manager), 
                    user_memory_pos,
                    span_of_free, block);
                *avail_space_mem = block->size();
                
                Description desc;
                guard_t l(_segments_map_lock);
                ensure_index(start_address.segment);
                auto &entry = _opened_segments[start_address.segment];
                assert(entry.is_null()); //should not exists such segment yet
                entry.avail_bytes_offset(avail_bytes_offset.offset);
                 
                op_g.commit();
            }
            void open(FarAddress start_address, SegmentManager& manager) override
            {
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                _segment_manager = &manager;
                segment_pos_t avail_bytes_offset = start_address.offset;
                
                if (start_address.segment == 0)
                {//only single instance of free-space list
                    auto free_blocks_pos = start_address;
                    //auto first_block_pos = offset + aligned_sizeof<free_blocks_t>(SegmentHeader::align_c);
                    _free_blocks = free_blocks_t::open(manager, free_blocks_pos);
                    avail_bytes_offset += free_blocks_t::byte_size();
                }
                avail_bytes_offset = 
                    OP::utils::align_on(avail_bytes_offset, SegmentDef::align_c);
                guard_t l(_segments_map_lock);
                ensure_index(start_address.segment);
                auto &entry = _opened_segments[start_address.segment];
                assert(entry.is_null()); //should not exists such segment yet
                entry.avail_bytes_offset(avail_bytes_offset);
                op_g.commit();
            }
            void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
            {
                guard_t l(_segments_map_lock);
                _opened_segments[segment_index].erase();
            }
        private:
            typedef std::recursive_mutex map_lock_t;
            typedef std::unique_lock<map_lock_t> guard_t;

            struct Description
            {
                Description()
                    : _avail_bytes_offset(SegmentDef::eos_c)
                {
                }
                Description(const Description&) = delete;
                Description(Description && other)
                    : _avail_bytes_offset(other._avail_bytes_offset)
                {}
                
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
                bool is_null() const
                {
                    return _avail_bytes_offset == SegmentDef::eos_c;
                }
                void erase()
                {
                    _avail_bytes_offset = SegmentDef::eos_c;
                }
            private:
                segment_pos_t _avail_bytes_offset;
            };
            //?->typedef std::unordered_map<segment_idx_t, Description> opened_segment_t;
            mutable map_lock_t _segments_map_lock;
            typedef std::vector<Description> opened_segment_t;
            opened_segment_t _opened_segments;
            typedef Log2SkipList<FreeMemoryBlockTraits> free_blocks_t;

            std::unique_ptr<free_blocks_t> _free_blocks;
            SegmentManager* _segment_manager = nullptr;
        private:
            /**Return modifyable reference to variable that keep available memory in segment */
            segment_pos_t& get_available_bytes(segment_idx_t segment)
            {
                guard_t l(_segments_map_lock);
                auto& found = _opened_segments[segment];
                assert(!found.is_null()); //should already exists
                FarAddress pos(segment, found.avail_bytes_offset());
                //no need in lock anymore
                l.unlock();
                return *accessor<segment_pos_t>(*_segment_manager, pos);
            }
            /**Ensure that _opened_segments contains specific index. Must ve called ubder lock _segments_map_lock*/
            void ensure_index(segment_idx_t segment)
            {
                if (segment < _opened_segments.size())
                    return;
                _opened_segments.resize(segment + 1);
            }
            
            void do_deallocate(MemoryChunk& header_block)
            {
                //Mark segment and memory for FreeMemoryBlock as available for write
                auto header = header_block.at<MemoryBlockHeader>(0);
                assert(!header->is_free());
                header->set_free(true);
                //from header address evaluate address of memory block
                FarAddress address (FreeMemoryBlock::get_addr_by_header(header_block.address())); 
                auto free_marker = _segment_manager->wr_at<FreeMemoryBlock>(address);

                FreeMemoryBlock *span_of_free = new (free_marker) FreeMemoryBlock(emplaced_t());
                auto deposited = header->size();
                
                _free_blocks->insert(FreeMemoryBlockTraits(_segment_manager), address, span_of_free, header);
                get_available_bytes(address.segment) += deposited;
            }
            /**Postpon delete of memory block until Transaction commit,*/
            struct PostponDeallocToTransactionEnd : public OP::vtm::BeforeTransactionEnd
            {
                PostponDeallocToTransactionEnd(HeapManagerSlot * memory_manager, ReadonlyMemoryChunk memory_block)
                    : _memory_manager(memory_manager)
                    , _memory_block(std::move(memory_block))
                {

                }
                void on_commit() override
                {
                    auto header = _memory_manager->_segment_manager->upgrade_to_writable_block(_memory_block);
                    _memory_manager->do_deallocate(header);
                }
                void on_rollback() override
                {

                }
            private:
                HeapManagerSlot * _memory_manager;
                ReadonlyMemoryChunk _memory_block;
            };
        };


    }//namespace trie
}//namespace OP
#endif //_OP_TRIE_MEMORYMANAGER__H_