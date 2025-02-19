#pragma once

#ifndef _OP_VTM_SEGMENTMANAGER__H_
#define _OP_VTM_SEGMENTMANAGER__H_

#include <type_traits>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <fstream>
#include <sstream>
#include <string>

#include <op/common/Utils.h>
#include <op/common/Exceptions.h>
#include <op/common/Range.h>
#include <op/common/ThreadPool.h>

#include <op/vtm/typedefs.h>
#include <op/vtm/SegmentHelperCache.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/SegmentHelper.h>

namespace OP::vtm
{
        using namespace boost::interprocess;
        
        struct SegmentManager;

        struct SegmentEventListener
        {
            virtual void on_segment_allocated(segment_idx_t new_segment, SegmentManager& manager){}
            virtual void on_segment_opening(segment_idx_t new_segment, SegmentManager& manager){}
            /**
            *   Event is raised when segment is released (not deleted)
            */
            virtual void on_segment_releasing(segment_idx_t new_segment, SegmentManager& manager){}
        };

        struct SegmentOptions
        {
            SegmentOptions() :
                _memory_alignment(SegmentHeader::align_c)
            {
                segment_size(1);
            }

            segment_pos_t memory_alignment() const
            {
                return _memory_alignment;
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
                r = OP::utils::align_on(r, static_cast<segment_pos_t>(min_page_size));

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
                    OP::utils::align_on(
                        OP::utils::memory_requirement<T, n>::requirement, previous.memory_alignment()
                    ) + previous.memory_alignment()/*for memory-control-structure*/;
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
                        OP::utils::align_on(
                            OP::utils::memory_requirement<T>::array_size(_count), previous.memory_alignment()) 
                        + previous.memory_alignment()/*for memory-control-structure*/;
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
                return n*(OP::utils::aligned_sizeof<T>(previous.memory_alignment()) + previous.memory_alignment());
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
        
        
        /**
        * Wrap together boost's class to manage segments
        */
        struct SegmentManager 
        {
            friend struct SegmentOptions;
            using transaction_ptr_t = OP::vtm::transaction_ptr_t;

            virtual ~SegmentManager() = default;

            template <class Manager>
            static std::shared_ptr<Manager> create_new(const char * file_name,
                const SegmentOptions& options = SegmentOptions())
            {
                auto result = std::shared_ptr<Manager>(
                    new Manager(file_name, true, false));
                result->_segment_size = options.segment_size();
                return result;
            }

            template <class Manager>
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
                result->_segment_size = header.segment_size();
                return result;
            }

            constexpr segment_pos_t segment_size() const noexcept
            {
                return this->_segment_size;
            }
            
            constexpr segment_pos_t header_size() const noexcept
            {
                return OP::utils::aligned_sizeof<SegmentHeader>(SegmentHeader::align_c);
            }
            /** @return address of segment beginning */
            constexpr FarAddress start_address(segment_idx_t index) const noexcept
            {
                return FarAddress{ index, 0 };
            }

            /**create new segment if needed*/
            void ensure_segment(segment_idx_t index)
            {
                guard_t l(this->_file_lock);
                _fbuf.seekp(0, std::ios_base::end);
                file_t::pos_type pos = _fbuf.tellp();
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
                file_t::pos_type pos = _fbuf.tellp();
                return static_cast<segment_idx_t>(pos / segment_size());
            }
            
            /**This operation does nothing, returns just null referenced wrapper*/
            [[nodiscard]] virtual transaction_ptr_t begin_transaction() 
            {
                return transaction_ptr_t();
            }
            
            /**
            *   @param hint - default behaviour is to release lock after ReadonlyMemoryChunk destroyed.
            * @throws ConcurrentLockException if block is already locked for write
            */
            [[nodiscard]] virtual ReadonlyMemoryChunk readonly_block(
                FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) 
            {
                assert((static_cast<size_t>(pos.offset()) + size) <= this->segment_size());
                return ReadonlyMemoryChunk(0, make_buffer(pos, size), size, pos);
            }
            
            template <class T>
            ReadonlyAccess<T> view(FarAddress pos, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c)
            {
                return ReadonlyAccess<T>(
                            std::move(readonly_block(pos, OP::utils::memory_requirement<T>::requirement, hint))
                       );
            }
            
            template <class T>
            WritableAccess<T> accessor(FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                return WritableAccess<T>(
                        writable_block(pos, OP::utils::memory_requirement<T>::requirement, hint)
                );
            }

            /**
            * @throws ConcurrentLockException if block is already locked for concurrent write or concurrent read (by the other transaction)
            */
            [[nodiscard]] virtual MemoryChunk writable_block(
                FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                assert((static_cast<size_t>(pos.offset()) + size) <= this->segment_size());
                return MemoryChunk(make_buffer(pos, size), size, pos);
            }

            /**
            * @throws ConcurrentLockException if block is already locked for concurrent write or concurrent read (by the other transaction)
            */
            [[nodiscard]] virtual MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro)
            {
                return MemoryChunk(make_buffer(ro.address(), ro.count()), ro.count(), ro.address());
            }
            
            /** Shorthand for \code
                writable_block(pos, sizeof(T)).at<T>(0)
            \endcode
            */
            template <class T>
            inline T* wr_at(FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                return this
                    ->writable_block(
                        pos, OP::utils::memory_requirement<T>::requirement, hint)
                    .template at<T>(0);
            }

            void subscribe_event_listener(SegmentEventListener *listener)
            {
                _listener = listener;
            }
            
            /**Invoke functor 'f' for each segment.
            * @tparam F functor that accept 2 arguments of type ( segment_idx_t index_of segment, SegmentManager& segment_manager )
            */
            template <class F>
            void foreach_segment(F f)
            {
                _fbuf.seekp(0, std::ios_base::end);
                auto end = (size_t)_fbuf.tellp();
                for (size_t p = 0; p < end; p += _segment_size)
                {
                    segment_idx_t idx = static_cast<segment_idx_t>(p / _segment_size);
                    f(idx, *this);
                }
            }
            
            void _check_integrity()
            {
                //this->_cached_segments._check_integrity();
            }

        protected:

            SegmentManager(const char * file_name, bool create_new, bool is_readonly) 
                : _file_name(file_name)
                , _cached_segments(10)
                , _listener(nullptr)
                , _segment_size{ 0 }//just temp assignment, is overriden on crete/open static methods
            {
                //file is opened always in RW mode
                std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out | std::ios_base::binary;
                if(create_new)
                    mode |=  std::ios_base::trunc;

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
            /** Create memory buffer on raw memory, so associated deleter does nothing. */
            ShadowBuffer make_buffer(FarAddress address, size_t size)
            {
                return ShadowBuffer{
                    this->get_segment(address.segment()).at<std::uint8_t>(address.offset()),
                    size,
                    /*dummy deleter since memory address from segment is not allocated in a heap*/
                    false
                };
            }

            template <class T>
            inline SegmentManager& do_write(const T& t)
            {
                do_write(&t, 1);
                return *this;
            }

            template <class T>
            inline SegmentManager& do_write(const T* t, size_t n)
            {
                const auto to_write = OP::utils::memory_requirement<T>::array_size(n);
                _fbuf.write(reinterpret_cast<const file_t::char_type*>(t), to_write);
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
                const auto to_read = OP::utils::memory_requirement<T>::array_size(n);
                _fbuf.read(reinterpret_cast<file_t::char_type*>(t), to_read);
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

            details::SegmentHelper& get_segment(segment_idx_t index)
            {
                bool render_new = false;
                details::SegmentHelper& region = _cached_segments.get(
                    index,
                    [&](segment_idx_t key)
                    {
                        render_new = true;
                        auto offset = key * this->_segment_size;
                        return details::SegmentHelper{
                            this->_mapping,
                            offset,
                            this->_segment_size};
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

            using file_t = std::fstream;
            /**Per boost documentation file_lock cannot be used between 2 threads (only between process) on POSIX sys, so use named mutex*/
            typedef std::recursive_mutex file_lock_t;
            typedef std::lock_guard<file_lock_t> guard_t;
            uint32_t _segment_size;
            SegmentEventListener *_listener;

            std::string _file_name;
            file_t _fbuf;
            file_lock_t _file_lock;
            mutable file_mapping _mapping;

            using cache_region_t = SegmentHelperCache<details::SegmentHelper, segment_idx_t>;
            using slot_address_range_t = Range<const std::uint8_t*, segment_pos_t>;

            mutable cache_region_t _cached_segments;

            segment_idx_t allocate_segment()
            {
                guard_t l(_file_lock);
                _fbuf.seekp(0, std::ios_base::end);
                auto segment_offset = (std::streamoff)_fbuf.tellp();
                auto new_pos = OP::utils::align_on(segment_offset, _segment_size);
                segment_idx_t result = static_cast<segment_idx_t>(new_pos / _segment_size);
                SegmentHeader header(_segment_size);
                do_write(header);
                //place empty memory block
                auto current = OP::utils::align_on((std::streamoff)_fbuf.tellp(), SegmentHeader::align_c);
                //_fbuf.seekp(current);
                
                _fbuf.seekp(new_pos + std::streamoff(_segment_size - 1), std::ios_base::beg);
                _fbuf.put(0);
                file_flush();
                _cached_segments.put(result, details::SegmentHelper{
                    this->_mapping,
                    segment_offset,
                    this->_segment_size });
                if (_listener)
                    _listener->on_segment_allocated(result, *this);
                return result;
            }
            
        };

        /**Stub that return the same SegmentManager, but compatible with other owning classes */
        inline SegmentManager& resolve_segment_manager(SegmentManager& t) noexcept
        {
            return t;
        }
       
    
}//endof namespace OP::vtm

#endif //_OP_VTM_SEGMENTMANAGER__H_
