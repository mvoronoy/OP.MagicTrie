#pragma once
#ifndef _OP_VTM_STRINGMEMORYMANAGER__H_
#define _OP_VTM_STRINGMEMORYMANAGER__H_

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <fstream>
#include <op/common/has_member_def.h>
#include <op/trie/Containers.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/FixedSizeMemoryManager.h>
#include <op/vtm/HeapManager.h>
#include <op/vtm/PersistedReference.h>

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
            using element_t = typename smm::StringHead::value_type;
            using persisted_char_array_t = PersistedSizedArray<element_t, segment_pos_t>;

            template <class TSegmentTopology>
            StringMemoryManager(TSegmentTopology& topology)
                : _segment_manager(topology.segment_manager())
                , _heap_mngr(topology.template slot<HeapManagerSlot>())
            {
            }
            /**
            * Allocate new persisted string.
            * \tparam StringLike - any type supporting size(), data() methods
            * \throws std::out_of_range - when string size exceeds segment capacity
            * \return FarAddress allocated by HeapManagerSlot
            */
            template <class StringLike>
            FarAddress insert(const StringLike& str)
            {
                return insert(str.begin(), str.end());
                //using namespace std::string_literals;
                //if (str.size() >= _segment_manager.segment_size())
                //    throw std::out_of_range("String must be less than "s
                //        + std::to_string(_segment_manager.segment_size()));

                //auto alloc_size = persisted_char_array_t::memory_requirement(
                //    static_cast<segment_pos_t>(str.size()));

                //OP::vtm::TransactionGuard op_g(
                //    _segment_manager.begin_transaction()); //invoke begin/end write-op
                ////
                //persisted_char_array_t result (_heap_mngr.allocate(alloc_size));
                //auto& content = result.ref(_segment_manager);

                //content.size = static_cast<segment_pos_t>(str.size());
                //memcpy(content.data, 
                //    str.data(), 
                //    static_cast<segment_pos_t>(str.size()) * sizeof(element_t) );

                //op_g.commit();
                //return result.address;
            }                                          
            template <class Iterator>
            FarAddress insert(Iterator begin, Iterator end)
            {
                using namespace std::string_literals;
                auto size = (end - begin);
                if (size >= _segment_manager.segment_size())
                    throw std::out_of_range("String must be less than "s
                        + std::to_string(_segment_manager.segment_size()));

                segment_pos_t segment_adjusted_size = static_cast<segment_pos_t>(size);
                auto alloc_size = persisted_char_array_t::memory_requirement(
                    segment_adjusted_size);

                OP::vtm::TransactionGuard op_g(
                    _segment_manager.begin_transaction()); //invoke begin/end write-op
                //
                persisted_char_array_t result (_heap_mngr.allocate(alloc_size));
                auto& content = result.ref(_segment_manager, segment_adjusted_size);

                content.size = segment_adjusted_size;
                for(auto *p = content.data; begin != end; ++begin, ++p) 
                    *p = *begin;

                op_g.commit();
                return result.address;
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

            segment_pos_t size(FarAddress str_addr)
            {
                persisted_char_array_t result (str_addr);
                return result.size(_segment_manager);
            }
            /**
            *  Extract string from the persisted state.
            *  
            *  \param str_addr - string previously allocated by #inset
            *  \param offset - start taking persisted characters from this position, default 
            *                   is 0. If offset exceeds persisted string size nothing copied to
            *                   output iterator
            *  \param length - desired length of persisted character sequence to extract, 
            *                default is `std::numeric_limits<segment_pos_t>::max()`
            *  \tparam  FOutControl - have signature `bool (TChar)`
            *  \return total number of symbols loaded to `out_control` (this value is never exceeds persisted
            *       string size and `length`)
            */
            template<class FOutControl>
            std::enable_if_t<std::is_invocable_v<FOutControl, element_t>, segment_pos_t>
            get(FarAddress str_addr, 
                FOutControl out_control,
                segment_pos_t offset = 0, 
                segment_pos_t length = std::numeric_limits<segment_pos_t>::max())
            {
                OP::vtm::TransactionGuard op_g(
                    _segment_manager.begin_transaction()); //invoke begin/end read-op
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
                segment_pos_t size = std::min(
                    length, 
                    head->_size - offset //it is safe since I've already check: `offset < head->_size`
                );
                ReadonlyMemoryChunk ra = _segment_manager.
                    readonly_block(
                        data_block,
                        element_size_c * size);
                auto source = ra.at<typename smm::StringHead::value_type>(0);
                segment_pos_t result = 0;
                for (; size; --size, ++source, ++result)
                {
                    if(!out_control(*source))
                        break;
                }
                return result;
            }

            /** 
            * Simplified version of #get (see above), when instead of predicate the std::back_insert_iterator used.
            *  \tparam OutIter - output iterator, something like std::back_insert_iterator. It
            *       have to support `++` and `*` operators.
            */
            template<class OutIter>
            std::enable_if_t< 
                std::is_same_v<typename std::iterator_traits<OutIter>::iterator_category, std::output_iterator_tag >,
                segment_pos_t>
                get(FarAddress str_addr, OutIter out,
                        segment_pos_t offset = 0, segment_pos_t length = std::numeric_limits<segment_pos_t>::max())
            {
                return this->get(str_addr,
                    [&](element_t symb){*out = symb; ++out; return true;}, offset, length);
            }

        private:
            SegmentManager& _segment_manager;
            HeapManagerSlot& _heap_mngr;
        };
    
}//endof namespace OP::vtm
#endif //_OP_VTM_STRINGMEMORYMANAGER__H_