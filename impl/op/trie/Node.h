
#pragma once

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <fstream>
#include <op/trie/Containers.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Range.h>
namespace OP
{
    namespace trie
    {
        /**
        @tparam Capacity number of #Payload entries in this container
        */
        template <class Payload, size_t Capacity>
        struct NodeManager : public Slot
        {
            static_assert(Capacity > 0, "Capacity template argument must be greater than 0");

            typedef Payload payload_t;
            typedef NodeManager<Payload, Capacity> this_t;

            FarAddress allocate()
            {
                OP::vtm::TransactionGuard op_g(_segment_manager->begin_transaction()); //invoke begin/end write-op
                //capture ZeroHeader for write during 10 tries
                auto header = OP::vtm::transactional_yield_retry_n<10>([this]()
                {
                    return _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                });
                if (header->_next == SegmentDef::far_null_c)
                {  //need allocate new segment
                    auto avail_segments = _segment_manager->available_segments();
                    _segment_manager->ensure_segment(avail_segments);
                }
                FarAddress result;
                if (header->_adjacent_count > 1) 
                {//there are adjacent blocks more then one, so don't care about following list of other
                    --header->_adjacent_count;
                    //return last available entry 
                    result.address = header->_next + memory_requirement<Payload>::requirement * header->_adjacent_count;
                }
                else
                {//only one entry left, so need rebuild further list
                    result.address = header->_next; //points to block with ZeroHeader over it will be erased after allocation
                    *header =
                        *_segment_manager->ro_at<ZeroHeader>(result);
                }
                *_segment_manager->wr_at<payload_t>(result) = std::move(payload_t());
                op_g.commit();
                return result;
            }
            void deallocate(FarAddress addr)
            {
                OP::vtm::TransactionGuard op_g(_segment_manager->begin_transaction()); //invoke begin/end write-op
                //@! todo: validate that addr belong to NodeManager
                
                //capture ZeroHeader for write during 10 tries
                auto header = OP::vtm::transactional_yield_retry_n<10>([this]()
                {
                    return _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                });
                //following will raise ConcurentLockException immediatly, if 'addr' cannot be locked
                ZeroHeader* just_freed = _segment_manager->wr_at<ZeroHeader>(FarAddress(addr), WritableBlockHint::new_c);
                *just_freed = *header;
                header->_next = addr;
                header->_adjacent_count = 1;
                op_g.commit();
            }
            void _check_integrity(FarAddress segment_addr, SegmentManager& manager)
            {
                if (segment_addr.segment != 0)
                    return;
                //check from _zero_header_address
                auto header = 
                    manager.readonly_block(_zero_header_address, sizeof(ZeroHeader)).at<ZeroHeader>(0);
                size_t n_free_blocks = 0;
                //count all free blocks
                while( header->_next != SegmentDef::far_null_c )
                {
                    assert(header->_adjacent_count > 0);
                    n_free_blocks += header->_adjacent_count;
                    header = manager.readonly_block(
                        FarAddress(header->_next), sizeof(ZeroHeader)).at<ZeroHeader>(0);
                }
                assert(manager.available_segments() * Capacity >= n_free_blocks);
            }
        private:
            /**Structure specific for 0 segment only*/
            struct ZeroHeader
            {
                /**Pointer from 1 to #Capacity blocks free to use, if no free blocks this value is #SegmentDef::far_null_c. 
                * This value can point not only single segment.
                */
                far_pos_t _next;
                /** Number of free blocks adjacent one by at point addressed by _next. Valid values starts from 1 to #Capacity */
                std::uint32_t _adjacent_count;
            };
        protected:

            virtual bool has_residence(segment_idx_t segment_idx, SegmentManager& manager) const override
            {
                return true; //always true, has always NodeManager in segment
            }
            /**
            *   @return byte size that should be reserved inside segment. 
            */
            virtual segment_pos_t byte_size(FarAddress segment_address, SegmentManager& manager) const override 
            {
                segment_pos_t result = entry_size_c * Capacity;
                if (segment_address.segment == 0)
                {  //reserve place for ZeroHeader struct in 0-segment
                    result += memory_requirement<ZeroHeader>::requirement;
                }
                return result;
            }
            /**
            *   Make initialization of slot in the specified segment as specified offset
            */
            virtual void on_new_segment(FarAddress start_address, SegmentManager& manager) override
            {
                _segment_manager = &manager;
                auto blocks_begin = start_address;
                OP::vtm::TransactionGuard op_g(_segment_manager->begin_transaction()); //invoke begin/end write-op
                if (start_address.segment == 0)
                { //create special entry for ZeroHeader
                    blocks_begin += memory_requirement<ZeroHeader>::requirement;
                    _zero_header_address = start_address;
                    //no problem with following capturing zero-block again
                    *_segment_manager->wr_at<ZeroHeader>(_zero_header_address, WritableBlockHint::new_c) 
                    = { SegmentDef::far_null_c, 0 };
                }
                //New segment allocation is a big challenge for entire system, that is why 
                //count of reties to capture ZeroHeader so big
                auto header = OP::vtm::transactional_yield_retry_n<1000>([this]()
                {
                    return _segment_manager->wr_at<ZeroHeader>(_zero_header_address);
                });
                //in the just allocated blocks make copy of zero-header
                *_segment_manager->wr_at<ZeroHeader>(blocks_begin, WritableBlockHint::new_c) 
                    = *header;
                header->_next = blocks_begin;
                header->_adjacent_count = Capacity;
                op_g.commit();
            }
            /**
            *   Perform slot openning in the specified segment as specified offset
            */
            virtual void open(FarAddress start_address, SegmentManager& manager) override
            {
                if (start_address.segment == 0)
                {
                    _zero_header_address = start_address;
                }
            }
            /**Notify slot that some segement should release resources. It is not about deletion of segment, but deactivating it*/
            virtual void release_segment(segment_idx_t segment_index, SegmentManager& manager) override
            {

            }

        private:
            /**Size of entry in persistence state, must have capacity to accomodate ZeroHeader*/
            static OP_CONSTEXPR(const) segment_pos_t entry_size_c =
                memory_requirement<ZeroHeader>::requirement > memory_requirement<Payload>::requirement ?
                memory_requirement<ZeroHeader>::requirement : memory_requirement<Payload>::requirement;
            SegmentManager* _segment_manager;
            FarAddress _zero_header_address;
        };
    }
}//endof namespace OP
