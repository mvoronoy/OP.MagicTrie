#pragma once

#ifndef _OP_VTM_MEMORYBLOCK__H_
#define _OP_VTM_MEMORYBLOCK__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/Skplst.h>

namespace OP::vtm
{

    struct FreeMemoryBlockTraits
    {
        using target_t = FreeMemoryBlock ;
        using key_t = size_t ;
        using reference_t = FreeMemoryBlock& ;
        using ptr_t = FreeMemoryBlock* ;
        using const_ptr_t = const FreeMemoryBlock* ;
        using const_reference_t = const FreeMemoryBlock& ;

        /**
        * @param entry_list_pos position that points to skip-list root
        */
        explicit FreeMemoryBlockTraits(SegmentManager& segment_manager) noexcept
            : _segment_manager(segment_manager)
            , _smallest(slots_c)//smallest block = 32
            , _largest(_segment_manager.segment_size())
        {
        }

        /**Compare 2 FreeMemoryBlock by the size*/
        constexpr static bool less(key_t left, key_t right) noexcept
        {
            return left < right;
        }


        reference_t deref(FarAddress n)
        {
            return *_segment_manager.wr_at<FreeMemoryBlock>(n);
        }

    private:
        constexpr static const size_t slots_c = sizeof(std::uint32_t) << 3;

        SegmentManager& _segment_manager;
        const size_t _smallest;
        const size_t _largest;
    };

} //endof OP::vtm

#endif //_OP_VTM_MEMORYBLOCK__H_
