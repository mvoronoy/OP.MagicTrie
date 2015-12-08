#ifndef _OP_TR_SEGMENTMANAGER__H_
#define _OP_TR_SEGMENTMANAGER__H_

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER


#include <type_traits>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/thread.hpp>

#include <mutex>

#include <fstream>
#include <string>
#include <op/trie/Exceptions.h>
#include <op/trie/Range.h>
#include <op/trie/CacheManager.h>
#include <op/trie/Transactional.h>
#include <op/trie/Utils.h>


namespace OP
{
    namespace trie
    {
        using namespace boost::interprocess;
        typedef std::uint32_t header_idx_t;
        typedef std::uint32_t segment_idx_t;
        /**position inside segment*/
        typedef std::uint32_t segment_pos_t;
        typedef std::int32_t segment_off_t;
        /**Combines together segment_idx_t (high part) and segment_pos_t (low part)*/
        typedef std::uint64_t far_pos_t;
        struct SegmentDef
        {
            static const segment_idx_t null_block_idx_c = ~0u;
            static const segment_pos_t eos_c = ~0u;
            static const far_pos_t far_null_c = ~0ull;
            enum
            {
                align_c = 16
            };
        };
        union FarAddress
        {
            far_pos_t address;
            struct
            {
                segment_pos_t offset;
                segment_idx_t segment;
            };
            FarAddress():
                offset(SegmentDef::eos_c), segment(SegmentDef::eos_c){}
            explicit FarAddress(far_pos_t a_address) :
                address(a_address){}
            FarAddress(segment_idx_t a_segment, segment_pos_t a_offset) :
                segment(a_segment),
                offset(a_offset){}
            operator far_pos_t() const
            {
                return address;
            }
            FarAddress operator + (segment_pos_t pos) const
            {
                assert(offset <= (~0u - pos)); //test overflow
                return FarAddress(segment, offset + pos);
            }
            /**Signed operation*/
            FarAddress operator + (segment_off_t a_offset) const
            {
                assert(
                    ( (a_offset < 0)&&(static_cast<segment_pos_t>(-a_offset) < offset) )
                    || ( (a_offset >= 0)&&(offset <= (~0u - a_offset) ) )
                    );

                return FarAddress(segment, offset + a_offset);
            }
            FarAddress& operator += (segment_pos_t pos)
            {
                assert(offset <= (~0u - pos)); //test overflow
                offset += pos;
                return *this;
            }
            /**Find signable distance between to holders on condition they belong to the same segment*/
            segment_off_t diff(const FarAddress& other) const
            {
                assert(segment == other.segment);
                return offset - other.offset;
            }
        };

        template <class T, class Y>
        OP_CONSTEXPR(OP_EMPTY_ARG) inline T align_on(T address, Y base)
        {
            auto align = address % base;
            return align ? (address + (base - align)) : address;
        }
        /**Rounds align down (compare with #align_on that rounds up)*/
        template <class T, class Y>
        OP_CONSTEXPR(OP_EMPTY_ARG) inline T ceil_align_on(T address, Y base)
        {
            return (address / base)*base;
        }
        template <class T, class Y>
        OP_CONSTEXPR(OP_EMPTY_ARG) inline segment_pos_t aligned_sizeof(Y base)
        {
            return align_on(static_cast<segment_pos_t>(sizeof(T)), base);
        }
        template <class T, class Y>
        inline bool is_aligned(T address, Y base)
        {
            return ((size_t)(address) % base) == 0;
        }
        /**get segment part from far address*/
        inline segment_idx_t segment_of_far(far_pos_t pos)
        {
            return static_cast<segment_idx_t>(pos >> 32);
        }
        /**get offset part from far address*/
        inline segment_pos_t pos_of_far(far_pos_t pos)
        {
            return static_cast<segment_pos_t>(pos);
        }
        
        /**Special marker to construct objects inplace*/
        struct emplaced_t{};

        struct SegmentManager;

        struct SegmentEventListener
        {
            typedef SegmentManager segment_manager_t;
            virtual void on_segment_allocated(segment_idx_t new_segment, segment_manager_t& manager){}
            virtual void on_segment_opening(segment_idx_t new_segment, segment_manager_t& manager){}
            /**
            *   Event is raised when segment is released (not deleted)
            */
            virtual void on_segment_releasing(segment_idx_t new_segment, segment_manager_t& manager){}
        };
        /**
        *   Emplaceable structure that resides at beggining of the each segment
        */
        struct SegmentHeader
        {
            enum
            {
                align_c = SegmentDef::align_c
            };
            SegmentHeader() 
            {}
            SegmentHeader(segment_pos_t segment_size) :
                segment_size(segment_size)
            {
                //static_assert(sizeof(Segment) == 8, "Segment must be sizeof 8");
                memcpy(signature, SegmentHeader::seal(), sizeof(signature));
            }
            bool check_signature() const
            {
                return 0 == memcmp(signature, SegmentHeader::seal(), sizeof(signature));
            }

            char signature[4];
            segment_pos_t segment_size;
            static const char * seal()
            {
                static const char* signature = "mgtr";
                return signature;
            }
        };
        struct SegmentOptions
        {
            SegmentOptions() :
                _memory_alignment(16)
            {
                segment_size(1);

            }
            segment_pos_t memory_alignment() const
            {
                return _memory_alignment;
            }
            SegmentOptions& memory_alignment(segment_pos_t memory_alignment)
            {
                _memory_alignment = memory_alignment;
            }
            /**
            * Segment is a big chunk of virtual memory where the all other memory blocks are allocated.
            * @param hint allows specify value of segment, but real value will be aligned on operation-system value of page-size
            */
            SegmentOptions& segment_size(segment_pos_t hint)
            {
                _segment_size = hint;
                return *this;
            }
            /**The same as segment_size but allows specify memory basing on heuristic - how many objects of type Ta and Tb will be allocated*/
            template <class ... Tx>
            SegmentOptions& heuristic_size(Tx&& ... restarg)
            {
                eval_size(std::forward<Tx>(restarg)...);
                return *this;
            }
            /**@return result size that obtained from user prefeence either of #segment_size(segment_pos_t hint) or #heuristic_size and alligned for OS-depended
            *   page size
            */
            segment_pos_t segment_size() const
            {
                auto r = _segment_size;

                size_t min_page_size = boost::interprocess::mapped_region::get_page_size();
                r = align_on(r, static_cast<segment_pos_t>(min_page_size));

                return r;
            }
            segment_pos_t raw_size() const
            {
                return _segment_size;
            }
        private:
            template <class Ta>
            void eval_size(Ta&& obj)
            {
                segment_size(_segment_size + static_cast<segment_pos_t>(obj(*this)));
            }
            void eval_size()
            {
            }
            template <class Ta, class ... Tx>
            void eval_size(Ta&& obj, Tx&& ... rest)
            {
                eval_size<Ta>(std::forward<Ta>(obj));
                eval_size(std::forward<Tx>(rest)...);
            }
            segment_pos_t _segment_size;
            
            segment_pos_t _memory_alignment;
        };
        /**Namespace place together utilities to evaluate size of segment in heuristic way. Each item from namespace can be an argument to SegmentOptions::heuristic_size*/
        namespace size_heuristic
        {
            /**Specify heuristic size to reserve 'n'-items array of 'T'. Used with SegmentOptions::heuristic_size. For example:
            *\code
            *   SegmentOptions options;
            *   ...
            *   options.heuristic_size(size_heuristic::of_array<int, 100>); //reserve int-array of 100 items
            *\endcode
            */
            template <class T, size_t n>
            inline size_t of_array(const SegmentOptions& previous)
            {
                return
                    align_on(sizeof(T)*n, previous.memory_alignment()) + previous.memory_alignment()/*for memory-control-structure*/;
            }
            template <class T>
            struct of_array_dyn
            {
                of_array_dyn(size_t count) :
                    _count(count)
                {

                }
                size_t operator ()(const SegmentOptions& previous) const
                {
                    return
                        align_on(sizeof(T)*_count, previous.memory_alignment()) + previous.memory_alignment()/*for memory-control-structure*/;
                }
            private:
                size_t _count;
            };
            /**Specify heuristic size to reserve 'n' assorted items of 'T'. Used with SegmentOptions::heuristic_size. For example:
            *\code
            *   SegmentOptions options;
            *   ...
            *   options.heuristic_size(size_heuristic::of_assorted<int, 100>); //reserve place for 100 ints (don't do it for small types because memory block consumes 16bytes+ of memory)
            *\endcode*/
            template <class T, size_t n = 1>
            inline size_t of_assorted(const SegmentOptions& previous)
            {
                return n*(aligned_sizeof<T>(previous.memory_alignment()) + previous.memory_alignment());
            }
            /**Increase total amount of already reserved bytes by some percentage value. Used with SegmentOptions::heuristic_size. For example:
            *\code
            *   SegmentOptions options;
            *   ...
            *   options.heuristic_size(size_heuristic::add_percentage(5)); //reserve +5% basing on already reserved memory
            *\endcode*/
            struct add_percentage
            {
                add_percentage(std::int8_t percentage) :
                    _percentage(percentage)
                {
                }
                size_t operator()(const SegmentOptions& previous) const
                {
                    return previous.raw_size() * _percentage / 100;
                }
            private:
                std::int8_t _percentage;
            };
        }
        struct SegmentHelper
        {
            friend struct SegmentManager;
            SegmentHelper(file_mapping &mapping, offset_t offset, std::size_t size) :
                _mapped_region(mapping, read_write, offset, size),
                _avail_bytes(0)
            {
            }
            SegmentHeader& get_segment() const
            {
                return *at<SegmentHeader>(0);
            }
            template <class T>
            T* at(segment_pos_t offset) const
            {
                auto *addr = reinterpret_cast<std::uint8_t*>(_mapped_region.get_address());
                //offset += align_on(s_i_z_e_o_f(Segment), Segment::align_c);
                return reinterpret_cast<T*>(addr + offset);
            }
            std::uint8_t* raw_space() const
            {
                return at<std::uint8_t>(aligned_sizeof<SegmentHeader>(SegmentHeader::align_c));
            }
            segment_pos_t available() const
            {
                return this->_avail_bytes;
            }
            //segment_pos_t allocate(segment_pos_t to_alloc)
            //{
            //    guard_t l(_free_map_lock);
            //    if (_avail_bytes < to_alloc)
            //        throw trie::Exception(trie::er_no_memory);
            //    auto found = _free_blocks.lower_bound(&MemoryBlockHeader(to_alloc));
            //    if (found == _free_blocks.end()) //there is free blocks, but compression is needed
            //        throw trie::Exception(trie::er_memory_need_compression);
            //    auto current_pair = *found;
            //    //before splittng remove block from map
            //    free_block_erase(found);
            //    auto new_block = current_pair->split_this(to_alloc);
            //    if (new_block != current_pair) //new block was allocated
            //    {
            //        free_block_insert(current_pair);
            //        _avail_bytes -= new_block->real_size();
            //    }
            //    else //when existing block is used it is not needed to use 'real_size' - because header alredy counted
            //        _avail_bytes -= new_block->size();
            //    return unchecked_to_offset(new_block->memory());
            //}
            //void deallocate(void *memblock)
            //{
            //    guard_t l(_free_map_lock);
            //    if (!is_aligned(memblock, Segment::align_c)
            //        || !check_pointer(memblock))
            //        throw trie::Exception(trie::er_invalid_block);
            //    std::uint8_t* pointer = reinterpret_cast<std::uint8_t*>(memblock);
            //    MemoryBlockHeader* header = reinterpret_cast<MemoryBlockHeader*>(
            //        pointer - aligned_sizeof<MemoryBlockHeader>(Segment::align_c));
            //    if (!header->check_signature() || header->is_free())
            //        throw trie::Exception(trie::er_invalid_block);
            //    //check if prev or next can be joined
            //    MemoryBlockHeader* to_merge = header;
            //    to_merge->set_free(true);
            //    for (;;)
            //    {
            //        if (to_merge->prev_block_offset() != SegmentDef::eos_c &&
            //            to_merge->prev()->is_free())
            //        { //previous block is also free, so 2 blocks can be merged together
            //            auto adjacent = find_by_ptr(to_merge->prev());
            //            _avail_bytes -= to_merge->prev()->size();//temporary deposit it from free space
            //            assert(adjacent != _free_blocks.end());
            //            free_block_erase(adjacent);
            //            to_merge = to_merge->glue_prev();
            //        }
            //        else if (to_merge->has_next() && to_merge->next()->is_free())
            //        {  //next block is also free - merge both
            //            auto adjacent = find_by_ptr(to_merge->next());
            //            _avail_bytes -= to_merge->next()->size();//temporary deposit it from free space
            //            assert(adjacent != _free_blocks.end());
            //            free_block_erase(adjacent);
            //            to_merge = to_merge->next()->glue_prev();
            //        }
            //        else
            //            break;
            //    }
            //    to_merge->set_free(true);
            //    _avail_bytes += to_merge->size();
            //    free_block_insert(to_merge);
            //}

            segment_pos_t to_offset(const void *memblock)
            {
                if (!check_pointer(memblock))
                    throw trie::Exception(trie::er_invalid_block);
                return unchecked_to_offset(memblock);
            }
            segment_pos_t unchecked_to_offset(const void *memblock)
            {
                return static_cast<segment_pos_t> (
                    reinterpret_cast<const std::uint8_t*>(memblock)-reinterpret_cast<const std::uint8_t*>(this->_mapped_region.get_address())
                    );
            }
            std::uint8_t* from_offset(segment_pos_t offset)
            {
                if (offset >= this->_mapped_region.get_size())
                    throw trie::Exception(trie::er_invalid_block);
                return reinterpret_cast<std::uint8_t*>(this->_mapped_region.get_address()) + offset;
            }
            void flush(bool assync = true)
            {
                _mapped_region.flush(0, 0, assync);
            }
            void _check_integrity()
            {
                //guard_t l(_free_map_lock);
                //MemoryBlockHeader *first = at<MemoryBlockHeader>(aligned_sizeof<Segment>(Segment::align_c));
                //size_t avail = 0, occupied = 0;
                //size_t free_block_count = 0;
                //MemoryBlockHeader *prev = nullptr;
                //for (;;)
                //{
                //    assert(first->prev_block_offset() < 0);
                //    if (prev)
                //        assert(prev == first->prev());
                //    else
                //        assert(SegmentDef::eos_c == first->prev_block_offset());
                //    prev = first;
                //    first->_check_integrity();
                //    if (first->is_free())
                //    {
                //        avail += first->size();
                //        assert(_free_blocks.end() != find_by_ptr(first));
                //        free_block_count++;
                //    }
                //    else
                //    {
                //        occupied += first->size();
                //        assert(_free_blocks.end() == find_by_ptr(first));
                //    }
                //    if (first->has_next())
                //        first = first->next();
                //    else break;
                //}
                //assert(avail == this->_avail_bytes);
                //assert(free_block_count == _free_blocks.size());
                ////occupied???
            }
        private:
            mapped_region _mapped_region;

            segment_pos_t _avail_bytes;
            /**Because free_map_t is ordered by block size there is no explicit way to find by pointer,
            this method just provide better than O(N) improvement*/
            //free_set_t::const_iterator find_by_ptr(MemoryBlockHeader* block) const
            //{
            //    auto result = _revert_free_map_index.find(block);
            //    if (result == _revert_free_map_index.end())
            //        return _free_blocks.end();
            //    return result->second;
            //    
            //}
            //void free_block_insert(MemoryBlockHeader* block)
            //{
            //    free_set_t::const_iterator ins = _free_blocks.insert(block);
            //    auto result = _revert_free_map_index.insert(revert_index_t::value_type(block, ins));
            //    assert(result.second); //value have to be unique
            //}
            //template <class It>
            //void free_block_erase(It&to_erase)
            //{
            //    auto block = *to_erase;
            //    _revert_free_map_index.erase(block);
            //    _free_blocks.erase(to_erase);
            //}
            /** validate pointer against mapped region range*/
            bool check_pointer(const void *ptr)
            {
                auto byte_ptr = reinterpret_cast<const std::uint8_t*>(ptr);
                auto base = reinterpret_cast<const std::uint8_t*>(_mapped_region.get_address());
                return byte_ptr >= base
                    && byte_ptr < (base + _mapped_region.get_size());
            }
        };
        typedef std::shared_ptr<SegmentHelper> segment_helper_p;
        
        struct MemoryRangeBase;

        struct BlockDisposer
        {
            virtual ~BlockDisposer() OP_NOEXCEPT = default;
            virtual void on_leave_scope(MemoryRangeBase& closing) OP_NOEXCEPT = 0;
        };
        struct MemoryRangeBase : public OP::Range<std::uint8_t *, segment_pos_t>
        {
            typedef OP::Range<std::uint8_t *> base_t;
            MemoryRangeBase(){}
            MemoryRangeBase(std::uint8_t * pos, segment_pos_t count, FarAddress && address, segment_helper_p && segment ) OP_NOEXCEPT
                : base_t(pos, count)
                , _address(std::move(address))
                ,_segment(std::move(segment))
                {
                }
            MemoryRangeBase(segment_pos_t count, FarAddress && address, segment_helper_p && segment ) OP_NOEXCEPT
                :base_t(segment->at<std::uint8_t>(address.offset), count),
                _address(std::move(address)),
                _segment(std::move(segment))
                {
                }
            MemoryRangeBase(MemoryRangeBase&& right)  OP_NOEXCEPT
                : base_t(right.pos(), right.count())
                , _address(std::move(right._address))
                , _segment(std::move(right._segment))
                , _disposer(std::move(right._disposer))
            {

            }
            MemoryRangeBase& operator = (MemoryRangeBase&& right)  OP_NOEXCEPT
            {
                _address = std::move(right._address);
                _segment = std::move(right._segment);
                _disposer = std::move(right._disposer);
                base_t::operator=(std::move(right));
                return *this;
            }

            MemoryRangeBase(const MemoryRangeBase&) = delete;
            MemoryRangeBase& operator = (const MemoryRangeBase&) = delete;
            
            ~MemoryRangeBase() OP_NOEXCEPT
            {
                if(_disposer)
                    _disposer->on_leave_scope(*this);
            }

            const FarAddress& address() const
            {
                return _address;
            }
            segment_helper_p segment() const
            {
                return _segment;
            }

            typedef std::unique_ptr<BlockDisposer> disposable_ptr_t;
            void emplace_disposable(disposable_ptr_t d)
            {
                _disposer = std::move(d);
            }
        private:
            disposable_ptr_t _disposer;
            FarAddress _address;
            segment_helper_p _segment;
        };
        /**
        *   Pointer in virtual memory and it size
        */
        struct MemoryRange : public MemoryRangeBase
        {
            MemoryRange(segment_pos_t count, FarAddress && address, segment_helper_p && segment ) :
                MemoryRangeBase(count, std::move(address), std::move(segment)){}

            MemoryRange(std::uint8_t * pos, segment_pos_t count, FarAddress && address, segment_helper_p && segment ) :
                MemoryRangeBase(pos, count, std::move(address), std::move(segment) )
                {
                }
            MemoryRange() = default;
            MemoryRange(MemoryRange&& right)
                :MemoryRangeBase(std::move(right))
            {
            }
            
            MemoryRange(const MemoryRange&) = delete;
            MemoryRange& operator = (const MemoryRange&) = delete;
            /**
            * @return new instance that is subset of this where beginning is shifeted on `offset` bytes
            *   @param offset - how many bytes to offset from beggining, must be less than #count()
            */
            MemoryRange subset(segment_pos_t offset) const
            {
                assert(offset < count());
                return MemoryRange(this->pos() + offset, count() - offset, address() + offset, this->segment());
            }
            template <class T>
            T* at(segment_pos_t idx)
            {
                assert(idx < this->count());
                return reinterpret_cast<T*>(pos() + idx);
            }

        };
        struct ReadonlyMemoryRange : MemoryRangeBase
        {
            ReadonlyMemoryRange(segment_pos_t count, FarAddress && address, segment_helper_p && segment ) :
                MemoryRangeBase(count, std::move(address), std::move(segment)){}
            ReadonlyMemoryRange(std::uint8_t * pos, segment_pos_t count, FarAddress && address, segment_helper_p && segment ) :
                MemoryRangeBase(pos, count, std::move(address), std::move(segment) )
                {
                }
            ReadonlyMemoryRange() = default;
            ReadonlyMemoryRange(ReadonlyMemoryRange&& right)  OP_NOEXCEPT
            {
                MemoryRangeBase::operator=(std::move(right));
            }
            ReadonlyMemoryRange& operator = (ReadonlyMemoryRange&& right)  OP_NOEXCEPT
            {
                MemoryRangeBase::operator=(std::move(right));
                return *this;
            }

            ReadonlyMemoryRange(const ReadonlyMemoryRange&) = delete;
            ReadonlyMemoryRange& operator = (const ReadonlyMemoryRange&) = delete;

            template <class T>
            const T* at(segment_pos_t idx) const
            {
                assert(idx < this->count());
                return reinterpret_cast<T*>(pos() + idx);
            }
        };
        /**Hint allows specify how writable block will be used*/
        enum class WritableBlockHint : std::uint8_t
        {
            block_no_hint_c = 0,
            block_for_read_c = 0x1,
            block_for_write_c = 0x2,
            force_optimistic_write_c=0x4,
            /**Block contains some information and will be used for r/w operations*/
            update_c = block_for_read_c | block_for_write_c,

            /**Block is used only for write purpose and doesn't contain usefull information yet*/
            new_c = block_for_write_c
        };
        /**Hint allows specify how readonly blocks will be used*/
        struct ReadonlyBlockHint 
        {
            enum type : std::uint8_t
            {
                ro_no_hint_c = 0,
                /**Forces keep lock even after ReadonlyMemoryRange is released. The default behaviour releases lock */
                ro_keep_lock = 0x1
            };
        };
        /**
        * Wrap together boost's class to manage segments
        */
        struct SegmentManager 
        {
            friend struct SegmentOptions;
            typedef std::shared_ptr<OP::vtm::Transaction> transaction_ptr_t;

            template <class Manager = SegmentManager>
            static std::shared_ptr<Manager> create_new(const char * file_name,
                const SegmentOptions& options = SegmentOptions())
            {
                auto result = std::shared_ptr<Manager>(
                    new Manager(file_name, true, false));
                result->_segment_size = options.segment_size();
                return result;
            }
            template <class Manager = SegmentManager>
            static std::shared_ptr<Manager> open(const char * file_name)
            {
                auto result = std::shared_ptr<Manager>(
                    new Manager(file_name, false, false));
                //try to read header of segment if previous exists
                guard_t l(result->_file_lock);

                SegmentHeader header;
                result->do_read(&header, 1);
                if (!header.check_signature())
                    throw trie::Exception(trie::er_invalid_signature, file_name);
                result->_segment_size = header.segment_size;
                return result;
            }

            segment_pos_t segment_size() const
            {
                return this->_segment_size;
            }
            segment_pos_t header_size() const
            {
                return aligned_sizeof<SegmentHeader>(SegmentHeader::align_c);
            }
            /**create new segment if needed*/
            void ensure_segment(segment_idx_t index)
            {
                guard_t l(this->_file_lock);
                _fbuf.seekp(0, std::ios_base::end);
                MyFileBuf::pos_type pos = _fbuf.tellp();
                size_t total_pages = pos / segment_size();
                
                while (total_pages <= index )
                {//no such page yet
                    this->allocate_segment();
                    total_pages++;
                }
            }
            segment_idx_t available_segments()
            {
                guard_t l(this->_file_lock);
                _fbuf.seekp(0, std::ios_base::end);
                MyFileBuf::pos_type pos = _fbuf.tellp();
                return static_cast<segment_idx_t>(pos / segment_size());
            }
            /**This operation does nothing, returns just null referenced wrapper*/
            virtual transaction_ptr_t begin_transaction() 
            {
                return transaction_ptr_t();
            }
            
            /**
            *   @param hint - default behaviour is to release lock after ReadonlyMemoryRange destroyed.
            * @throws ConcurentLockException if block is already locked for write
            */
            virtual ReadonlyMemoryRange readonly_block(FarAddress pos, segment_pos_t size, ReadonlyBlockHint::type hint = ReadonlyBlockHint::ro_no_hint_c) 
            {
                assert((pos.offset + size) <= this->segment_size());
                return ReadonlyMemoryRange(size, std::move(pos), std::move(this->get_segment(pos.segment)));
            }
            /**
            * @throws ConcurentLockException if block is already locked for concurent write or concurent read (by the other transaction)
            */
            virtual MemoryRange writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                assert((pos.offset + size) <= this->segment_size());
                return MemoryRange(size, std::move(pos), std::move(this->get_segment(pos.segment)));
            }
            /**
            * @throws ConcurentLockException if block is already locked for concurent write or concurent read (by the other transaction)
            */
            virtual MemoryRange upgrade_to_writable_block(ReadonlyMemoryRange& ro)
            {
                auto addr = ro.address();
                return MemoryRange(ro.count(), std::move(addr), std::move(ro.segment()));
            }
            /** Shorthand for \code
                readonly_block(pos, sizeof(T)).at<T>(0)
            \endcode
            */
            /*template <class T>
            const T* ro_at(const FarAddress& pos)
            {
                return this->readonly_block(pos, sizeof(T)).at<T>(0);
            } */
            /** Shorthand for \code
                readonly_block(pos, sizeof(T)).at<T>(0)
            \endcode
            */
            template <class T>
            T* wr_at(const FarAddress& pos, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                return this->writable_block(pos, sizeof(T), hint).at<T>(0);
            }
            /**
            *   It is very slow (!)  method. Prefer to use #to_far(segment_idx_t, const void *address) instead
            *   @throws std::invalid_argument if address is not in the scope of existing mapped segments
            */
            /*virtual far_pos_t to_far(const void * address) const
            {
                auto pair = this->get_index_by_address(address);
                auto diff = reinterpret_cast<const std::uint8_t*>(address)-pair.first.pos();
                return _far(pair.second, static_cast<segment_pos_t>(diff));
            }*/
            /**
            *  By the given memory pointer tries to restore origin far-pos.
            *   @param segment - where pointer should reside
            *   @param address - memory to lookup in 'segment'
            *   @return far-position resided inside specified segment
            *   @throws trie::Exception(trie::er_invalid_block) if `address` doesn't belong to `segment`
            */
            virtual far_pos_t to_far(segment_idx_t segment, const void * address) 
            {
                auto h = get_segment(segment);
                return _far(segment, h->to_offset(address));
            }
            void subscribe_event_listener(SegmentEventListener *listener)
            {
                _listener = listener;
            }
            /**Invoke functor 'f' for each segment.
            * @tparam F functor that accept 2 arguments of type ( segment_idx_t index_of segement, SegmentManager& segment_manager )
            */
            template <class F>
            void foreach_segment(F& f)
            {
                _fbuf.seekp(0, std::ios_base::end);
                auto end = (size_t)_fbuf.tellp();
                for (size_t p = 0; p < end; p += _segment_size)
                {
                    segment_idx_t idx = static_cast<segment_idx_t>(p / _segment_size);
                    segment_helper_p segment = get_segment(idx);
                    f(idx, *this);
                }
            }
            void _check_integrity()
            {
                //this->_cached_segments._check_integrity();
            }
        protected:
            SegmentManager(const char * file_name, bool create_new, bool is_readonly) :
                _file_name(file_name),
                _cached_segments(10)
            {
                //file is opened always in RW mode
                std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out | std::ios_base::binary
                    | (create_new ? std::ios_base::trunc : 0);
                _fbuf.open(file_name, mode);
                if (_fbuf.bad())
                    throw trie::Exception(trie::er_file_open, file_name);

                //make segment
                try
                {
                    _mapping = file_mapping(file_name, is_readonly ? read_only : read_write);
                }
                catch (boost::interprocess::interprocess_exception& e)
                {
                    throw trie::Exception(trie::er_memory_mapping, e.what());
                }
            }
            template <class T>
            inline SegmentManager& do_write(const T& t)
            {
                do_write(&t, 1);
                return *this;
            }
            inline void do_seek(segment_idx_t segment_idx, segment_pos_t size)
            {

            }
            template <class T>
            inline SegmentManager& do_write(const T* t, size_t n)
            {
                const auto to_write = sizeof(T)*n;
                _fbuf.write(reinterpret_cast<const MyFileBuf::char_type*>(t), to_write);
                if (_fbuf.bad())
                {
                    std::stringstream ose;
                    ose << "Cannot write:(" << to_write << ") bytes to file:" << _file_name;

                    throw trie::Exception(trie::er_write_file, ose.str().c_str());
                }
                return *this;
            }

            template <class T>
            inline SegmentManager& do_read(T* t, size_t n)
            {
                const auto to_read = sizeof(T)*n;
                _fbuf.read(reinterpret_cast<MyFileBuf::char_type*>(t), to_read);
                if (_fbuf.bad())
                {
                    std::stringstream ose;
                    ose << "Cannot read:(" << to_read << ") bytes from file:" << _file_name;

                    throw trie::Exception(trie::er_read_file, ose.str().c_str());
                }
                return *this;
            }
            void file_flush()
            {
                _fbuf.flush();
            }
            void memory_flush()
            {

            }

            inline static far_pos_t _far(segment_idx_t segment_idx, segment_pos_t offset)
            {
                return (static_cast<far_pos_t>(segment_idx) << 32) | offset;
            }
            segment_helper_p get_segment(segment_idx_t index) 
            {
                bool render_new = false;
                segment_helper_p region = _cached_segments.get(
                    index,
                    [&](segment_idx_t key)
                {
                    render_new = true;
                    auto offset = key * this->_segment_size;
                    auto result = std::make_shared<SegmentHelper>(
                        this->_mapping,
                        offset,
                        this->_segment_size);
                    
                    return result;
                }
                );
                if (render_new)
                {
                    //notify listeners that some segment was opened
                    if (_listener)
                    {
                        _listener->on_segment_opening(index, *this);
                    }
                }
                return region;
            }
            

        private:

            typedef std::fstream MyFileBuf;
            /**Per boost documentation file_lock cannot be used between 2 threads (only between process) on POSIX sys, so use named mutex*/
            typedef std::recursive_mutex file_lock_t;
            typedef std::lock_guard<file_lock_t> guard_t;
            uint32_t _segment_size;
            SegmentEventListener *_listener;

            std::string _file_name;
            MyFileBuf _fbuf;
            file_lock_t _file_lock;
            mutable file_mapping _mapping;

            typedef SparseCache<SegmentHelper, segment_idx_t> cache_region_t;
            typedef Range<const std::uint8_t*, segment_pos_t> slot_address_range_t;
            typedef boost::shared_mutex shared_mutex_t;
            typedef boost::shared_lock< shared_mutex_t > ro_guard_t;
            typedef boost::upgrade_lock< shared_mutex_t > upgradable_guard_t;
            typedef boost::upgrade_to_unique_lock< shared_mutex_t > upgraded_guard_t;
            typedef boost::unique_lock< shared_mutex_t > wo_guard_t;

            mutable cache_region_t _cached_segments;


            segment_idx_t allocate_segment()
            {
                guard_t l(_file_lock);
                _fbuf.seekp(0, std::ios_base::end);
                auto segment_offset = (std::streamoff)_fbuf.tellp();
                auto new_pos = align_on(segment_offset, _segment_size);
                segment_idx_t result = static_cast<segment_idx_t>(new_pos / _segment_size);
                SegmentHeader header(_segment_size);
                do_write(header);
                //place empty memory block
                auto current = align_on((std::streamoff)_fbuf.tellp(), SegmentHeader::align_c);
                //_fbuf.seekp(current);
                
                _fbuf.seekp(new_pos + std::streamoff(_segment_size - 1), std::ios_base::beg);
                _fbuf.put(0);
                file_flush();
                _cached_segments.put(result, std::make_shared<SegmentHelper>(
                    this->_mapping,
                    segment_offset,
                    this->_segment_size));
                if (_listener)
                    _listener->on_segment_allocated(result, *this);
                return result;
            }
            
        };
        
        struct Slot
        {
            virtual ~Slot() = default;
            /**
            *   Check if slot should be placed to specific segment. This used to organize some specific behaviour.
            *   For example MemoryManagerSlot always returns true - to place memory manager in each segment.
            *   While NamedObjectSlot returns true only for segment #0 - to support global named objects.
            */
            virtual bool has_residence(segment_idx_t segment_idx, SegmentManager& manager) const = 0;
            /**
            *   @return byte size that should be reserved inside segment. 
            */
            virtual segment_pos_t byte_size(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) const = 0;
            /**
            *   Make initialization of slot in the specified segment as specified offset
            */
            virtual void emplace_slot_to_segment(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) = 0;
            /**
            *   Perform slot openning in the specified segment as specified offset
            */
            virtual void open(segment_idx_t segment_idx, SegmentManager& manager, segment_pos_t offset) = 0;
            /**Notify slot that some segement should release resources. It is not about deletion of segment, but deactivating it*/
            virtual void release_segment(segment_idx_t segment_index, SegmentManager& manager) = 0;
            /**Allows on debug check integrity of particular segement. Default impl does nothing*/
            virtual void _check_integrity(segment_idx_t segment_index, SegmentManager& manager, segment_pos_t offset)
            {
                /*Do nothing*/
            }
        };
        /**
        *   SegmentTopology is a way to declare how interrior memory of virtual memory segement will be used.
        *   Toplogy is described as linear (one by one) applying of slots. Each slot is controled by Slot 
        *   that is specified as template argument.
        *   For example SegmentTopology <NamedObjectSlot, HeapMemorySlot> declares that Segment have to accomodate 
        *   NamedObjects in the beggining and HeapMemory after all
        */
        template <class ... TSlot>
        class SegmentTopology : public SegmentEventListener
        {
            struct TopologyHeader
            {
                std::uint16_t _slots_count;
                segment_pos_t _address[1];
            };
            typedef std::shared_ptr<Slot> slot_ptr_t;
            typedef std::array < slot_ptr_t, (sizeof...(TSlot))> slots_arr_t;
            
        public:
            typedef SegmentManager segment_manager_t;
            typedef std::shared_ptr<segment_manager_t> segments_ptr_t;

            typedef SegmentTopology<TSlot...> this_t;
            enum
            {
                slots_count_c = (sizeof...(TSlot)),
                addres_table_size_c = sizeof(TopologyHeader) + (slots_count_c-1) * sizeof(segment_pos_t)
            };
            template <class Sm>
            SegmentTopology(Sm& segments) :
                _slots({slot_ptr_t(new TSlot)... }),
                _segments(segments)
            {
                static_assert(sizeof...(TSlot) > 0, "Specify at least 1 TSlot to leverage topology");
                _segments->subscribe_event_listener(this);
                _segments->ensure_segment(0);
                _segments->readonly_block(FarAddress(0), 1);
            }
            SegmentTopology(const SegmentTopology&) = delete;

            template <class T>
            T& slot()
            {
                return dynamic_cast<T&>(*_slots[get_type_index<T, TSlot...>::value]);
            }
            void _check_integrity()
            {
                _segments->_check_integrity();
                _segments->foreach_segment([this](segment_idx_t idx, SegmentManager& segments){
                    segment_pos_t current_offset = segments.header_size();
                    //start write toplogy right after header
                    ReadonlyMemoryRange topology_address = segments.readonly_block(FarAddress(idx, current_offset), 
                        addres_table_size_c);
                    current_offset += addres_table_size_c;
                    for (auto i : _slots)
                    {
                        if (i->has_residence(idx, segments))
                        {
                            i->_check_integrity(idx, segments, current_offset);
                            current_offset += i->byte_size(idx, segments, current_offset);
                        }
                    }
                });
            }
            SegmentManager& segment_manager()
            {
                return *_segments;
            }
        protected:
            void on_segment_allocated(segment_idx_t new_segment, segment_manager_t& manager)
            {
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                segment_pos_t current_offset = manager.header_size();
                //start write toplogy right after header
                TopologyHeader* header = manager.wr_at<TopologyHeader>(FarAddress(new_segment, current_offset));
                current_offset += addres_table_size_c;
                header->_slots_count = 0;

                for (auto p = _slots.begin(); p != _slots.end(); ++p, ++header->_slots_count)
                {
                    auto& slot = *p;
                    if (!slot->has_residence(new_segment, manager))
                    {
                        header->_address[header->_slots_count] = SegmentDef::eos_c;
                        continue; //slot is not used for this segment
                    }
                    header->_address[header->_slots_count] = current_offset;
                    slot->emplace_slot_to_segment(new_segment, manager, current_offset);
                    current_offset += slot->byte_size(new_segment, manager, current_offset);
                }
                op_g.commit();
            }
            void on_segment_opening(segment_idx_t opening_segment, segment_manager_t& manager)
            {
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                segment_pos_t current_offset = manager.header_size();
                
                segment_pos_t processing_size =addres_table_size_c;
                ReadonlyMemoryRange topology_address = manager.readonly_block(
                    FarAddress(opening_segment, current_offset), processing_size);
                const TopologyHeader* header = topology_address.at<TopologyHeader>(0);
                assert(header->_slots_count == slots_count_c);

                for (auto i = 0; i < slots_count_c; ++i)
                {
                    if (SegmentDef::eos_c == header->_address[i])
                        continue;
                    auto p = _slots[i];
                    p->open(opening_segment, manager, header->_address[i]);
                }
                op_g.commit();
            }
            slots_arr_t _slots;
            segments_ptr_t _segments;
        };

        
    }
}//endof namespace OP

#endif //_OP_TR_SEGMENTMANAGER__H_
