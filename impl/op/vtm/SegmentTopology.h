#pragma once

#ifndef _OP_VTM_SEGMENTTOPOLOGY__H_
#define _OP_VTM_SEGMENTTOPOLOGY__H_

#include <type_traits>
#include <cstdint>
#include <memory>

#include <op/vtm/SegmentManager.h>

namespace OP::vtm
{

        /**Abstract base to construct slots. Slot is an continuous mapped memory chunk that allows statically format 
        *  existing segment of virtual memory.
        * So instead of dealing with raw memory provided by SegmentManager, you can describe memory usage-rule 
        * at compile time by specifying SegmentTopology with bunch of slots.
        * For example: \code
        *   SegmentTopology<NodeManager, HeapManagerSlot> 
        * \endcode
        *   This example specifies that we place 2 slots into each segment processed by SegmentManager. 
        */
        class Slot
        {
        protected:
            explicit Slot(SegmentManager& manager) noexcept
                : _manager(&manager)
            {
            }

            Slot(Slot&&) noexcept = default;
            Slot(const Slot&) = delete;
            
            SegmentManager& segment_manager() const noexcept
            { return *_manager; }

        public:
            virtual ~Slot() = default;
            /**
            *   Check if slot should be placed to specific segment. This used to organize some specific behaviour.
            *   For example MemoryManagerSlot always returns true - to place memory manager in each segment.
            *   Some may returns true only for segment #0 - to support single residence only.
            */
            virtual bool has_residence(segment_idx_t segment_idx) const = 0;
            /**
            *   @return byte size that should be reserved inside segment. 
            */
            virtual segment_pos_t byte_size(FarAddress segment_address) const = 0;
            /**
            *   Make initialization of slot in the specified segment as specified offset
            */
            virtual void on_new_segment(FarAddress start_address) = 0;
            /**
            *   Perform slot opening in the specified segment as specified offset
            */
            virtual void open(FarAddress start_address) = 0;

            /**Notify slot that some segment should release resources. It is not about deletion of segment, but deactivating it*/
            virtual void release_segment(segment_idx_t segment_index) = 0;

            /**Allows on debug check integrity of particular segment. Default impl does nothing.
            *   Implementation can use exception or `assert` (in debug mode) to discover failures in contracted structures.
            */
            virtual void _check_integrity(FarAddress segment_addr)
            {
                /*Do nothing*/
            }

        private:
            SegmentManager* _manager;
        };

        /**
        *   SegmentTopology is a way to declare how interior memory of virtual memory segment will be used.
        *   Topology is described as linear (one by one) applying of slots. Each slot is controlled by corresponding 
        *   Slot-inherited object that is specified as template argument.
        *   For example SegmentTopology <FixedSizeMemoryManager, HeapMemorySlot> declares that Segment have to accommodate 
        *   FixedSizeMemoryManager in the beginning and HeapMemory after all
        */
        template <class ... TSlot>
        class SegmentTopology : public SegmentEventListener
        {
            struct TopologyHeader
            {
                std::uint16_t _slots_count;
                segment_pos_t _address[1];
            };

            using slots_t = std::tuple<std::unique_ptr<TSlot>...>;
            
        public:
            typedef SegmentManager segment_manager_t;
            typedef std::shared_ptr<segment_manager_t> segments_ptr_t;

            typedef SegmentTopology<TSlot...> this_t;

            /** Total number of slots in this topology */
            constexpr static size_t slots_count_c = (sizeof...(TSlot));
            /** Small reservation of memory for explicit addresses of existing slots*/
            constexpr static size_t addres_table_size_c = OP::utils::align_on(
                OP::utils::memory_requirement<TopologyHeader>::requirement + 
                    //(-1) because TopologyHeader::array already preservers 1 item of segment_pos_t
                OP::utils::memory_requirement<segment_pos_t>::array_size(slots_count_c-1),
                    //payload inside segments start aligned 
                SegmentDef::align_c
                );

            template <class TSegmentManager>
            SegmentTopology(std::shared_ptr<TSegmentManager> segments) 
                : _slots{std::make_unique<TSlot>(*segments)...}
                , _segments(std::move(segments))
            {
                static_assert(sizeof...(TSlot) > 0, "Specify at least 1 TSlot to declare topology");
                _segments->subscribe_event_listener(this);
                
                OP::vtm::TransactionGuard g(_segments->begin_transaction());
                _segments->foreach_segment([this](segment_idx_t segment_idx, SegmentManager& segment_manager){
                    on_segment_opening(segment_idx, segment_manager);
                });
                //force to have at least 1 segment
                _segments->ensure_segment(0);
                g.commit();
            }

            SegmentTopology(const SegmentTopology&) = delete;

            template <class T>
            T& slot()
            {
                return *std::get<std::unique_ptr<T>>(_slots);
            }

            void _check_integrity()
            {
                _segments->_check_integrity();
                _segments->foreach_segment([this](segment_idx_t idx, SegmentManager& segments){
                    segment_pos_t current_offset = segments.header_size();
                    //start write topology right after header
                    ReadonlyMemoryChunk topology_address = segments.readonly_block(FarAddress(idx, current_offset), 
                        addres_table_size_c);
                    current_offset += addres_table_size_c;
                    std::apply(
                        [&](auto& slot_ptr){

                            auto& slot = static_cast<Slot&>(*slot_ptr);
                            if (slot.has_residence(idx))
                            {
                                FarAddress addr(idx, current_offset);
                                slot._check_integrity(addr);
                                current_offset += slot.byte_size(addr);
                            }
                        },  _slots);
                });
            }

            SegmentManager& segment_manager()
            {
                return *_segments;
            }

        protected:
            /** When new segment allocated this callback ask each Slot from topology optionally
                allocate itself in new segment
            */
            void on_segment_allocated(segment_idx_t new_segment, segment_manager_t& manager)
            {
                segment_pos_t current_offset = manager.header_size();
                //start write topology right after header
                auto topology_block = manager
                    .writable_block(FarAddress(new_segment, current_offset), addres_table_size_c);
                TopologyHeader* header = topology_block.template at<TopologyHeader>(0);
                current_offset += addres_table_size_c;
                header->_slots_count = 0;

                std::apply([&](auto& ... slot_ptr)->void {
                    ((current_offset += slot_on_segment_allocated(new_segment, manager, *slot_ptr, header, current_offset)), ...);
                    }, _slots);
            }
            
            /** Open existing (on file level) segment */
            void on_segment_opening(segment_idx_t opening_segment, segment_manager_t& manager)
            {
                segment_pos_t current_offset = manager.header_size();
                
                segment_pos_t processing_size = addres_table_size_c;
                ReadonlyMemoryChunk topology_address = manager.readonly_block(
                    FarAddress(opening_segment, current_offset), processing_size);
                const TopologyHeader* header = topology_address.at<TopologyHeader>(0);
                assert(header->_slots_count == slots_count_c);
                 
                std::apply([&](auto& ...slot_ptr)->void{
                    size_t i = 0;
                    (slot_on_segment_opening(opening_segment, manager, header->_address[i++], *slot_ptr), ...);
                }, _slots);
            }

        private:
            segment_pos_t slot_on_segment_allocated(
                segment_idx_t new_segment, segment_manager_t& manager, Slot& slot, TopologyHeader* header, segment_pos_t current_offset)
            {
                auto current_slot_index = header->_slots_count++;
                if(slot.has_residence(new_segment))
                {
                    header->_address[current_slot_index] = current_offset;
                    FarAddress segment_address(new_segment, current_offset);
                    slot.on_new_segment(segment_address);
                    return slot.byte_size(segment_address); //precise number of bytes to reserve
                } 
                else
                {//slot is not used for this segment
                    header->_address[current_slot_index] = SegmentDef::eos_c;
                    return 0; //empty size required
                }
            }
            
            void slot_on_segment_opening(
                segment_idx_t opening_segment,
                segment_manager_t& manager, segment_pos_t in_slot_address, Slot& slot)
            {
                if (SegmentDef::eos_c != in_slot_address)
                {
                    slot.open(FarAddress(opening_segment, in_slot_address));
                }
            }

            slots_t _slots;
            segments_ptr_t _segments;
        };
        
        /**Resolver of SegmentManager from class instance that has accessor `segment_manager()`. */
        template <class T>
        inline std::enable_if_t< std::is_invocable_v<decltype(&T::segment_manager), T& >, SegmentManager&> 
            resolve_segment_manager(T& t) noexcept
        {
            return resolve_segment_manager(t.segment_manager());
        }

}//endof namespace OP::vtm

#endif //_OP_VTM_SEGMENTTOPOLOGY__H_
