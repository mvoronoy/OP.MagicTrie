#pragma once
#ifndef _OP_VTM_STRINGMEMORYMANAGER__H_
#define _OP_VTM_STRINGMEMORYMANAGER__H_

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <fstream>
#include <op/trie/Containers.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/FixedSizeMemoryManager.h>
#include <op/vtm/HeapManager.h>

namespace OP::vtm 
    {
        using namespace OP::utils;
        namespace smm
        {
            struct StringHead
            {
                using value_type = char;
                segment_pos_t _size = 0;
                value_type buffer[1];
            };

        }//ns:smm

        /**
        *  StringMemoryManager simplifies persistence of string in SegmentManagement 
        * structure. Internally it just use allocation with help of HeapManagerSlot so 
        * topology must specify this slot.
        * String management based on 3 methods #insert, #destroy and #get  
        */
        struct StringMemoryManager 
        {
            template <class TSegmentTopology>
            StringMemoryManager(TSegmentTopology& topology)
                : _segment_manager(topology.segment_manager())
                , _heap_mngr(topology.slot<HeapManagerSlot>())
            {
            }

            template <class StringLike>
            FarAddress insert(const StringLike& str)
            {
                using namespace std::string_literals;
                if (str.size() >= _segment_manager.segment_size())
                    throw std::out_of_range("String must be less than "s
                        + std::to_string(_segment_manager.segment_size()));

                auto alloc_size = //remember about +1 byte inside StringHead
                    static_cast<segment_pos_t>(OP::utils::align_on(
                        memory_requirement<smm::StringHead>::requirement
                        + sizeof(typename smm::StringHead::value_type) * str.size(),
                        SegmentHeader::align_c));
                ;

                OP::vtm::TransactionGuard op_g(
                    _segment_manager.begin_transaction()); //invoke begin/end write-op
                //
                auto result = _heap_mngr.allocate(alloc_size);
                auto block = _segment_manager.writable_block(
                    result, alloc_size, WritableBlockHint::new_c);
                auto header = block.at<smm::StringHead>(0);
                header->_size = static_cast<segment_pos_t>(str.size());
                memcpy(header->buffer, 
                    str.data(), 
                    static_cast<segment_pos_t>(str.size()) * sizeof(typename smm::StringHead::value_type) );

                op_g.commit();
                return result;
            }                                          
            
            /**
            *   \tparam Args - optional argument of Payload constructor.
            */
            void destroy(FarAddress str)
            {
                OP::vtm::TransactionGuard op_g(_segment_manager.begin_transaction()); //invoke begin/end write-op
                _heap_mngr.deallocate(str);
                op_g.commit();
            }

            template<class OutIter>
            segment_pos_t get(FarAddress str_addr, OutIter out,
                segment_pos_t offset = 0, segment_pos_t length = ~segment_pos_t{})
            {
                OP::vtm::TransactionGuard op_g(
                    _segment_manager.begin_transaction()); //invoke begin/end write-op
                auto head = view< smm::StringHead >(_segment_manager, str_addr);
                if (offset >= head->_size)
                    return 0;
                //non-empty
                constexpr auto element_size_c =
                    sizeof(typename smm::StringHead::value_type);
                //read body of the string
                FarAddress data_block =
                    str_addr
                    + static_cast<segment_off_t>(
                        offsetof(smm::StringHead, buffer)
                        + offset * element_size_c)
                    ;
                segment_pos_t size = std::min(length, head->_size);
                ReadonlyMemoryChunk ra = _segment_manager.
                    readonly_block(
                        data_block,
                        element_size_c * size,
                        ReadonlyBlockHint::ro_no_hint_c);
                auto source = ra.at<typename smm::StringHead::value_type>(0);
                segment_pos_t result = 0;
                for (; size; --size, ++out, ++source, ++result)
                    *out = *source;
                return result;
            }

        private:
            SegmentManager& _segment_manager;
            HeapManagerSlot& _heap_mngr;
        };
    
}//endof namespace OP::vtm
#endif //_OP_VTM_STRINGMEMORYMANAGER__H_