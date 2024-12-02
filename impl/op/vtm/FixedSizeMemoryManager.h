#pragma once

#ifndef _OP_VTM_FIXEDSIZEMEMORYMANAGER__H_
#define _OP_VTM_FIXEDSIZEMEMORYMANAGER__H_

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <fstream>
#include <op/trie/Containers.h>
#include <op/vtm/SegmentManager.h>
#include <op/common/Range.h>
namespace OP::vtm
{
    using namespace OP::utils;
    /**
    @tparam Capacity number of #Payload entries in this container
    */
    template <class Payload, std::uint32_t Capacity>
    struct FixedSizeMemoryManager : public Slot
    {
        static_assert(Capacity > 1, "Capacity template argument must be greater than 0");

        typedef Payload payload_t;
        typedef FixedSizeMemoryManager<Payload, Capacity> this_t;
        /**
        *   \tparam Args - optional argument of Payload constructor.
        */
        template <class ... Args>
        FarAddress allocate(Args&& ...args)
        {
            FarAddress result;
            allocate_n(&result, 1,
                [&](size_t, auto* raw)
                {
                    return new(raw) payload_t(std::forward<Args>(args)...);
                }
            );
            return result;
        }

        /**
        * \param n - number of items to allocate. 0 is allowed but nothing is allocated
        */
        template <class FConstr>
        void allocate_n(FarAddress* out_allocs, size_t n, FConstr constr)
        {
            if (n < 1)
                return;
            OP::vtm::TransactionGuard op_g(_segment_manager->begin_transaction()); //invoke begin/end write-op
            auto avail_segments = _segment_manager->available_segments();
            //capture ZeroHeader for write during 10 tries
            ZeroHeader* header = OP::vtm::template transactional_yield_retry_n<60>([this]()
                {
                    return _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                });
            size_t i = 0;
            for (FarAddress* result = out_allocs; n; --n, ++result, ++i)
            {
                if (header->_next == SegmentDef::far_null_c)
                {  //need allocate new segment
                    _segment_manager->ensure_segment(avail_segments);
                }
                //just to ensure last version of block. No locks required - since previous WR already captured
                header = _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                //`writable_block` used instead of `wr_at` to capture full block to improve transaction speed
                auto void_block = _segment_manager->writable_block(
                    FarAddress(header->_next), entry_size_c, WritableBlockHint::block_for_write_c);

                auto* block = void_block.at<FreeBlockHeader>(0);

                if (block->_adjacent_count > 0)
                {//there are adjacent blocks more than one, so don't care about following list of other
                    //return last available entry 
                    result->address =
                        header->_next //using var `header` is correct there
                        + entry_size_c * block->_adjacent_count;
                    --block->_adjacent_count;
                }
                else
                {//only one entry left, so need rebuild further list
                    result->address = header->_next; //return exactly `block` address
                    header->_next = block->_next;
                    //@@@@!!! Must research multithread env
                    ////be proactive in predicting new segment allocation but only for single addr requested
                    //if (n == 1 && block->_next == SegmentDef::far_null_c)
                    //{
                    //    //`n == 1` used to ensure no thread-waiting mechanic needed
                    //    _segment_manager->thread_pool().one_way(
                    //        [](SegmentManager* sm, segment_idx_t avail) {
                    //            sm->ensure_segment(avail);
                    //        },
                    //        _segment_manager, avail_segments);
                    //}
                }
                constr(i, _segment_manager->wr_at<payload_t>(*result));
                --header->_in_free;
                ++header->_in_alloc;
            }
            op_g.commit();
        }
        bool is_valid_address(FarAddress addr)
        {
            //@! todo: validate that addr belong to FixedSizeMemoryManager
            return true;
        }
        void deallocate(FarAddress addr)
        {
            if( !is_valid_address(addr) )
            {
                using namespace std::string_literals;
                throw std::runtime_error("Address doesn't belong to "s + typeid(*this).name());
            }
            OP::vtm::TransactionGuard op_g(_segment_manager->begin_transaction()); //invoke begin/end write-op
            //capture ZeroHeader for write during 10 tries
            auto header = OP::vtm::template transactional_yield_retry_n<10>([this]()
                {
                    return _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                });

            //following will raise ConcurrentLockException immediately, if 'addr' cannot be locked
            auto entry = _segment_manager->writable_block(
                addr, entry_size_c, WritableBlockHint::block_for_write_c);

            payload_t* to_free = entry.template at<payload_t>(0);
            to_free->~payload_t();

            auto just_freed = entry.template at<FreeBlockHeader>(0);
            *just_freed = { header->_next, 0 };
            header->_next = addr;
            ++header->_in_free;
            --header->_in_alloc;
            op_g.commit();
        }

        void _check_integrity(FarAddress segment_addr, SegmentManager& manager) override
        {
            if (segment_addr.segment() != 0)
                return;
            std::lock_guard guard(_topology_mutex);
            //check from _zero_header_address
            auto header =
                manager.readonly_block(
                    _zero_header_address, memory_requirement<ZeroHeader>::requirement)
                    .template at<ZeroHeader>(0);
            FarAddress block_addr = FarAddress{ header->_next };
            size_t n_free_blocks = 0;
            //count all free blocks
            while (block_addr != SegmentDef::far_null_c)
            {
                const FreeBlockHeader* mem_block =
                    manager.readonly_block(
                        block_addr, entry_size_c)
                    .template at<FreeBlockHeader>(0);
                if ((mem_block->_adjacent_count + 1) > Capacity)
                {
                    std::ostringstream error;
                    error << typeid(this).name() << " detected block at:0x{"
                        << block_addr
                        << "} with invalid adjacent number="
                        << mem_block->_adjacent_count;
                    throw std::runtime_error(error.str());
                }

                n_free_blocks += (1 + mem_block->_adjacent_count);
                block_addr =
                    FarAddress(mem_block->_next);
            }
            if (manager.available_segments() * Capacity < n_free_blocks)
            {
                std::ostringstream error;
                error << typeid(this).name()
                    << " total number of free blocks {"
                    << n_free_blocks
                    << "} exceeds Capacity in all segments";
            }
        }
        /**
        * @return pair where first indicate total number of busy elements and second
        *   is about number of free (available to allocate) elements.
        */
        std::pair<size_t, size_t> usage_info() const
        {
            //check from _zero_header_address
            auto header =
                _segment_manager->readonly_block(
                    _zero_header_address, memory_requirement<ZeroHeader>::requirement)
                    .template at<ZeroHeader>(0);
            return std::make_pair(header->_in_alloc, header->_in_free);
        }

    private:
        /**Structure specific for 0 segment only*/
        struct ZeroHeader
        {
            explicit ZeroHeader(far_pos_t next) noexcept
                : _next(next)
            {
            }
            /**Head of forward-only linked list of free (unallocated) blocks,
            * if no free blocks left then fields contains #SegmentDef::far_null_c.
            * This value can point not only single segment.
            */
            far_pos_t _next;
            std::uint32_t _in_free = Capacity, _in_alloc = 0;
        };

        struct FreeBlockHeader
        {
            constexpr FreeBlockHeader(far_pos_t next, std::uint32_t adjacent_count) noexcept
                : _next{ next }
                , _adjacent_count{ adjacent_count }
            {
            }

            far_pos_t _next;
            /**
            * Number of free blocks adjacent one by at point addressed by _next. Valid values are in range
            * [0..#Capacity)
            */
            std::uint32_t _adjacent_count;
        };

    protected:

        bool has_residence(segment_idx_t segment_idx, const SegmentManager& manager) const override
        {
            return true; //always true, has always FixedSizeMemoryManager in each segment
        }
        /**
        *   @return byte size that should be reserved inside segment.
        */
        segment_pos_t byte_size(FarAddress segment_address, const SegmentManager& manager) const override
        {
            segment_pos_t result = 0;
            auto addr_emulation = segment_address.address;
            if (segment_address.segment() == 0)
            {  //reserve place for ZeroHeader struct in 0-segment
                auto align_pad = //padding needed if segment_address not aligned well
                    static_cast<segment_pos_t>(
                        OP::utils::align_on(addr_emulation, alignof(ZeroHeader)) - addr_emulation);
                result += memory_requirement<ZeroHeader>::requirement + align_pad;
                addr_emulation += align_pad;
            }
            auto align_pad2 = //padding needed if segment_address not aligned well
                static_cast<segment_pos_t>(
                    OP::utils::align_on(addr_emulation, max_entry_align_c) - addr_emulation);
            result += align_pad2 + entry_size_c * Capacity;
            return result;
        }
        /**
        *   Make initialization of slot in the specified segment as specified offset
        */
        void on_new_segment(FarAddress start_address, SegmentManager& manager) override
        {
            std::lock_guard guard(_topology_mutex);

            _segment_manager = &manager;
            FarAddress blocks_begin;
            OP::vtm::TransactionGuard op_g(_segment_manager->begin_transaction()); //invoke begin/end write-op
            ZeroHeader* header;
            if (start_address.segment() == 0)
            { //create special entry for ZeroHeader

                blocks_begin = FarAddress(OP::utils::align_on(
                    start_address.address, alignof(ZeroHeader)));
                _zero_header_address = blocks_begin;
                blocks_begin += memory_requirement<ZeroHeader>::requirement;
                //capturing zero-block 
                header = _segment_manager->wr_at<ZeroHeader>(
                    _zero_header_address, WritableBlockHint::new_c);
                *header = ZeroHeader{ SegmentDef::far_null_c };
            }
            else
            {
                header = OP::vtm::template transactional_yield_retry_n<10>([this]()
                    {
                        return _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                    });
                header->_in_free += Capacity;
                blocks_begin = FarAddress(
                    OP::utils::align_on(start_address.address, max_entry_align_c));
            }
            FreeBlockHeader* big_chunk =
                _segment_manager->wr_at<FreeBlockHeader>(blocks_begin, WritableBlockHint::new_c);
            *big_chunk = FreeBlockHeader{
                header->_next,
                Capacity - 1 // (-1) since when (adjacent == 0) we have exact 1 free block
            };
            header->_next = blocks_begin;

            op_g.commit();
        }
        /**
        *   Perform slot openning in the specified segment as specified offset
        */
        void open(FarAddress start_address, SegmentManager& manager) override
        {
            std::lock_guard guard(_topology_mutex);
            _segment_manager = &manager;
            if (start_address.segment() == 0)
            {
                _zero_header_address = start_address;
            }
        }
        /**Notify slot that some segement should release resources. It is not about deletion of segment, but deactivating it*/
        void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
        {

        }

    private:
        /**Size of entry in persistence state, must have capacity to accommodate ZeroHeader*/
        constexpr static const segment_pos_t entry_size_c =
            memory_requirement<FreeBlockHeader>::requirement > memory_requirement<Payload>::requirement 
            ? memory_requirement<FreeBlockHeader>::requirement 
            : memory_requirement<Payload>::requirement;
        /**When FreeBlockHeader and Payload occupies the same memory - both entries must be aligned properly*/
        constexpr static const size_t max_entry_align_c =
            std::max(alignof(Payload), alignof(FreeBlockHeader));
        SegmentManager* _segment_manager = nullptr;
        FarAddress _zero_header_address;
        std::mutex _topology_mutex;
    };
}//ns:OP::vtm

#endif //_OP_VTM_FIXEDSIZEMEMORYMANAGER__H_