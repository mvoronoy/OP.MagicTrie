#pragma once

#ifndef _OP_VTM_MEMORYBLOCK__H_
#define _OP_VTM_MEMORYBLOCK__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryBlockHeader.h>
#include <op/vtm/Skplst.h>

namespace OP::vtm
{

    struct FreeMemoryBlockTraits
    {
        using target_t = FreeMemoryBlock ;
        using key_t = size_t ;
        using pos_t = far_pos_t ;
        using reference_t = FreeMemoryBlock& ;
        using ptr_t = FreeMemoryBlock* ;
        using const_ptr_t = const FreeMemoryBlock* ;
        using const_reference_t = const FreeMemoryBlock& ;

        /**
        * @param entry_list_pos position that points to skip-list root
        */
        explicit FreeMemoryBlockTraits(SegmentManager* segment_manager) noexcept
            : _segment_manager(segment_manager),
            _smallest(slots_c),//smallest block = 32
            _largest(segment_manager->segment_size())
        {
        }

        constexpr static inline pos_t eos() noexcept
        {
            return SegmentDef::far_null_c;
        }

        constexpr static bool is_eos(pos_t pt) noexcept
        {
            return pt == eos();
        }

        /**Compare 2 FreeMemoryBlock by the size*/
        constexpr static bool less(key_t left, key_t right) noexcept
        {
            return left < right;
        }

        constexpr size_t entry_index(key_t key) const noexcept
        {
            size_t base = 0;
            const size_t low_strat = 256;
            if (key < low_strat)
                return key * 3 / low_strat;
            base = 3;
            key -= low_strat;
            const size_t mid_strat = 4096;
            if (key < mid_strat)/*Just an assumption about biggest payload stored in virtual memory*/
                return base + ((key * (_smallest / 2/*aka 16*/)) / (mid_strat));
            base += _smallest / 2;
            key -= mid_strat;
            size_t result = base + (key * (_smallest - 3 - _smallest / 2/*aka 13*/)) / _largest;
            if (result >= _smallest)
                return _smallest - 1;
            return result;
        }

        static const pos_t& next(const_reference_t ref) noexcept
        {
            return ref.next;
        }
        static pos_t& next(reference_t ref) noexcept
        {
            return ref.next;
        }

        void set_next(reference_t mutate, pos_t next) noexcept
        {
            mutate.next = next;
        }

        reference_t deref(pos_t n)
        {
            return *_segment_manager->wr_at<FreeMemoryBlock>(FarAddress(n));
        }

    private:
        constexpr static const size_t slots_c = sizeof(std::uint32_t) << 3;

        SegmentManager* _segment_manager;
        const size_t _smallest;
        const size_t _largest;
    };

} //endof OP::vtm

#endif //_OP_VTM_MEMORYBLOCK__H_
