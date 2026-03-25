#pragma once

#ifndef _OP_VTM_STRINGMEMORYMANAGER__H_
#define _OP_VTM_STRINGMEMORYMANAGER__H_

#include <cstdint>
#include <type_traits>
#include <memory>
#include <fstream>
#include <op/common/has_member_def.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/ShadowBuffer.h>
#include <op/vtm/slots/HeapManager.h>
#include <op/vtm/PersistedReference.h>

namespace OP::vtm 
{
    using namespace OP::utils;
    namespace smm
    {
        /** Header for string persisted state */
        struct StringHeader
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
            /** Const used as indicator for FarAddress usage */
            constexpr static std::uint8_t far_use_c = ~std::uint8_t{};
            static_assert(inline_byte_size_limit < far_use_c && inline_byte_size_limit >= sizeof(FarAddress),
                "Invalid inlining byte_size used. The value must be in range [sizeof(FarAddress)..254]"
            );
            struct as_short_str_t {} constexpr static inline as_short_str{};

            static constexpr bool can_use_short_str(segment_pos_t str_size) noexcept
            {
                return str_size <= data_byte_size_c;
            }

            constexpr SmartStringAddress() noexcept
            {
                as_far_address() = FarAddress{};
            }
            //constexpr SmartStringAddress& operator = (const SmartStringAddress&) noexcept = default;
            //constexpr SmartStringAddress& operator = (SmartStringAddress&&) noexcept = default;

            constexpr explicit SmartStringAddress(FarAddress addr) noexcept
            {
                as_far_address() = addr;
            }

            template <class Iter>
            constexpr explicit SmartStringAddress(Iter begin, Iter end) noexcept
                : _short_size(std::min(data_byte_size_c, static_cast<std::uint8_t>(std::distance(begin, end))))
            {
                assert(std::distance(begin, end) <= data_byte_size_c);
                ShortStr& substr = *reinterpret_cast<ShortStr*>(std::launder(_data));
                std::copy_n(begin, _short_size, substr._buffer);
            }
            
            //construct zero-size short str
            constexpr explicit SmartStringAddress(as_short_str_t) noexcept
                : _short_size(0)
            {
            }

            SmartStringAddress& assign(FarAddress addr) noexcept
            {
                _short_size = far_use_c;
                as_far_address() = addr;
                return *this;
            }

            template <class StringLike>
            SmartStringAddress& assign(const StringLike& str) noexcept
            {
                assert(str.size() <= data_byte_size_c);
                _short_size = static_cast<std::uint8_t>(str.size());
                ShortStr& substr = *reinterpret_cast<ShortStr>(std::launder(_data));
                std::copy_n(str.begin(), str.end(), substr._buffer);
                return *this;
            }

            template <class T>
            SmartStringAddress& operator = (const T& value) noexcept
            {
                return assign(value);
            }

            /** Check if current state points nowhere */
            constexpr bool is_nil() const noexcept
            {
                return _short_size == far_use_c && as_far_address().is_nil();
            }

            constexpr bool is_short_str() const noexcept
            {
                return _short_size < far_use_c;
            }

            constexpr std::uint8_t short_size() const noexcept
            {
                assert(_short_size != far_use_c);
                return _short_size;
            }

            constexpr void resize(std::uint_fast8_t new_size) noexcept
            {
                assert(_short_size != far_use_c);
                assert(new_size < data_byte_size_c);
                _short_size = new_size;
            }

            constexpr FarAddress& as_far_address() noexcept
            {
                assert(_short_size == far_use_c);
                return *reinterpret_cast<FarAddress*>(std::launder(_data));
            }

            constexpr const FarAddress& as_far_address() const noexcept
            {
                assert(_short_size == far_use_c);
                return *reinterpret_cast<const FarAddress*>(std::launder(_data));
            }

            constexpr OP::common::atom_string_view_t as_str() const noexcept
            {
                assert(is_short_str());
                const ShortStr& substr = *reinterpret_cast<const ShortStr*>(std::launder(_data));
                return OP::common::atom_string_view_t(_data, _short_size);
            }

        private:
            /** 0..254 value means length of string, otherwise distinguish short-string from heap-allocated string*/
            std::uint8_t _short_size = far_use_c;
            struct ShortStr
            {
                std::uint8_t _buffer[inline_byte_size_limit - 1];
            };
            /** Limit of string size that can be stored in short (inlined) form
            */
            constexpr static std::uint_fast8_t data_byte_size_c =
                std::max({ sizeof(ShortStr), sizeof(FarAddress) });

            static constexpr size_t max_data_align_c = std::max({ alignof(FarAddress), alignof(ShortStr) });

            alignas(max_data_align_c) std::uint8_t _data[data_byte_size_c];
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
        using element_t = typename smm::StringHeader::value_type;
        using header_t = smm::StringHeader;
        using smart_str_address_t = smm::SmartStringAddress<smart_string_allowance>;
        static_assert(std::is_standard_layout_v<smart_str_address_t>, "smm::SmartStringAddress must have standard layout");

        template <class TSegmentTopology>
        StringMemoryManager(TSegmentTopology& topology)
            : _segment_manager(topology.segment_manager())
            , _heap_mngr(topology.template slot<HeapManagerSlot>())
        {
        }

        /**
        * Allocate new persisted string. Must be called in the transaction scope.
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
                throw std::out_of_range("String length must be less than "s
                    + std::to_string(_segment_manager.segment_size()));

            segment_pos_t segment_adjusted_size = static_cast<segment_pos_t>(size);
            auto byte_alloc_size = memory_requirements(segment_adjusted_size);

            FarAddress result = _heap_mngr.allocate(byte_alloc_size);
            auto chunk = _segment_manager.writable_block(result, byte_alloc_size);
            header_t* string_header = chunk.at<header_t>(0);
            string_header->_size = segment_adjusted_size;
            for(auto *p = string_header->buffer; begin != end; ++begin, ++p)
                *p = *begin;

            return result;
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
            if(!smart_str_address_t::can_use_short_str(sz))//regular long str
            {
                return smart_str_address_t{insert(begin, end)};
            }
            smart_str_address_t short_address(begin, end); 
            return short_address;
        }

        /**
        * Destroy string persisted by #insert. Must be called in transaction.
        * \tparam Args - optional argument of Payload constructor.
        */
        void destroy(FarAddress str)
        {
            _heap_mngr.deallocate(str);
        }
        
        void destroy(smart_str_address_t& str)
        {
            if(!str.is_short_str())
            { //regular str
                destroy(str.as_far_address());
                return;
            }
            //do nothing with short str
            str = smart_str_address_t{};
        }

        void truncate(smart_str_address_t& str, segment_pos_t new_size)
        {
            if (str.is_short_str())
            {//for short str just change size
                if (new_size < str.short_size())
                {
                    if (new_size == 0)
                    {
                        destroy(str);
                        str = smart_str_address_t{ smart_str_address_t::as_short_str };
                    }
                    else
                        str.resize(static_cast<std::uint8_t>(new_size));
                }
            }
            else
            { //regular str
                smm::StringHeader prefix_str;
                auto addr = str.as_far_address();
                view(_segment_manager, addr, prefix_str);

                if (new_size < prefix_str._size)
                {
                    if (smart_str_address_t::can_use_short_str(new_size)) // can transform to short-string
                    {
                        if (new_size > 0)
                        {
                            auto reader = _segment_manager.readonly_block(addr,
                                memory_requirements(new_size));
                            auto* p = reader.template at<smm::StringHeader>(0)->buffer;
                            smart_str_address_t short_address(p, p + new_size);
                            str = short_address;
                        }
                        else
                            str = smart_str_address_t{ smart_str_address_t::as_short_str };//str = {}; //make result nil when new_size == 0
                        destroy(addr);
                    }
                    else
                    { // just shrink size of string
                        auto wr = _segment_manager.template accessor<smm::StringHeader>(addr);
                        wr->_size = new_size;
                    }
                }
            }
            
        }

        segment_pos_t size(FarAddress str_addr)
        {
            smm::StringHeader head;
            view(_segment_manager, str_addr, head);
            return head._size;
        }

        segment_pos_t size(const smart_str_address_t& str)
        {
            if (str.is_short_str())
                return str.short_size();
            else
                return size(str.as_far_address());
        }
        
        /** Take string from `str_addr`, then substring specified by offset/length appended to `dest`. */
        template <class TStringLike>
        segment_pos_t append_to(const smart_str_address_t& str_addr, 
            TStringLike& dest,
            segment_pos_t offset = 0, 
            segment_pos_t length = std::numeric_limits<segment_pos_t>::max())
        {
            if (str_addr.is_short_str())
            {
                if (offset >= str_addr.short_size())
                    return 0;
                auto prev_size = dest.size();
                dest.append(str_addr.as_str(), offset, length);
                return static_cast<segment_pos_t>(dest.size() - prev_size);
            }
            else
            { //regular str
                smm::StringHeader head;
                view(_segment_manager, str_addr.as_far_address(), head);
                if (offset >= head._size)
                    return 0;
                constexpr auto element_size_c =
                    sizeof(typename smm::StringHeader::value_type);
                //read body of the string
                FarAddress data_block =
                    str_addr.as_far_address()
                    + static_cast<segment_off_t>(
                        offsetof(smm::StringHeader, buffer) + offset * element_size_c);

                segment_pos_t size = std::min(
                    length, 
                    head._size - offset //it is safe since I've already check: `offset < head->_size`
                );
                
                if (size)
                {
                    auto prev_size = dest.size();
                    dest.resize(prev_size + size);
                    auto memory = dest.data() + prev_size;
                    _segment_manager.read(
                            data_block, reinterpret_cast<std::uint8_t*>(memory), 
                            sizeof(element_t) * size);
                }
                return size;
            }
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
            smm::StringHeader head;
            view(_segment_manager, str_addr, head);
            if (offset >= head._size)
                return 0;
            //non-empty
            constexpr auto element_size_c =
                sizeof(typename smm::StringHeader::value_type);
            //read body of the string
            FarAddress data_block =
                str_addr
                + static_cast<segment_off_t>(
                    offsetof(smm::StringHeader, buffer) + offset * element_size_c);

            segment_pos_t size = std::min(
                length, 
                head._size - offset //it is safe since I've already check: `offset < head->_size`
            );
            
            if (!size)
                return 0;
            constexpr segment_pos_t buffer_size_c = 256;
            typename smm::StringHeader::value_type buffer[buffer_size_c];
            segment_pos_t result = 0;
            while(size)
            {
                auto to_read = std::min(size, buffer_size_c);
                _segment_manager.read(
                    data_block, reinterpret_cast<std::uint8_t*>(buffer), 
                    sizeof(smm::StringHeader::value_type) * to_read);
                data_block += to_read;
                size -= to_read;
                for (auto i = 0; i < to_read; ++i, ++result)
                {
                    if (!out_control(buffer[i]))
                        return result;
                }
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
            if(!str_addr.is_short_str())
            {
                return get(str_addr.as_far_address(), out_control, offset, length);
            }
            //
            if (offset >= str_addr.short_size())
                return 0;
            segment_pos_t result = 0;
            for (auto c: str_addr.as_str().substr(offset, length))
            {
                if(!out_control(c))
                    break;
                ++result;
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
            if(!str_addr.is_short_str())
            {
                return get(str_addr.as_far_address(), out, offset, length);
            }
            if (offset >= str_addr.short_size())
                return 0;
            segment_pos_t result = 0;
            for (auto c: str_addr.as_str().substr(offset, length))
            {
                *out++ = c;
                ++result; 
            }
            return result;
        }

    private:
        SegmentManager& _segment_manager;
        HeapManagerSlot& _heap_mngr;

        /** Calculate memory size expected to persist string of length `expected_size` */
        static constexpr segment_pos_t memory_requirements(segment_pos_t expected_size) noexcept
        {
            return 
                OP::utils::memory_requirement<header_t>::requirement
                + (expected_size - 1) *sizeof(element_t) ;
        }
    };
    
}//endof namespace OP::vtm

#endif //_OP_VTM_STRINGMEMORYMANAGER__H_
