#ifndef _OP_TRIE_TYPEHELPER__H_
#define _OP_TRIE_TYPEHELPER__H_

#include <op/common/typedefs.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/SegmentHelper.h>

namespace OP
{
    namespace trie
    {
        using namespace OP::vtm;
        /** 
        *   Wraps operations to de-reference plain (std::is_standard_layout_v == true) object from FarAddress.
        * Since this type is plain too it can be helpfull to have this filed instead of explicit FarAddress
        */
        template <class T>
        struct PersistedReference
        {
            static_assert(std::is_standard_layout_v<T>, "only  standart-layout alllowed in persisted hash-table");

            using element_t = T;
            FarAddress address;

            explicit PersistedReference(FarAddress aadr)
                : address(aadr)
            {}
            PersistedReference()
                : address{}
            {}
            template <class TSegmentManager>
            T* ref(TSegmentManager& manager)
            {
                return manager.OP_TEMPL_METH(wr_at)<T>(address);
            }
            bool is_null() const
            {
                return address == SegmentDef::far_null_c;
            }
            template <class TSegmentManager, class ... Args>
            T* construct(TSegmentManager& manager, Args&& ... args)
            {
                return new (manager.OP_TEMPL_METH(wr_at)<T>(address)) T(std::forward<Args>(args)...);
            }
        };
        template <class T>
        struct PersistedArray
        {
            using element_t = T;
            FarAddress address;
            
            explicit PersistedArray(FarAddress aadr)
                : address(aadr)
            {}
            PersistedArray()
                : address{}
            {}
            
            constexpr static segment_pos_t memory_requirement(segment_pos_t expected_size)
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
                segment_pos_t offset = OP::utils::memory_requirement<element_t>::requirement * at;
                return *resolve_segment_manager(manager).OP_TEMPL_METH(wr_at)<element_t>(address + offset);
            }

        };
        // Same as prev but Read-Only mode supposed
        template <class T>
        struct ConstantPersistedArray
        {
            using element_t = T;
            FarAddress address;
            
            explicit ConstantPersistedArray(FarAddress aadr)
                : address(aadr)
            {}
            ConstantPersistedArray()
                : address{}
            {}
            
            constexpr static segment_pos_t memory_requirement(segment_pos_t expected_size)
            {
                return OP::utils::memory_requirement<element_t>::requirement * (expected_size );
            }

            bool is_null() const
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
        * Since this type is plain too it can be helpfull to use this as filed declaration 
        *   instead of explicit FarAddress
        */
        template <class T, class Size = segment_pos_t >
        struct PersistedSizedArray
        {
            static_assert(std::is_standard_layout_v<T>, "only  standart-layout alllowed in persisted hash-table");

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
                return manager.OP_TEMPL_METH(wr_at)<Container>(address).size;
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

    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_TYPEHELPER__H_
