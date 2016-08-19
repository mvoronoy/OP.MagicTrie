#ifndef _OP_TR_SEGMENTMANAGER__H_
#define _OP_TR_SEGMENTMANAGER__H_

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER


#include <type_traits>
#include <cstdint>
#include <set>
#include <unordered_map>

#include <mutex>
#include <memory>

#include <fstream>
#include <string>
#include <op/common/Exceptions.h>
#include <op/common/Range.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/MemoryChunks.h>
#include <op/common/Utils.h>
#include <op/vtm/SegmentHelper.h>

namespace OP
{
    namespace trie
    {
        using namespace boost::interprocess;
        
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
        
        
        /**
        * Wrap together boost's class to manage segments
        */
        struct SegmentManager 
        {
            friend struct SegmentOptions;
            typedef OP::vtm::transaction_ptr_t transaction_ptr_t;

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
            *   @param hint - default behaviour is to release lock after ReadonlyMemoryChunk destroyed.
            * @throws ConcurentLockException if block is already locked for write
            */
            virtual ReadonlyMemoryChunk readonly_block(FarAddress pos, segment_pos_t size, ReadonlyBlockHint::type hint = ReadonlyBlockHint::ro_no_hint_c) 
            {
                assert((pos.offset + size) <= this->segment_size());
                return ReadonlyMemoryChunk(size, std::move(pos), std::move(this->get_segment(pos.segment)));
            }
            
            /**
            * @throws ConcurentLockException if block is already locked for concurent write or concurent read (by the other transaction)
            */
            virtual MemoryChunk writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                assert((pos.offset + size) <= this->segment_size());
                return MemoryChunk(size, std::move(pos), std::move(this->get_segment(pos.segment)));
            }

            /**
            * @throws ConcurentLockException if block is already locked for concurent write or concurent read (by the other transaction)
            */
            virtual MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro)
            {
                auto addr = ro.address();
                return MemoryChunk(ro.count(), std::move(addr), std::move(ro.segment()));
            }
            /** Shorthand for \code
                readonly_block(pos, sizeof(T)).at<T>(0)
            \endcode
            */
            template <class T>
            T* wr_at(FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                return this->writable_block(pos, memory_requirement<T>::requirement, hint).at<T>(0);
            }
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
                    details::segment_helper_p segment = get_segment(idx);
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
            details::segment_helper_p get_segment(segment_idx_t index)
            {
                bool render_new = false;
                details::segment_helper_p region = _cached_segments.get(
                    index,
                    [&](segment_idx_t key)
                {
                    render_new = true;
                    auto offset = key * this->_segment_size;
                    auto result = std::make_shared<details::SegmentHelper>(
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

            typedef SparseCache<details::SegmentHelper, segment_idx_t> cache_region_t;
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
                _cached_segments.put(result, std::make_shared<details::SegmentHelper>(
                    this->_mapping,
                    segment_offset,
                    this->_segment_size));
                if (_listener)
                    _listener->on_segment_allocated(result, *this);
                return result;
            }
            
        };
        /**Abstract base to construct slots. Slot is an entity that allows statically format existing segment of virtual memory.
        *   So instead of dealing with raw memory provided by SegmentManager, you can describe memory usage-rule at compile time
        * by specifying SegementTopology with bunch of slots.
        * For example: \code
        *   SegmentTopology<NodeManager, HeapManagerSlot> 
        * \endcode
        *   Specifies that we place 2 slots into each segment processed by SegmentManager. 
        */
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
            virtual segment_pos_t byte_size(FarAddress segment_address, SegmentManager& manager) const = 0;
            /**
            *   Make initialization of slot in the specified segment as specified offset
            */
            virtual void on_new_segment(FarAddress start_address, SegmentManager& manager) = 0;
            /**
            *   Perform slot openning in the specified segment as specified offset
            */
            virtual void open(FarAddress start_address, SegmentManager& manager) = 0;
            /**Notify slot that some segement should release resources. It is not about deletion of segment, but deactivating it*/
            virtual void release_segment(segment_idx_t segment_index, SegmentManager& manager) = 0;
            /**Allows on debug check integrity of particular segement. Default impl does nothing*/
            virtual void _check_integrity(FarAddress segment_addr, SegmentManager& manager)
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
                    ReadonlyMemoryChunk topology_address = segments.readonly_block(FarAddress(idx, current_offset), 
                        addres_table_size_c);
                    current_offset += addres_table_size_c;
                    for (auto i : _slots)
                    {
                        if (i->has_residence(idx, segments))
                        {
                            FarAddress addr(idx, current_offset);
                            i->_check_integrity(addr, segments);
                            current_offset += i->byte_size(addr, segments);
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
                TopologyHeader* header = manager
                    .writable_block(FarAddress(new_segment, current_offset), addres_table_size_c)
                    .at<TopologyHeader>(0);
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
                    FarAddress segment_address(new_segment, current_offset);
                    slot->on_new_segment(segment_address, manager);
                    current_offset += slot->byte_size(segment_address, manager);
                }
                op_g.commit();
            }
            void on_segment_opening(segment_idx_t opening_segment, segment_manager_t& manager)
            {
                OP::vtm::TransactionGuard op_g(manager.begin_transaction()); //invoke begin/end write-op
                segment_pos_t current_offset = manager.header_size();
                
                segment_pos_t processing_size =addres_table_size_c;
                ReadonlyMemoryChunk topology_address = manager.readonly_block(
                    FarAddress(opening_segment, current_offset), processing_size);
                const TopologyHeader* header = topology_address.at<TopologyHeader>(0);
                assert(header->_slots_count == slots_count_c);

                for (auto i = 0; i < slots_count_c; ++i)
                {
                    if (SegmentDef::eos_c == header->_address[i])
                        continue;
                    auto p = _slots[i];
                    p->open(FarAddress(opening_segment, header->_address[i]), manager);
                }
                op_g.commit();
            }
        private:
            slots_arr_t _slots;
            segments_ptr_t _segments;
        };
        /** Helper SFINAE to discover if target `T` has member method segment_manager() to resolve SegmentManager. 
        * Use:\code
        * has_segment_manager_accessor<T>::has_c 
        *\endcode
        */
        template<typename T>
        class has_segment_manager_accessor
        {
            template<typename U, SegmentManager&(U::*)() > struct SFINAE {};
            template<typename U> static char test(SFINAE<U, &U::segment_manager>*);
            template<typename U> static int test(...);
        public:
            static const bool has_c = sizeof(test<T>(0)) == sizeof(char);
        };
        
        /**Resolver of SegmentManager */
        template <class T, typename std::enable_if< std::is_convertible<T&, SegmentManager&>::value >::type* = nullptr>
        inline SegmentManager& resolve_segment_manager(T& t)
        {
            return ((SegmentManager&)t);
        }
        template <class T, typename std::enable_if< has_segment_manager_accessor<T>::has_c >::type* = nullptr>
        inline SegmentManager& resolve_segment_manager(T& t)
        {
            return resolve_segment_manager(t.segment_manager());
        }

        
    }
}//endof namespace OP

#endif //_OP_TR_SEGMENTMANAGER__H_