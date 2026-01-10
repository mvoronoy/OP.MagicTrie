#ifndef _OP_TRIE_PLAINVALUEMANAGER__H_
#define _OP_TRIE_PLAINVALUEMANAGER__H_

#include <cstdint>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>
#include <op/vtm/StringMemoryManager.h>

#include <op/trie/StoreConverter.h>

namespace OP::trie
{

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
        using payload_t = typename store_converter::Storage<source_payload_t>::storage_type_t;

        using data_storage_t = PlainDataStorage<payload_t, inline_byte_size_limit>;
        using storage_converter_t = store_converter::Storage<source_payload_t>; 

        template <class TSegmentTopology>
        static void allocate(TSegmentTopology& topology, data_storage_t &storage)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto& heap_manager = topology.template slot<vtm::HeapManagerSlot> ();
                auto address = heap_manager.allocate(
                    memory_requirement<payload_t>::requirement);
                *std::launder(reinterpret_cast<FarAddress*>(storage._data)) = address;
            }
            else
            {
                //nothing to do, memory already available
                ::new (storage._data) payload_t{};
            }
        }

        template <class TSegmentTopology>
        static void destroy(TSegmentTopology& topology, data_storage_t &storage)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto& heap_manager = topology.template slot<vtm::HeapManagerSlot>();
                auto& address = *std::launder(reinterpret_cast<FarAddress*>(storage._data));
                auto wr_block = resolve_segment_manager(topology).writable_block(address,
                                        memory_requirement<payload_t>::requirement);
                auto& val = *wr_block.template at<payload_t>(0);
                storage_converter_t::destroy(topology, val);
                heap_manager.deallocate(address);
            }
            else
            {
                auto& val = *std::launder(reinterpret_cast<payload_t*>(storage._data));
                storage_converter_t::destroy(topology, val);
            }
        }

        template <class TSegmentTopology, class FRawDataCallback>
        static void raw(TSegmentTopology& tsegment, data_storage_t &storage, FRawDataCallback payload_callback)
        {
            if constexpr( data_storage_t::big_payload_c )
            {
                auto& address = *std::launder(reinterpret_cast<FarAddress*>(storage._data));
                auto wr_data = OP::vtm::resolve_segment_manager(tsegment)
                    .writable_block(address, memory_requirement< payload_t>::requirement);
                payload_callback(*wr_data.template at<payload_t>(0));
            }
            else
            {
                payload_callback( 
                    *std::launder(reinterpret_cast<payload_t*>(storage._data))
                    );
            }
        }

        template <class TSegmentTopology, class FDataCallback>
        static auto rawc(TSegmentTopology& tsegment, const data_storage_t &storage, FDataCallback payload_callback)
        {
            if constexpr (data_storage_t::big_payload_c)
            {
                const auto& address = *std::launder(reinterpret_cast<const FarAddress*>(storage._data));
                auto ro_data = OP::vtm::resolve_segment_manager(tsegment)
                    .readonly_block(address, memory_requirement< payload_t>::requirement);
                return payload_callback(*ro_data.template at<payload_t>(0));
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
