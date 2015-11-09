#ifndef _OP_TRIE_MEMORYBLOCK__H_
#define _OP_TRIE_MEMORYBLOCK__H_

#include <OP/trie/SegmentManager.h>
#include <op/trie/Skplst.h>
namespace OP
{

    namespace trie
    {
    
        /**
        * Describe block of memory
        * Each block is palced to list of all memory blocks and managed by `next`, `prev` navigation methods.
        */
        struct MemoryBlockHeader
        {
            /**Special constructor for placement new*/
            MemoryBlockHeader(emplaced_t) :
                _free(1),
                _has_next(0)
            {
                nav._size = SegmentDef::eos_c;
                nav._prev_block_offset = SegmentDef::eos_c;
                _signature = 0x3757;
            }
            /** Constructor for find purpose only*/
            MemoryBlockHeader(segment_pos_t user_size)
            {
                nav._size = user_size;
            }
            bool check_signature() const
            {
                return _signature == 0x3757;
            }
            segment_pos_t size() const
            {
                return nav._size;
            }
            /**interrior size of block alligned to Segment::align */
            MemoryBlockHeader* size(segment_pos_t byte_size)
            {
                nav._size = (byte_size);
                return this;
            }
            /**@return real occupied bytes size()+sizeof(*this)*/
            segment_pos_t real_size() const
            {
                return size() + aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
            }
            segment_idx_t my_segement() const
            {
                return nav._my_segment;
            }
            MemoryBlockHeader* my_segement(segment_idx_t segment) 
            {
                nav._my_segment = segment;
                return this;
            }
            /*!@:To delete MemoryBlockHeader* next() const
            {
                assert(has_next());

                return reinterpret_cast<MemoryBlockHeader*>(memory() + size());
            }
            MemoryBlockHeader* prev() const
            {
                assert(prev_block_offset() < 0 && prev_block_offset() != ~0u);

                return reinterpret_cast<MemoryBlockHeader*>(
                    ((std::uint8_t*)this) + prev_block_offset());
            } 
            MemoryBlockHeader* set_prev(MemoryBlockHeader* prev)
            {
                prev_block_offset(static_cast<segment_off_t>((std::uint8_t*)prev - (std::uint8_t*)this));
                return this;
            }*/
            segment_off_t prev_block_offset() const
            {
                return nav._prev_block_offset;
            }
            MemoryBlockHeader* prev_block_offset(segment_off_t offset)
            {
                nav._prev_block_offset = offset;
                return this;
            }
            
            bool is_free() const
            {
                return this->_free;
            }
            MemoryBlockHeader* set_free(bool flag)
            {
                this->_free = flag ? 1 : 0;
                return this;
            }
            bool has_next() const
            {
                return this->_has_next;
            }
            MemoryBlockHeader* set_has_next(bool flag)
            {
                this->_has_next = flag ? 1 : 0;
                return this;
            }
            /**
            *   Optionally split this block. This depends on available size of memory, if block big enough then
            * this operation makes 2 adjacent blocks  - one for queried bytes and another with rest of memory.
            * If current block is to small to fit queried `byte_size+ sizeof(MemoryBlockHeader)` then entire this
            * should be used
            * @param byte_size - user needs byte size (not real_size - without sizeof(MemoryBlockHeader)) Must be alligned on SegmentDef::align_c
            * @return  a writable memory range for user purpose (it is what about of MemoryBlockHeader)
            */
            FarAddress split_this(SegmentManager& segment_manager, far_pos_t this_pos, segment_pos_t byte_size)
            {
                assert(_free);
                OP_CONSTEXPR(const) segment_pos_t mbh_size_align = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                assert(size() >= byte_size);
                //alloc new segment at the end of current
                segment_pos_t offset = size() - byte_size;
                //allocate new block inside this memory but mark this as a free
                FarAddress access_pos(this_pos + (/*mbh_size_align + */offset));
                auto access = segment_manager
                    .writable_block(access_pos, mbh_size_align/*byte_size - don't need write entire block*/, WritableBlockHint::new_c);
                    
                MemoryBlockHeader * result = new (access.pos()) MemoryBlockHeader(emplaced_t());
                result
                    ->size(byte_size)
                    ->prev_block_offset(FarAddress(this_pos).diff(access_pos))
                    ->set_free(false)
                    ->set_has_next(this->has_next())
                    ->my_segement(segment_of_far(this_pos))
                    ;
                if (this->has_next())
                {
                    FarAddress next_pos(this_pos + real_size());
                    MemoryBlockHeader *next = segment_manager.wr_at<MemoryBlockHeader>(next_pos);
                    next->prev_block_offset(access_pos.diff(next_pos));
                }
                this
                    ->size(offset - mbh_size_align)
                    ->set_has_next(true)
                    ;
                //user memory block starts after access_pos + mbh
                return access_pos+mbh_size_align;
            }
            /**Glues this with previous block*/
            /*!@:To delete MemoryBlockHeader *glue_prev()
            {
                MemoryBlockHeader *prev = this->prev();
                assert(prev->is_free());

                if (this->has_next())
                    this->next()->set_prev(prev);
                prev
                    ->size(prev->size() + this->real_size())
                    ->set_has_next(this->has_next())
                    ;
                return prev;
            }
            */
            std::uint8_t* memory() const
            {
                return ((std::uint8_t*)this) + aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
            }
            void _check_integrity() const
            {
                assert(this->check_signature());
                /*@!:To delete if (has_next())
                {
                    assert(this < this->next());
                    assert(this->next()->check_signature());
                }
                if (SegmentDef::eos_c != this->prev_block_offset())
                {
                    MemoryBlockHeader* prev = this->prev();
                    assert(prev->check_signature());
                } */
            }

            std::uint32_t _free : 1;
            std::uint32_t _has_next : 1;
            std::uint32_t _signature : 30;
            struct data_t
            {
                segment_idx_t _my_segment;
                segment_pos_t _size;
                segment_off_t _prev_block_offset;
            };
            union
            {
                data_t nav;
                mutable std::uint8_t raw[1];
            };
            MemoryBlockHeader(const MemoryBlockHeader&) = delete;
        };
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
            const MemoryBlockHeader* get_header() const
            {
                return reinterpret_cast<const MemoryBlockHeader*>(
                    reinterpret_cast<const std::uint8_t*>(this) - aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c)
                    );
            }
            /**
            *   Taking this far address convert it to far address of associated MemoryBlockHeader
            */
            static far_pos_t get_header_addr(far_pos_t this_addr)
            {
                return this_addr - aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
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
            key_t key(const_reference_t fb) const
            {
                return fb.get_header()->nav._size;
            }
    
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
            const_reference_t const_deref(pos_t n) const
            {
                return *_segment_manager->ro_at<FreeMemoryBlock>(FarAddress(n));
            }
        private:
            OP_CONSTEXPR(static const) size_t slots_c = sizeof(std::uint32_t) << 3;
            SegmentManager *_segment_manager;
            
            const size_t _low;
            const size_t _high;
        };

        struct FreeMemoryBlockPtrLess
        {
            bool operator()(const FreeMemoryBlock*left, const FreeMemoryBlock*right)const
            {
                return left->get_header()->real_size() < right->get_header()->real_size();
            }
        };
    } //endof trie
} //endof OP
#endif //_OP_TRIE_MEMORYBLOCK__H_
