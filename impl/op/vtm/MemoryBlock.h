#ifndef _OP_TRIE_MEMORYBLOCK__H_
#define _OP_TRIE_MEMORYBLOCK__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryBlockHeader.h>
#include <op/vtm/Skplst.h>
namespace OP
{

    namespace trie
    {
        
        /**
        *   When Memory block is released it have to be placed to special re-cycling container.
        *   This structure is placed inside MemoryBlockHeader (so doesn't consume space) to support 
        *   sorted navigation between free memory blocks.
        */
        struct FreeMemoryBlock : public OP::trie::ForwardListBase
        {
            /**Special constructor for objects allocated in existing memory*/
            FreeMemoryBlock(emplaced_t)
            {
            }
            /*const MemoryBlockHeader* get_header() const
            {
                return reinterpret_cast<const MemoryBlockHeader*>(
                    reinterpret_cast<const std::uint8_t*>(this) - aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c)
                    );
            }*/
            /**
            *   Taking this far address convert it to far address of associated MemoryBlockHeader
            */
            static far_pos_t get_header_addr(far_pos_t this_addr)
            {
                return this_addr - OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
            }
            /**
            *   Taking this far address convert it to far address of associated MemoryBlockHeader
            */
            static far_pos_t get_addr_by_header(far_pos_t header_addr)
            {
                return header_addr + OP::utils::aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
            }
            
        };
        struct FreeMemoryBlockTraits
        {
            typedef FreeMemoryBlock target_t;
            typedef size_t key_t;
            typedef far_pos_t pos_t;
            typedef FreeMemoryBlock& reference_t;
            typedef FreeMemoryBlock* ptr_t;
            typedef const FreeMemoryBlock* const_ptr_t;
            typedef const FreeMemoryBlock& const_reference_t;

            /**
            * @param entry_list_pos position that points to skip-list root
            */
            FreeMemoryBlockTraits(SegmentManager *segment_manager):
                _segment_manager(segment_manager),
                _low(32),//smallest block 
                _high(segment_manager->segment_size())
            {
            }
            static inline const pos_t eos()
            {
                return SegmentDef::far_null_c;
            }
            static bool is_eos(pos_t pt)
            {
                return pt == eos();
            }
            /**Compare 2 FreeMemoryBlock by the size*/
            static bool less(key_t left, key_t right)
            {
                return left < right;
            }
            
            size_t entry_index(key_t key) const
            {
                size_t base = 0;
                const size_t low_strat = 256;
                if (key < low_strat)
                    return key * 3 / low_strat;
                base = 3;
                key -= low_strat;
                const size_t mid_strat = 4096;
                if (key < mid_strat)/*Just an assumption about biggest payload stored in virtual memory*/
                    return base + ((key*(slots_c/2/*aka 16*/))/(mid_strat));
                base += slots_c / 2;
                key -= mid_strat;
                size_t result = base + (key * (slots_c - 3 - slots_c / 2/*aka 13*/)) / _high;
                if (result >= slots_c)
                    return slots_c - 1;
                return result;
            }
            /*key_t key(const_reference_t fb) const
            {
                return fb.get_header()->nav._size;
            }  */
    
            static const pos_t& next(const_reference_t ref)
            {
                return ref.next;
            }
            static pos_t& next(reference_t ref)
            {
                return ref.next;
            }
            void set_next(reference_t mutate, pos_t next)
            {
                mutate.next = next;
            }
            
            reference_t deref(pos_t n)
            {
                return *_segment_manager->wr_at<FreeMemoryBlock>(FarAddress(n));
            }
        private:
            static OP_CONSTEXPR(const) size_t slots_c = sizeof(std::uint32_t) << 3;
            SegmentManager *_segment_manager;
            
            const size_t _low;
            const size_t _high;
        };

    } //endof trie
} //endof OP
#endif //_OP_TRIE_MEMORYBLOCK__H_
