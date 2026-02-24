#pragma once

#ifndef _OP_VTM_SKPLST__H_
#define _OP_VTM_SKPLST__H_

#include <assert.h>
#include <array>
#include <numeric>
#include <memory>
#include <mutex>

#include <op/common/Utils.h>

#include <op/vtm/Transactional.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>

#include <op/vtm/slots/HeapBlockHeader.h>

namespace OP::vtm
{
        using namespace OP::utils;


        /***/
        template <segment_pos_t bitmask_size_c = 32>
        struct Log2SkipList
        {
            using this_t = Log2SkipList<bitmask_size_c>;
            
            constexpr static size_t smallest_c = sizeof(std::uint32_t) << 3;//smallest block = 32

            /**
            *   Evaluate how many bytes expected to place to the header of this data-structure.
            */
            constexpr static segment_pos_t byte_size() noexcept
            {
                return OP::utils::align_on(
                    memory_requirement<ForwardListBase>::array_size( bitmask_size_c ),
                    SegmentDef::align_c);
            }

            constexpr size_t entry_index(size_t key) const noexcept
            {
                size_t base = 0;
                const size_t low_strat = 256;
                if (key < low_strat)
                    return key * 3 / low_strat;
                base = 3;
                key -= low_strat;
                const size_t mid_strat = 4096;
                if (key < mid_strat)/*Just an assumption about biggest payload stored in virtual memory*/
                    return base + ((key * (smallest_c / 2/*aka 16*/)) / (mid_strat));
                base += smallest_c / 2;
                key -= mid_strat;
                size_t result = base + (key * (smallest_c - 3 - smallest_c / 2/*aka 13*/)) / _largest;
                if (result >= smallest_c)
                    return smallest_c - 1;
                return result;
            }

            /**Format memory starting from `start` for skip-list header
            *@return new instance of Log2SkipList
            */
            static std::unique_ptr<this_t> create_new(SegmentManager& manager, FarAddress start)
            {
                auto wr = manager.writable_block(start, byte_size(), WritableBlockHint::new_c);
                
                //make formatting for all bunches
                new (wr.pos())ForwardListBase[bitmask_size_c];
                
                return std::make_unique<this_t>(manager, start);
            }

            /**Open existing list from starting point 'start'
            *@return new instance of Log2SkipList
            */
            static std::unique_ptr<this_t> open(SegmentManager& manager, FarAddress start)
            {
                return std::make_unique<this_t>(manager, start);
            }
            /**
            *   Construct new header for skip-list.
            * \param start position where header will be placed. After this position header will occupy
            memory block of #byte_size() length
            */
            Log2SkipList(SegmentManager& manager, FarAddress start) 
                : _segment_manager(manager)
                , _list_pos(start)
                , _largest(_segment_manager.segment_size())
            {
            }

            ~Log2SkipList()
            {
            }

            /**
            * Finds through contained memory blocks as HeapBlockHeader and return first one not less than size specified by key.
            * Returned block is automatically removed.
            * 
            * \return std::pair: first is FarAddress of HeapBlockHeader (may be nil if list has no big enough block) and second 
            *   is size of result block.
            */
            std::pair<FarAddress, segment_pos_t> pull_not_less(size_t key)
            {
                //trick to use additional mutex that grants that ro-access is enough to 
                //find matched block then upgrade to wr
                std::lock_guard g(_list_acc);
                ConstantPersistedArray<ForwardListBase> list(_list_pos);
                auto index = entry_index(key);
                //try to pull from correct bucket, if fails start from biggest to smallest
                //such way big chunk is split ASAP
                ReadonlyAccess<ForwardListBase> ro_entry =
                    list.ref_element(_segment_manager, index);
                std::pair<FarAddress, segment_pos_t> result{};
                if(pull_from_bucket(key, ro_entry, result.first, result.second))
                    return result;
                for(auto i = bitmask_size_c; i > index; --i)
                {//start from biggest
                    auto ro_entry2 =
                        list.ref_element(_segment_manager, i-1);
                    if (pull_from_bucket(key, ro_entry2, result.first, result.second))
                        return result;
                }

                return result;
            }

            void insert(FarAddress memory_block_addr, HeapBlockHeader* memory_block)
            {
                std::lock_guard g(_list_acc); //trick to avoid complex retry logic when upgrading list from read to write
                auto key = memory_block->size();
                auto index = entry_index(key);
                PersistedArray<ForwardListBase> list(_list_pos);

                auto entry = _segment_manager.accessor<ForwardListBase>(list.element_address(index));
                if(entry->_next.is_nil()) //first time uses this bucket
                {
                    entry->_next = memory_block_addr;
                    memory_block->_next = FarAddress{};
                }
                else
                {
                    FarAddress previous{ entry.address() }, current{ entry->_next };
                    void(*upgrade_previous)(SegmentManager&, FarAddress, FarAddress) = 
                        update_header_next<ForwardListBase>;

                    while(!current.is_nil())
                    { // support ordering of same `index` block by size
                        auto header = _segment_manager.view<HeapBlockHeader>(current);

                        if (!less(header->size(), key))
                        {//ready to insert

                            memory_block
                                ->next(current); //point to header
                            upgrade_previous(_segment_manager, previous, memory_block_addr);
                            break;
                        }
                        previous = current;
                        current = header->_next;
                    }
                }
                memory_block
                    ->set_free(true)
                    ;
            }

        private:
            /**Compare 2 FreeMemoryBlock by the size*/
            constexpr static bool less(size_t left, size_t right) noexcept
            {
                return left < right;
            }

            //used together with `update_header_next` to support forward-list update 
            //of previous item in `pull_from_bucket`
            static void update_list_next(SegmentManager& segment_manager, ReadonlyMemoryChunk& ro, FarAddress address) {
                WritableAccess<ForwardListBase> wr{
                        segment_manager.upgrade_to_writable_block(ro)
                };
                wr->_next = address;
            }

            //used together with `update_list_next` to support forward-list update 
            //of previous item in `pull_from_bucket`
            template <class T>
            static void update_header_next(SegmentManager& segment_manager, FarAddress previous, FarAddress address) {
                segment_manager.wr_at<T>(previous)->_next = address;
            };

            /** pull single free block from bucket started at 'start_from'. 
            * \param result [out] on success points to HeapBlockHeader that is not less queried block size `key`
            * \return true if such block exists in bucket, false mean caller should retry with bigger bucket
            */
            bool pull_from_bucket(size_t key, ReadonlyAccess<ForwardListBase>& start_from, FarAddress& result, segment_pos_t& result_size)
            {
                //when starting need keep track of previous element, but it can be either ForwardList or HeapBlockHeader
                //so make 2 lambdas to update 'previous'
                void(*upgrade_previous)(SegmentManager&, FarAddress, FarAddress) = update_header_next<ForwardListBase>;
                FarAddress previous{ start_from.address() }, current{start_from->_next};
                while(!current.is_nil())
                {
                    auto ro_header = _segment_manager.view<HeapBlockHeader>(current);
                    if (!less(ro_header->size(), key))
                    {
                        auto header = WritableAccess<HeapBlockHeader>(
                            _segment_manager.upgrade_to_writable_block(ro_header));
                        //remove from list
                        upgrade_previous(_segment_manager, previous, header->_next);
                        header->set_free(false);
                        header->next(FarAddress{}); //just for case
                        result = header.address();
                        result_size = header->_size;
                        return true;
                    }
                    previous = current;
                    upgrade_previous = update_header_next<HeapBlockHeader>; //the rest of the list consist of HeapBlockHeader so ready to update _next
                    current = ro_header->_next;
                }
                return false;
            }
            SegmentManager& _segment_manager;
            FarAddress _list_pos;
            //use recursive since `pull` can consequentially call `insert`
            std::recursive_mutex _list_acc;
            const size_t _largest;

    };
    
}//ns: OP::vtm
#endif //_OP_VTM_SKPLST__H_
