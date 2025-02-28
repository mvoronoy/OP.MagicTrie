#pragma once

#ifndef _OP_VTM_PERSISTEDREFERENCE__H_
#define _OP_VTM_PERSISTEDREFERENCE__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/SegmentHelper.h>

namespace OP::vtm
{
    /** 
    *   Wraps operations to de-reference plain (std::is_standard_layout_v == true) object from FarAddress.
    * Since this type is plain too it can be helpful to have this filed instead of explicit FarAddress
    */
    template <class T>
    struct PersistedReference
    {
        static_assert(std::is_standard_layout_v<T>, "only standard-layout allowed in persisted hash-table");

        using element_t = T;

        constexpr explicit PersistedReference(FarAddress aadr) noexcept
            : _address(aadr)
        {}
        
        constexpr PersistedReference() noexcept
            : _address{}
        {}
        
        template <class TSegmentManager>
        T* ref(TSegmentManager& manager)
        {
            return manager.template wr_at<T>(_address);
        }
        
        constexpr bool is_nil() const noexcept
        {
            return _address.is_nil();
        }

        template <class TSegmentManager, class ... Args>
        T* construct(TSegmentManager& manager, Args&& ... args) const
        {
            return new (manager.template wr_at<T>(_address)) T(std::forward<Args>(args)...);
        }
        
        constexpr FarAddress address() const noexcept
        {
            return _address;
        }

        void address(FarAddress new_addr) noexcept
        {
            _address = new_addr;
        }

    private:
        FarAddress _address;

    };

    template <class T>
    struct PersistedArray
    {
        using element_t = T;
        FarAddress address;
        
        explicit PersistedArray(FarAddress aadr) noexcept
            : address(aadr)
        {}

        constexpr PersistedArray() noexcept
            : address{}
        {}
        
        constexpr static segment_pos_t memory_requirement(segment_pos_t expected_size) noexcept
        {
            return OP::utils::memory_requirement<element_t>::requirement * (expected_size );
        }

        bool is_null() const
        {
            return address == SegmentDef::far_null_c;
        }
        
        template <class TSegmentManager>
        element_t* ref(TSegmentManager& manager, segment_pos_t capacity) const
        {
            return array_accessor<element_t>(
                    resolve_segment_manager(manager), address, capacity).template at<T>(0);
        }
        
        template <class TSegmentManager>
        ReadonlyAccess<T> cref(TSegmentManager& manager, segment_pos_t capacity) const
        {
            return array_view<T>(resolve_segment_manager(manager), address, capacity);
        }

        /** reference single element of array by index, no boundary check is performed*/
        template <class TSegmentManager>
        element_t& ref_element(TSegmentManager& manager, segment_pos_t at) const
        {
            return *resolve_segment_manager(manager).template wr_at<element_t>(
                element_address(at));
        }

        FarAddress element_address(segment_pos_t at) const
        {
            segment_pos_t offset = OP::utils::memory_requirement<element_t>::requirement * at;
            return address + offset;
        }
    };
    // Same as prev but Read-Only mode supposed
    template <class T>
    struct ConstantPersistedArray
    {
        using element_t = T;
        FarAddress address;
        
        explicit ConstantPersistedArray(FarAddress aadr) noexcept
            : address(aadr)
        {
        }

        ConstantPersistedArray() noexcept
            : address{}
        {}
        
        constexpr static segment_pos_t memory_requirement(segment_pos_t expected_size) noexcept
        {
            return OP::utils::memory_requirement<element_t>::requirement * (expected_size );
        }

        bool is_null() const noexcept
        {
            return address == SegmentDef::far_null_c;
        }
        
        template <class TSegmentManager>
        ReadonlyAccess<T> ref(TSegmentManager& manager, segment_pos_t capacity) const
        {
            return cref(manager, capacity);

        }

        template <class TSegmentManager>
        ReadonlyAccess<T> cref(TSegmentManager& manager, segment_pos_t capacity) const
        {
            return array_view<T>(resolve_segment_manager(manager), address, capacity);
        }

        /** reference single element of array by index, no boundary check is performed*/
        template <class TSegmentManager>
        ReadonlyAccess<T> ref_element(TSegmentManager& manager, segment_pos_t at) const
        {
            segment_pos_t offset = OP::utils::memory_requirement<element_t>::requirement * at;
            return view<element_t>(resolve_segment_manager(manager), (address + offset));
        }

    };

    /**
    *   Wraps persisted array with access methods when array on the storage
    * contains leading field of size.
    * Since this type is plain too it can be helpful to use this as filed declaration 
    *   instead of explicit FarAddress
    */
    template <class T, class Size = segment_pos_t >
    struct PersistedSizedArray
    {
        static_assert(std::is_standard_layout_v<T>, "only  standard-layout allowed in persisted hash-table");

        using element_t = T;
        FarAddress address;
        
        struct Container
        {
            Size size;
            element_t data[1];
        };
        
        constexpr static segment_pos_t memory_requirement(Size expected_size)
        {
            return OP::utils::memory_requirement<Container>::requirement
                    + OP::utils::memory_requirement<element_t>::requirement * (expected_size - 1);
        }

        constexpr explicit PersistedSizedArray(FarAddress aadr) noexcept
            : address(aadr)
        {}

        constexpr PersistedSizedArray() noexcept
            : address{}
        {}

        constexpr bool is_null() const
        {
            return address == SegmentDef::far_null_c;
        }
        
        Size size(SegmentManager& manager) const
        {
            auto ro = view<Container>(manager, address);
            return ro->size;
        }

        Size& size_ref(SegmentManager& manager) const
        {
            return manager.template wr_at<Container>(address).size;
        }

        Container& ref(SegmentManager& manager) const
        {
            auto ro = view<Container>(manager, address);
            return *manager
                .writable_block(address, memory_requirement(ro->size))
                .template at<Container>(0);
        }
        Container& ref(SegmentManager& manager, segment_pos_t str_capacity) const
        {
            return *manager
                .writable_block(address, memory_requirement(str_capacity))
                .template at<Container>(0);
        }
        ReadonlyAccess<Container> cref(SegmentManager& manager) const
        {
            auto head = view<Container>(manager, address);
            return ReadonlyAccess<Container>(manager.readonly_block(
                address, memory_requirement(head->size)));
        }
    };

}//ns:OP::vtm

#endif //_OP_VTM_PERSISTEDREFERENCE__H_
