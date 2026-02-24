#pragma once

#ifndef _OP_VTM_BASESEGMENTMANAGER__H_
#define _OP_VTM_BASESEGMENTMANAGER__H_

#include <type_traits>
#include <cstdint>
#include <memory>
#include <fstream>
#include <string>

#include <op/common/Utils.h>
#include <op/common/Exceptions.h>

#include <op/vtm/typedefs.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/managers/SegmentRegionCache.h>
#include <op/vtm/managers/SegmentRegion.h>

#include <op/vtm/vtm_error.h>

namespace OP::vtm
{
        namespace bip = boost::interprocess;
        
        struct SegmentOptions
        {
            SegmentOptions() 
                :_segment_size(1) //assume reasonable value will be assigned later
            {
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

            /**@return result size that obtained from user preference either of #segment_size(segment_pos_t hint) or #heuristic_size and alligned for OS-depended
            *   page size
            */
            segment_pos_t segment_size() const
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
        };

        /**Namespace exposes utilities to evaluate size of segment in heuristic way. Each item from namespace can be an argument to SegmentOptions::heuristic_size*/
        namespace size_heuristic
        {
            /** Evaluate heuristic size to reserve 'n'-items array of 'T'. Used with SegmentOptions::heuristic_size. For example:
            *\code
            *   SegmentOptions options;
            *   ...
            *   options.heuristic_size(size_heuristic::of_array<int, 100>); //reserve int-array of 100 items
            *\endcode
            */
            template <class T, size_t n>
            constexpr inline size_t of_array(const SegmentOptions& previous) noexcept
            {
                return
                    OP::utils::align_on(
                        OP::utils::memory_requirement<T, n>::requirement, SegmentDef::align_c
                    ) + SegmentDef::align_c/*for memory-control-structure*/;
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
                            OP::utils::memory_requirement<T>::array_size(_count), SegmentDef::align_c) 
                        + SegmentDef::align_c/*for memory-control-structure*/;
                }
            private:
                size_t _count;
            };

            /** Evaluate heuristic size to reserve 'n' assorted items of 'T'. Used with SegmentOptions::heuristic_size. For example:
            *\code
            *   SegmentOptions options;
            *   struct BigValue{...};
            *   ...
            *   //evaluate place for 100 BigValue (don't do it for small types because memory block consumes 16bytes+ of memory)
            *   options.heuristic_size(size_heuristic::of_assorted<BigValue, 100>); 
            *\endcode*/
            template <class T, size_t n = 1>
            inline size_t of_assorted(const SegmentOptions& previous)
            {
                return n*(OP::utils::aligned_sizeof<T>(SegmentDef::align_c) + SegmentDef::align_c);
            }
            
            /** Increase total amount of already evaluated bytes by some percentage value. Used with SegmentOptions::heuristic_size. For example:
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
                    return previous.segment_size() * _percentage / 100;
                }
            private:
                std::int8_t _percentage;
            };
        }//ns:size_heuristic
        
               
        /**
        * \brief SegmentManager implementation that provides basic and without any transactions access to virtual memory mapped to file system.
        *
        *   Implementation uses Boost memory mapped file.
        */
        struct BaseSegmentManager : public SegmentManager
        {
            using transaction_ptr_t = OP::vtm::transaction_ptr_t;

            static std::unique_ptr<SegmentManager> create_new(
                const char * file_name,
                const SegmentOptions& options)
            {
                using io = std::ios_base;

                size_t min_page_size = bip::mapped_region::get_page_size();
                auto segment_size = 
                    OP::utils::align_on(options.segment_size(), static_cast<segment_pos_t>(min_page_size));

                //file is opened always in RW mode
                auto file = open_file(file_name, io::in | io::out | io::binary | io::trunc);
                return std::unique_ptr<SegmentManager>(
                    new BaseSegmentManager(
                        file_name, 
                        std::move(file),
                        make_file_mapping(file_name), 
                        segment_size)
                );
            }

            static std::unique_ptr<SegmentManager> open(const char * file_name)
            {
                using io = std::ios_base;
                auto file = open_file(file_name, io::in | io::out | io::binary);

                auto result = std::unique_ptr<BaseSegmentManager>(
                    new BaseSegmentManager(
                        file_name, 
                        std::move(file),
                        make_file_mapping(file_name), 
                        1/*dummy*/)
                );

                SegmentHeader previous_header;
                result->do_read(&previous_header, 1);

                if (!previous_header.check_signature())
                    throw Exception(vtm::ErrorCodes::er_invalid_signature, file_name);
                result->_segment_size = previous_header.segment_size();
                return result;
            }

            ~BaseSegmentManager() = default;

            virtual segment_pos_t segment_size() const noexcept override
            {
                return _segment_size;
            }

            virtual segment_pos_t header_size() const noexcept override
            {
                return OP::utils::align_on(sizeof(SegmentHeader), SegmentDef::align_c);
            }

            virtual void ensure_segment(segment_idx_t index) override
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

            virtual segment_idx_t available_segments() override
            {
                guard_t l(this->_file_lock);
                _fbuf.seekp(0, std::ios_base::end);
                file_t::pos_type pos = _fbuf.tellp();
                return static_cast<segment_idx_t>(pos / segment_size());
            }
            
            /**This operation does nothing, returns just null referenced wrapper*/
            [[nodiscard]] virtual transaction_ptr_t begin_transaction() override
            {
                return transaction_ptr_t();
            }
            
            /**
            *  \brief Get memory for read-only purposes.
            *  \return raw block of memory for reading. Review use #view method for strongly typed access to the same memory.
            *
            *  \param hint - give implementation optional hint how block will be used. Default behavior is to release lock 
            *       after ReadonlyMemoryChunk destroyed.
            *  \throws ConcurrentLockException if block is already locked for write by another transaction.
            */
            [[nodiscard]] virtual ReadonlyMemoryChunk readonly_block(
                FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) override
            {
                assert((static_cast<size_t>(pos.offset()) + size) <= this->segment_size());
                return ReadonlyMemoryChunk(
                    0, 
                    ShadowBuffer{
                        this->get_segment(pos.segment()).at<std::uint8_t>(pos.offset()),
                        size,
                        /*dummy deleter since memory address from segment is not allocated in a heap*/
                        false
                    }, 
                    size, pos);
            }

            /**
            *  \brief Get memory for write purposes.
            * \throws ConcurrentLockException if block is already locked by another transaction.
            */
            [[nodiscard]] virtual MemoryChunk writable_block(
                FarAddress pos, segment_pos_t size, WritableBlockHint hint) override
            {
                assert((static_cast<size_t>(pos.offset()) + size) <= this->segment_size());
                return MemoryChunk(make_buffer(pos, size), size, pos);
            }


            /**
            * Implementation based integrity checking of this instance
            */
            virtual void _check_integrity(bool verbose)
            {
                auto& log = std::clog;
                for(auto i = 0; i < available_segments(); ++i)
                {
                    try{
                        get_segment(i)._check_integrity();
                    } catch(const std::runtime_error& inner)
                    {
                        std::ostringstream det;
                        det << "{File:" << __FILE__ << " at:" << __LINE__  << "} _check_integrity failed for segment #(" << i << "): '"
                        <<  inner.what() << "'"; 
                        if(verbose)
                            log << det.str() << "\n";
                        throw std::runtime_error(det.str().c_str());
                    }
                }
            }

            /**
            * \brief change read-only block to writable block.
            *
            * Implementation may not support this kind of upgrade, that is why default implementation provided and just call #writable_block method.
            *    
            * \throws ConcurrentLockException if block is already locked for concurrent write or concurrent read (by the other transaction).
            */
            [[nodiscard]] virtual MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro) override
            {
                return MemoryChunk(make_buffer(ro.address(), ro.count()), ro.count(), ro.address());
            }
            

            /** @return address of segment beginning */
            constexpr FarAddress start_address(segment_idx_t index) const noexcept
            {
                return FarAddress{ index, 0 };
            }
            
            
            /** Ensure underlying storage is synchronized */
            virtual void flush() override
            {
                _cached_segments.for_each([](auto& segment) {
                    segment.flush();
                    });
                _fbuf.flush();
            }

            virtual void subscribe_event_listener(SegmentEventListener* listener) override
            {
                _listener = listener;
            }
            
        protected:
            using file_t = std::fstream;

            BaseSegmentManager(const char * file_name, std::fstream file, bip::file_mapping mapping, segment_idx_t segment_size)
                : _file_name(file_name)
                , _cached_segments(10)
                , _listener(nullptr)
                , _segment_size(segment_size)
                , _fbuf(std::move(file))
                , _mapping(std::move(mapping))
            {
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

                    throw Exception(vtm::ErrorCodes::er_write_file, ose.str().c_str());
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

                    throw Exception(vtm::ErrorCodes::er_read_file, ose.str().c_str());
                }
                return *this;
            }

            inline static far_pos_t _far(segment_idx_t segment_idx, segment_pos_t offset)
            {
                return (static_cast<far_pos_t>(segment_idx) << 32) | offset;
            }

            SegmentRegion& get_segment(segment_idx_t index)
            {
                bool render_new = false;
                SegmentRegion& region = _cached_segments.get(
                    index,
                    [&](segment_idx_t key)
                    {
                        render_new = true;
                        auto offset = key * this->_segment_size;
                        return SegmentRegion{
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

            /**Per boost documentation file_lock cannot be used between 2 threads (only between process) on POSIX sys, so use named mutex*/
            using file_lock_t = std::recursive_mutex;
            using guard_t = std::lock_guard<file_lock_t> ;
            using cache_region_t = SegmentRegionCache;
            using slot_address_range_t = Range<const std::uint8_t*, segment_pos_t>;


            segment_idx_t _segment_size;
            SegmentEventListener *_listener;
            std::string _file_name;
            file_t _fbuf;
            mutable bip::file_mapping _mapping;
            file_lock_t _file_lock;
                                   
            mutable cache_region_t _cached_segments;

            static bip::file_mapping make_file_mapping(const char* file_name)
            {
                try
                {
                    return bip::file_mapping(file_name, bip::read_write);
                }
                catch (boost::interprocess::interprocess_exception& e)
                {
                    throw Exception(vtm::ErrorCodes::er_memory_mapping, e.what());
                }
            }

            static file_t open_file(const char* file_name, std::ios_base::openmode mode)
            {
                file_t new_file(file_name, mode);

                if (new_file.bad())
                {
                    std::system_error sys_err(errno, std::system_category(), file_name);
                    throw Exception(vtm::ErrorCodes::er_file_open, sys_err.what());
                }
                return new_file;
            }

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
                auto current = OP::utils::align_on((std::streamoff)_fbuf.tellp(), SegmentDef::align_c);
                
                _fbuf.seekp(new_pos + std::streamoff(_segment_size - 1), std::ios_base::beg);
                _fbuf.put(0);
                _fbuf.flush();
                _cached_segments.put(result, SegmentRegion{
                    this->_mapping,
                    segment_offset,
                    this->_segment_size });
                if (_listener)
                    _listener->on_segment_allocated(result, *this);
                return result;
            }
            
        };

}//endof namespace OP::vtm

#endif //_OP_VTM_BASESEGMENTMANAGER__H_
