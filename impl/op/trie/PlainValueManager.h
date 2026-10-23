#ifndef _OP_TRIE_PLAINVALUEMANAGER__H_
#define _OP_TRIE_PLAINVALUEMANAGER__H_

#include <cstdint>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>

#include <op/trie/StringMemoryManager.h>
#include <op/trie/StoreConverter.h>

namespace OP::trie
{
    namespace details
    {
        template <class T, std::enable_if_t<std::is_invocable_v<T>, int> = 0>
        decltype(auto) forward_constructor_arg(T&& f) noexcept (std::is_nothrow_invocable_v<T>)
        {
            return f();
        }

        template <class T, std::enable_if_t<!std::is_invocable_v<T>, int> = 0>
        decltype(auto) forward_constructor_arg(T&& f) noexcept
        {
            return f;
        }

    }//ns:details

    /** 
    * Represent persistent storage for the plain values (the values that are 
    *   complaint with std::is_standard_layout_v<Payload>).
    * This storage uses special limit to detect if value small enough to fit
    * into reference occupied by FarAddress structure, if so no additional
    * allocation used. Otherwise storage requires 
    *
    *   \tparam Payload any data types that meets condition `std::is_standard_layout_v<Payload>==true`
    *   \tparam inline_byte_size_limit - byte size to allocate for storage. By default this 
    *       value equals to `sizeof(FarAddress)`
    */
    template <class Payload, size_t inline_byte_size_limit = sizeof(vtm::FarAddress)>
    struct PlainDataStorage
    {
        using this_t = PlainDataStorage<Payload, inline_byte_size_limit>;
        using payload_t = Payload;
        using FarAddress = vtm::FarAddress;

        constexpr static size_t inline_byte_size_limit_c = inline_byte_size_limit;

        static_assert(inline_byte_size_limit >= sizeof(FarAddress), 
            "inline_byte_size_limit must be equal or bigger than sizeof(FarAddress)");

        static_assert(std::is_standard_layout_v<Payload>, 
            "only standart-layout allowed in PlainDataStorage");

        //static_assert(std::is_standard_layout_v<this_t>, 
        //    "Self check on standart-layout failed");

        constexpr static bool big_payload_c = sizeof(payload_t) > inline_byte_size_limit_c;
        constexpr static size_t byte_size_c = inline_byte_size_limit_c;
        constexpr static size_t align_size_c = std::max(alignof(payload_t), alignof(FarAddress));

        alignas(align_size_c) std::byte _data[byte_size_c];
    };
    
    /** Implement simple algorithm of data storage if Payload small enough to fit to 
    *  `inline_byte_size_limit` size then it stores inline, otherwise additional memory
    *  is allocated using HeapManagerSlot
    *   \tparam Payload any data types that meets condition `std::is_standard_layout_v<Payload>==true`
    *   \tparam inline_byte_size_limit - byte size to allocate for storage. By default this 
    *       value equals to `sizeof(FarAddress)`
    */
    template <class Payload, size_t inline_byte_size_limit = sizeof(vtm::FarAddress)>
    struct PlainValueManager
    {
        using source_payload_t = Payload;
        using FarAddress = vtm::FarAddress;
        using payload_t = typename store_converter::Storage<source_payload_t>::storage_type_t;

        using data_storage_t = PlainDataStorage<payload_t, inline_byte_size_limit>;

        static_assert(std::is_standard_layout_v<data_storage_t>, 
            "PlainDataStorage<payload_t> must have standard layout");

        using storage_converter_t = store_converter::Storage<source_payload_t>; 

        static void allocate(TrieOptions& options, data_storage_t &storage)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                *std::launder(reinterpret_cast<FarAddress*>(storage._data)) = 
                    options.allocator_service().allocate_for<payload_t>();
            }
            else
            {
                //nothing to do, memory already available
                ::new (storage._data) payload_t{};
            }
        }

        /**
        * @tparam TAssign - either functor without arg `assign()` or value to assign on construct.
        */
        template <class TAssign>
        static auto make_new(TrieOptions& options, data_storage_t &storage, TAssign&& assign)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto address = options.allocator_service().allocate_for<payload_t>();
                auto ptr = options.memory_service().make_new<payload_t>(address);

                *std::launder(reinterpret_cast<FarAddress*>(storage._data)) = address;
                storage_converter_t::serialize(options,
                     details::forward_constructor_arg(std::forward<TAssign>(assign)),
                     *ptr
                );
                return ptr;
            }
            else
            {
                //nothing to do, memory already available
                auto *ptr = ::new (storage._data) payload_t{};
                storage_converter_t::serialize(options,
                     details::forward_constructor_arg(std::forward<TAssign>(assign)),
                     *ptr
                );
                return ptr;
            }
        }

        static void destroy(TrieOptions& options, data_storage_t &storage)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto& address = *std::launder(reinterpret_cast<FarAddress*>(storage._data));
                auto wr_block = options.memory_service().writable_block(
                    address,
                    utils::memory_requirement<payload_t>::requirement);
                auto& val = *wr_block.template at<payload_t>(0);
                storage_converter_t::destroy(options, val);
                options.allocator_service().deallocate(address);
            }
            else
            {
                auto& val = *std::launder(reinterpret_cast<payload_t*>(storage._data));
                storage_converter_t::destroy(options, val);
            }
        }

        template <class FRawDataCallback,
            //specialization to assign data from callback(value_holder)
            std::enable_if_t<std::is_invocable_v<FRawDataCallback, payload_t&>, int> = 0>
        static void raw(TrieOptions& options, data_storage_t &storage, FRawDataCallback&& payload_callback)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto& address = *std::launder(reinterpret_cast<FarAddress*>(storage._data));
                auto wr_data = options.memory_service().access<payload_t>(address);
                payload_callback(*wr_data);
            }
            else
            {
                payload_callback( 
                    *std::launder(reinterpret_cast<payload_t*>(storage._data))
                );
            }
        }

        template <class TData,
            //specialization to assign data directly
            std::enable_if_t<std::is_convertible_v<TData, payload_t>, int> = 0
        >
        static void raw(TrieOptions& options, data_storage_t &storage, TData&& value)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto& address = *std::launder(reinterpret_cast<FarAddress*>(storage._data));
                auto wr_data = options.memory_service().access<payload_t>(address);
                *wr_data = std::forward<TData>(value);
            }
            else
            {
                *std::launder(reinterpret_cast<payload_t*>(storage._data)) = std::forward<TData>(value);
            }
        }

        template <class FDataCallback>
        static auto rawc(TrieOptions& options, const data_storage_t &storage, FDataCallback&& payload_callback)
        {
            if constexpr (data_storage_t::big_payload_c)
            {
                const auto& address = *std::launder(reinterpret_cast<const FarAddress*>(storage._data));
                auto ro_data = options.memory_service().view<payload_t>(address);
                return payload_callback(*ro_data);
            }
            else
            {
                return payload_callback(
                    *std::launder(reinterpret_cast<const payload_t*>(storage._data)));
            }
        }

    };

    
}//ns:OP::trie
    
#endif //_OP_TRIE_PLAINVALUEMANAGER__H_
