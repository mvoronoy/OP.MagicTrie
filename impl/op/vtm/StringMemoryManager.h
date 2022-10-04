#pragma once
#ifndef _OP_VTM_STRINGMEMORYMANAGER__H_
#define _OP_VTM_STRINGMEMORYMANAGER__H_

#include <cstdint>
#include <type_traits>
#include <memory>
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
            /** Header for string persisted state */
            struct StringHead
            {
                using value_type = char;
                segment_pos_t _size = 0;
                value_type buffer[1];
            };
            /** Allows optimize string storage when size less than 8 bytes 
            * by avoiding memory allocation and placing characters instead 
            * of dynamic memory to FarAddress.
            * \tparam inline_byte_size_limit number must be great or equal to 
            *   sizeof(FarAddress) and not exceed 254. This value allows 
            *   indicate number of bytes that are allowed to use for string
            *   inlining before dynamic memory allocation
            */
            template <size_t inline_byte_size_limit = sizeof(FarAddress)>
            struct SmartStringAddress
            {
                /** Const used as indicator for FarAddress useage */
                constexpr static std::uint8_t far_use_c = ~std::uint8_t{};
                static_assert(inline_byte_size_limit < far_use_c && inline_byte_size_limit >= sizeof(FarAddress),
                    "Invalid inlining byte_size used. The value must be in range [sizeof(FarAddress)..254]"
                );
                /** Limit of string size that can be stored in short (inlined) form 
                */
                constexpr static size_t data_byte_size_c = 
                    std::max(inline_byte_size_limit - 1/*for _short_size*/, sizeof(FarAddress));
                union 
                {
                    FarAddress _far;
                    char _buffer[data_byte_size_c];
                } _address = { FarAddress{} };
                /** flag that allow distinguish short-string from heap-allocated string*/
                std::uint8_t _short_size = far_use_c;

                /** Check if current state points nowhere */
                constexpr bool is_nil() const
                {
                    return _short_size == far_use_c && _address._far.is_nil();
                }
            };
        }//ns:smm

        /**
        *  StringMemoryManager simplifies persistence of string in SegmentManagement 
        * structure. Internally it just use allocation with help of HeapManagerSlot so 
        * topology must specify this slot.
        * String management based on 3 methods #insert, #destroy and #get  
        */
        template <size_t smart_string_allowance = sizeof(FarAddress)>
        struct StringMemoryManager 
        {
            using element_t = typename smm::StringHead::value_type;
            using persisted_char_array_t = PersistedSizedArray<element_t, segment_pos_t>;
            using smart_str_address_t = smm::SmartStringAddress<smart_string_allowance>;

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
            
            template <class StringLike>
            smart_str_address_t smart_insert(const StringLike& str)
            {
                return smart_insert(str.begin(), str.end());
            }
            
            template <class Iterator>
            smart_str_address_t smart_insert(Iterator begin, Iterator end)
            {
                auto sz = end - begin;
                if(smart_str_address_t::data_byte_size_c < sz)//regular long str
                {
                    return smart_str_address_t{
                        insert(begin, end),
                        smart_str_address_t::far_use_c
                    };
                }
                smart_str_address_t short_address; 
                short_address._short_size = static_cast<std::uint8_t>(sz);
                std::copy(begin, end, short_address._address._buffer);
                return short_address;
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
            
            void destroy(const smart_str_address_t& str)
            {
                if(smart_str_address_t::data_byte_size_c < str._short_size )
                { //regular str
                    destroy(str._address._far);
                    return;
                }
                //do nothing with short str
            }

            segment_pos_t size(FarAddress str_addr)
            {
                persisted_char_array_t result (str_addr);
                return result.size(_segment_manager);
            }

            segment_pos_t size(const smart_str_address_t& str)
            {
                if(smart_str_address_t::data_byte_size_c < str._short_size )
                {
                    return size(str._address._far);
                }
                return str._short_size;
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
                if (!size)
                    return 0;
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

            template<class FOutControl>
            std::enable_if_t<std::is_invocable_v<FOutControl, element_t>, segment_pos_t>
            get(const smart_str_address_t& str_addr,
                FOutControl out_control,
                segment_pos_t offset = 0, 
                segment_pos_t length = std::numeric_limits<segment_pos_t>::max())
            {
                if(smart_str_address_t::data_byte_size_c < str_addr._short_size )
                {
                    return get(str_addr._address._far, out_control, offset, length);
                }
                //
                if (offset >= str_addr._short_size)
                    return 0;
                segment_pos_t size = std::min(
                    length, 
                    static_cast<segment_pos_t >(str_addr._short_size) - offset //it is safe since I've already check: `offset < head->_size`
                );
                segment_pos_t result = 0;
                for (; size; --size, ++result, ++offset)
                {
                    if(!out_control(str_addr._address._buffer[offset]))
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

            template<class OutIter>
            std::enable_if_t< 
                std::is_same_v<typename std::iterator_traits<OutIter>::iterator_category, std::output_iterator_tag >,
                segment_pos_t>
                get(const smart_str_address_t& str_addr, OutIter out,
                        segment_pos_t offset = 0, segment_pos_t length = std::numeric_limits<segment_pos_t>::max())
            {
                if(smart_str_address_t::data_byte_size_c < str_addr._short_size )
                {
                    return get(str_addr._address._far, out, offset, length);
                }
                if (offset >= str_addr._short_size)
                    return 0;
                segment_pos_t size = std::min(
                    length, 
                    static_cast<segment_pos_t >(str_addr._short_size) - offset //it is safe since I've already check: `offset < head->_size`
                );
                segment_pos_t result = 0;
                for (; size; --size, ++result, ++offset)
                {
                    *out = str_addr._address._buffer[offset]; 
                    ++out; 
                }
                return result;
            }

        private:
            SegmentManager& _segment_manager;
            HeapManagerSlot& _heap_mngr;
        };
    
}//endof namespace OP::vtm
#endif //_OP_VTM_STRINGMEMORYMANAGER__H_