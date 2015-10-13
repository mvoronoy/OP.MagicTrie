#ifndef _OP_TRIE_MEMORYBLOCK__H_
#define _OP_TRIE_MEMORYBLOCK__H_

#include <OP/trie/SegmentManager.h>

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
            } */
            MemoryBlockHeader* set_prev(MemoryBlockHeader* prev)
            {
                prev_block_offset(static_cast<segment_off_t>((std::uint8_t*)prev - (std::uint8_t*)this));
                return this;
            }
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
            /**Split this block and return new created.
            * @param byte_size - user needs byte size (not real_size)
            *@return either this if byte_size consumes entire free-space or new block that is part of this block free space
            */
            MemoryBlockHeader *split_this(SegmentManager& segment_manager, far_pos_t this_pos, segment_pos_t byte_size)
            {
                assert(_free);
                OP_CONSTEXPR segment_pos_t mbh_size_align = aligned_sizeof<MemoryBlockHeader>(SegmentHeader::align_c);
                byte_size = align_on(byte_size, SegmentHeader::align_c);
                assert(size() >= byte_size);
                if ((nav._size - byte_size) < (mbh_size_align + SegmentHeader::align_c))
                {//use entire this 
                    //extend transaction region
                    segment_manager
                        .writable_block(FarPosHolder(this_pos + mbh_size_align), byte_size);
                    _free = 0;
                    return this;
                }
                byte_size += mbh_size_align;
                //alloc new segment at the end of current
                segment_pos_t offset = size() - byte_size;
                //allocate new block inside this memory but mark this free
                auto access = segment_manager
                    .writable_block(FarPosHolder((this_pos+mbh_size_align) + offset), byte_size);
                    
                MemoryBlockHeader * result = new (access.pos()) MemoryBlockHeader(emplaced_t());
                result
                    ->size(byte_size - mbh_size_align)
                    ->set_prev(this)
                    ->set_free(false)
                    ->set_has_next(this->has_next())
                    ->my_segement(segment_of_far(this_pos))
                    ;
                if (this->has_next())
                {
                    MemoryBlockHeader *next = segment_manager.wr_at<MemoryBlockHeader>(FarPosHolder(this_pos + real_size()));
                    next->set_prev(result);
                }
                this
                    ->size(offset)
                    ->set_has_next(true)
                    ;

                return result;
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
        struct FreeMemoryBlock
        {
            /**Special constructor for objects allocated in existing memory*/
            FreeMemoryBlock(emplaced_t)
            {
                down = SegmentDef::far_null_c;
            }
            far_pos_t down;
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
            /**
            * Giving MemoryBlockHeader resolve placed inside FreeMemoryBlock. Important that source block must be 
            * free
            */
            static const FreeMemoryBlock* of_block(const MemoryBlockHeader* mem)
            {
                assert(mem->is_free());//memory block must be free to contain inner FreeMemoryBlock
                return reinterpret_cast<const FreeMemoryBlock*>(mem->memory());
            }
        };
        struct FreeMemoryBlockTraits
        {
            typedef size_t key_t;
            typedef far_pos_t ptr_t;
            
            FreeMemoryBlockTraits(SegmentManager *segment_manager):
                _segment_manager(segment_manager)
            {
            }
            static inline const ptr_t eos()
            {
                return SegmentDef::far_null_c;
            }
            static bool is_eos(ptr_t pt)
            {
                return pt == eos();
            }
            /**Compare 2 FreeMemoryBlock by the size*/
            static bool less(key_t left, key_t right)
            {
                return left < right;
            }
            void write_lock(size_t idx)
            {
            }
            void write_unlock(size_t idx)
            {
            }
            key_t key(ptr_t ptr) const
            {
                const FreeMemoryBlock* inst = 
                    _segment_manager
                        ->readonly_block(FarPosHolder(ptr), sizeof(FreeMemoryBlock))
                        .at<FreeMemoryBlock>(0);
                return inst->get_header()->nav._size;
            }
    
            /*const ptr_t& next(ptr_t ref) const
            {
                const FreeMemoryBlock* inst = 
                    _segment_manager->readonly_block(ref, sizeof(FreeMemoryBlock)).at<FreeMemoryBlock>(0);
                return inst->down;
            }*/
            ptr_t& next(ptr_t ref) 
            {
                FreeMemoryBlock* inst = 
                    _segment_manager->writable_block(FarPosHolder(ref), sizeof(FreeMemoryBlock)).at<FreeMemoryBlock>(0);
                return inst->down;
            }
            void set_next(ptr_t mutate, ptr_t next)
            {
                FreeMemoryBlock* inst = 
                    _segment_manager->writable_block(FarPosHolder(mutate), sizeof(FreeMemoryBlock)).at<FreeMemoryBlock>(0);
                inst->down = next;
            }
            
            static ptr_t deref(ptr_t n)
            {
                return n;
            }
        private:
            SegmentManager *_segment_manager;
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
