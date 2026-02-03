#pragma once

#ifndef _OP_VTM_APPENDONLYLOG__H_
#define _OP_VTM_APPENDONLYLOG__H_

#include <type_traits>
#include <cstdint>
#include <mutex>
#include <memory>
#include <deque>
#include <set>
#include <fstream>
#include <filesystem>

#include <op/common/Utils.h>
#include <op/common/ftraits.h>
#include <op/common/Exceptions.h>
#include <op/common/ThreadPool.h>
#include <op/common/ExclusiveStream.h>

#include <op/vtm/typedefs.h>
#include <op/vtm/SegmentHelperCache.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/SegmentHelper.h>

namespace OP::vtm
{
    namespace bip = boost::interprocess;


    /**
     * \brief AppendOnlyLog - external storage data structure to support random/sequential access to
     * the existing items.
     *
     * The append-only policy enables simple and efficient memory allocation using
     * file-mapped memory (`mmap`) to store big amount of data.
     *
     * This implementation is thread-safe, so public methods of a single instance
     * may be used concurrently in a multithreaded environment.
     *
     * **Important:** Behavior is undefined if:
     * - Two or more instances of AppendOnlyLog attempt to use the same file.
     * - Multiple processes attempt to use the same file name.
     *
     * Instances of `AppendOnlyLog` can be created in two ways:
     * - `AppendOnlyLog::create_new()` - create a new file (always recreates).
     * - `AppendOnlyLog::open()` - open an existing file.
     */
    struct AppendOnlyLog : std::enable_shared_from_this<AppendOnlyLog>
    {
        virtual ~AppendOnlyLog() = default;

        /**
         * Create a new `AppendOnlyLog` instance backed by a memory-mapped file.
         * The file is always recreated. To work with an existing file,
         * use `AppendOnlyLog::open()` instead.
         *
         * \param thread_pool Thread pool used for executing background tasks.
         * \param file_name Path to the file to create. The caller must have
         *                  permissions to create, read, and write this file.
         *                  Behavior is undefined if two or more instances of
         *                  `AppendOnlyLog` use the same file.
         * \param segment_size Size in bytes of each memory segment. Values smaller
         *                     than `mapped_region::get_page_size()` are rounded up
         *                     to the minimum supported by the OS. The final value
         *                     is always aligned to the OS page size.
         *                     Use `segment_size()` to query the actual value.
         *
         * \return A new instance of the append-only log data structure.
         *
         * \throws trie::Exception with one of the following error codes:
         *   - `trie::er_file_open` or `trie::er_write_file` — The specified file
         *     could not be opened in read–write mode.
         *   - `trie::er_memory_mapping` — The operating system could not establish
         *     a memory mapping for the specified file.
         *   - `trie::er_invalid_signature` — The file was not created by
         *     `AppendOnlyLog::create_new()`.
         */
        static std::shared_ptr<AppendOnlyLog> create_new(
            OP::utils::ThreadPool& thread_pool,
            std::filesystem::path file_name, unsigned segment_size = 1)
        {
            using io = std::ios_base;
            size_t min_page_size = bip::mapped_region::get_page_size();
            segment_size = OP::utils::align_on(
                segment_size, static_cast<segment_pos_t>(min_page_size));
            std::ofstream init_header_file = OP::exclusive_open(
                file_name.string().c_str(), io::out | io::binary | io::trunc );
            if (init_header_file.bad())
            {
                std::string what = file_name.string();
                throw Exception(vtm::ErrorCodes::er_file_open, what.c_str());
            }
            constexpr auto zero_offset =
                OP::utils::aligned_sizeof<Header>(SegmentHeader::align_c);
            //assign new Header
            Header init(segment_size, 1, FarAddress(0, zero_offset), FarAddress(0, zero_offset));

            init_header_file.write(reinterpret_cast<char*>(&init), sizeof(Header));
            init_header_file.flush();
            if (init_header_file.bad())
            {
                std::string what = file_name.string();
                throw Exception(vtm::ErrorCodes::er_write_file, what.c_str());
            }
            std::filesystem::resize_file(file_name, segment_size);

            std::shared_ptr<AppendOnlyLog> result(
                new AppendOnlyLog(
                    thread_pool,
                    file_name,
                    init_file_mapping(file_name),
                    segment_size
                ));
            return result;
        }

        /**
         * Create a new `AppendOnlyLog` instance and setup memory-mapping with existing file.
         *
         * The file must exists _at_impl the moment of opening. To create/recreate file,
         * use `AppendOnlyLog::create_new()` instead.
         *
         * \param thread_pool Thread pool used for executing background tasks.
         * \param file_name Path to the file to open. The caller must have
         *                  permissions to read, and write this file.
         *                  Behavior is undefined if two or more instances of
         *                  `AppendOnlyLog` use the same file.
         *
         * \return A new instance of the append-only log data structure.
         *
         *  \throws trie::Exception with one of the following error codes:
         *   - `trie::er_file_open` - The specified file
         *     could not be opened in read–write mode.
         *   - `trie::er_memory_mapping` - The operating system could not establish
         *     a memory mapping for the specified file.
         *   - `trie::er_invalid_signature` - The file has invalid format and was not created by
         *     `AppendOnlyLog::create_new()`.
         */
        static std::shared_ptr<AppendOnlyLog> open(
            OP::utils::ThreadPool& thread_pool,
            std::filesystem::path file_name
        )
        {
            using io = std::ios_base;
            std::ifstream header_loader(file_name, io::in | io::binary);
            if (header_loader.bad())
            {
                std::string what = file_name.string();
                throw Exception(vtm::ErrorCodes::er_file_open, what.c_str());
            }

            Header preinit;
            header_loader.read(reinterpret_cast<char*>(&preinit), sizeof(Header));

            if (header_loader.bad() || !preinit.check_signature() || preinit._segments_count < 1)
            {
                std::string what = file_name.string();
                throw Exception(vtm::ErrorCodes::er_invalid_signature, what.c_str());
            }
            auto result = std::shared_ptr<AppendOnlyLog>(
                new AppendOnlyLog(
                    thread_pool, file_name,
                    init_file_mapping(file_name),
                    preinit._segment_size));
            result->_segments.resize(preinit._segments_count);
            // use deque without lock - is normal since instance is local only yet
            result->ensure_segment(0);
            return result;
        }

        constexpr segment_pos_t segment_size() const noexcept
        {
            return this->_segment_size;
        }

        segment_idx_t segments_count() const
        {
            guard_t guard_header(_header_lock);
            auto& mmap = _segments[0];
            assert(mmap.get_address()); //0-segment must be always valid
            return reinterpret_cast<Header*>(mmap.get_address())->_segments_count;
        }

        OP::utils::ThreadPool& thread_pool() const
        {
            return _thread_pool;
        }

        const std::filesystem::path& file_name() const
        {
            return _file_name;
        }

        template <class T = std::uint8_t>
        [[nodiscard]] T* at(FarAddress entry)
        {
            guard_t guard_header(_header_lock);
            LogEntry* log_entry = _at_impl<LogEntry>(entry);
            assert(log_entry->check_signature()); //DEBUG mode only
            return reinterpret_cast<T*>(log_entry->buffer());
        }

        /**
         * Sequentially iterates over all allocated memory blocks, invoking the provided callback functor.
         *
         * \tparam TCallback A callable type invoked for each allocated memory block.
         *   Supported signatures are:
         *   - `void(UserType*)` — Iterates over all memory blocks, using `reinterpret_cast` to `UserType`.
         *     You may use `void*`, `std::byte*` or a plain user-defined structure as `UserType`.
         *   - `bool(UserType*)` — Allows controlling iteration: return `true` to continue,
         *     or `false` to stop early.
         *
         * \param f The callback functor to apply to each memory block.
         */
        template <class TCallback>
        void for_each(TCallback&& f)
        {
            using ftraits_t = OP::utils::function_traits<TCallback>;
            using arg0_t = typename ftraits_t::template arg_i<0>;
            using lambda_res_t = typename ftraits_t::result_t;

            guard_t top_lock(_header_lock);
            auto header = _at_impl<Header>(FarAddress(0));
            //as soon A0l only grows, _end item can be cached and then checked with double-check pattern
            auto check_last = header->_end;
            auto i = header->_first;
            auto current_segment = i.segment();
            bip::mapped_region* current_mapping = &ensure_segment(current_segment);
            top_lock.unlock();

            for (;;)
            {
                if (i == check_last)
                { //apply double-check pattern 
                    guard_t guard_header(_header_lock);
                    check_last = header->_end;
                    if (i == check_last)
                        break;
                }
                auto* log_entry = reinterpret_cast<LogEntry*>(
                    reinterpret_cast<std::uint8_t*>(current_mapping->get_address()) + i.offset());

                if constexpr (std::is_convertible_v<lambda_res_t, bool>)
                {//lambda signature indicates when to stop iteration
                    if (!static_cast<bool>(f(reinterpret_cast<arg0_t>(log_entry->buffer()))))
                        break;
                }
                else //lambda has no indicator to stop, just iterate all
                    f(reinterpret_cast<arg0_t>(log_entry->buffer()));

                i += log_entry->_byte_size;
                if (i.offset() >= _segment_size)
                {
                    i = FarAddress(++current_segment, 0);
                    guard_t guard_header(_header_lock);
                    current_mapping = &ensure_segment(current_segment);
                }
            }
        }

        /**
         * Allocates a byte buffer in the file-mapped space.
         *
         * \param byte_count Number of bytes to allocate. The method will align this value
         *        up to `SegmentHeader::align_c`.
         *
         * \return A `std::pair<FarAddress, std::uint8_t*>` where:
         *         - The first element is a `FarAddress` that can be persisted and later
         *           converted back to a buffer using the #at method.
         *         - The second element is a pointer to the newly allocated byte buffer
         *           in memory.
         */
        [[nodiscard]] std::pair<FarAddress, std::uint8_t*> allocate(segment_pos_t byte_count)
        {
            byte_count = OP::utils::align_on(byte_count, SegmentHeader::align_c);
            auto real_size =
                OP::utils::aligned_sizeof<LogEntry>(SegmentHeader::align_c)
                + byte_count
                ;

            guard_t guard_header(_header_lock);
            auto header = _at_impl<Header>(FarAddress(0));

            auto next_offset = header->_end.offset() + real_size;
            std::uint8_t* result{};
            if (next_offset >= _segment_size)
            {
                //some time gap can appear between last and new segment, in this case include this 
                //tail to the last LogEntry to avoid size decoherence.
                LogEntry* last_log = _at_impl<LogEntry>(header->_last);
                last_log->_byte_size += (_segment_size - header->_end.offset());
                header->_end = FarAddress(header->_end.segment() + 1, real_size);
                header->_last = FarAddress(header->_end.segment(), 0);
            }
            else
            {
                header->_last = header->_end;
                header->_end.set_offset(next_offset);
            }
            result = raw(header->_last, real_size);
            return { header->_last, (::new (result) LogEntry{ real_size })->buffer() };
        }

        /** Allocate buffer and construct new instance of T with constructor args `TArgs`.
        *   \return pair: 
        *       - `FarAddress` where memory block was allocated, the value can be used later with `#at` method.
        *       - `T*` pointer to the just created instance.
        */
        template <class T, class ...TArgs>
        [[nodiscard]] std::pair<FarAddress, T*> construct(TArgs&&...constructor_args)
        {
            //`allocate` grants aligned memory allocation and `sizeof`
            auto [addr, buffer] = allocate(sizeof(T));
            return {addr, ::new (buffer) T(std::forward<TArgs>(constructor_args)...)};
        }

        //ShadowBuffer as_buffer(FarAddress address, segment_pos_t byte_count)
        //{
        //    return ShadowBuffer{
        //        at<std::uint8_t>(address),
        //        byte_count,
        //        /*dummy deleter since memory address from segment is not allocated in a heap*/
        //        false
        //    };
        //}


    protected:

        AppendOnlyLog(
            OP::utils::ThreadPool& thread_pool,
            std::filesystem::path file_name,
            bip::file_mapping file_mapping,
            segment_pos_t segment_size
        ) noexcept
            : _thread_pool(thread_pool)
            , _file_name(std::move(file_name))
            , _file_mapping(std::move(file_mapping))
            , _segment_size(segment_size)
        {
        }

    private:

        struct Header
        {
            constexpr static std::uint32_t signature_value_c = (((std::uint32_t{ 'h' } << 8 | 'A') << 8 | '0') << 8) | 'L';
            std::uint32_t _sig = signature_value_c;
            segment_pos_t _segment_size{};
            segment_idx_t _segments_count{};
            FarAddress _first{}, _end{}, _last{};

            Header() noexcept = default;

            constexpr Header(
                segment_pos_t segment_size,
                segment_idx_t segments_count,
                FarAddress first,
                FarAddress end) noexcept
                : _segment_size(segment_size)
                , _segments_count(segments_count)
                , _first(first)
                , _last(first)
                , _end(end)
            {
            }

            bool check_signature() const noexcept
            {
                return _sig == signature_value_c;
            }
        };

        struct LogEntry
        {
            static constexpr short signature = (short{ 'l' } << 8) | 'e';

            const short _sig = signature;
            segment_pos_t _byte_size; // this + align + buffer

            explicit LogEntry(segment_pos_t byte_size) noexcept
                : _byte_size(byte_size)
            {
            }

            std::uint8_t* buffer() noexcept
            {
                return reinterpret_cast<std::uint8_t*>(this) +
                    OP::utils::aligned_sizeof<LogEntry>(SegmentHeader::align_c);
            }

            bool check_signature() const noexcept
            {
                return _sig == signature;
            }
        };

        using guard_t = std::unique_lock<std::recursive_mutex>;

        OP::utils::ThreadPool& _thread_pool;
        std::filesystem::path _file_name;
        bip::file_mapping _file_mapping;
        const segment_pos_t _segment_size;
        std::deque<bip::mapped_region> _segments;
        std::set<segment_idx_t> _future_segments_queue;
        mutable std::recursive_mutex _header_lock;

        [[nodiscard]] std::uint8_t* segment_memory(segment_idx_t index, segment_pos_t offset = 0)
        {
            auto& mmap = _segments[index];
            auto* base = mmap.get_address();
            assert(base);
            return reinterpret_cast<std::uint8_t*>(base) + offset;
        }

        [[nodiscard]] std::uint8_t* raw(FarAddress address, segment_pos_t expected_size)
        {
            assert((address.offset() + expected_size) < _segment_size);
            auto& mapping = ensure_segment(
                address.segment(),
                // for 95% make prediction about next segment
                (address.offset() + expected_size) * 100 >= 95 * _segment_size
                ? ensure_hint_t::query_next
                : ensure_hint_t::no
            );
            return reinterpret_cast<std::uint8_t*>(mapping.get_address()) + address.offset();
        }

        template <class T>
        [[nodiscard]] T* _at_impl(FarAddress address)
        {
            return reinterpret_cast<T*>(
                raw(address, OP::utils::aligned_sizeof<T>(SegmentHeader::align_c))
                );
        }

        enum class ensure_hint_t
        {
            /** no hint, just check segment exists*/
            no,
            /** in short time there is high probability for next segment, corelated with boost::interprocess::advice_willneed */
            next_soon,
            /** start in thread pool task to allocate segment with `next_soon` flag */
            query_next
        };

        /** \brief create new segment if needed.
        * \pre captured lock: `guard_t guard(_header_lock);`
        */
        [[maybe_unused]] bip::mapped_region& ensure_segment(
            segment_idx_t index, ensure_hint_t predict_next = ensure_hint_t::no)
        {
            if (index >= _segments.size())
            {
                _segments.resize(index + 1);
                std::filesystem::resize_file(_file_name, _segments.size() * _segment_size);
            }

            if (!_segments[index].get_address())
            { //no mapping yet
                _segments[index] = bip::mapped_region(
                    _file_mapping, bip::read_write, index * _segment_size, _segment_size
                );
                if (predict_next == ensure_hint_t::next_soon)//tell the OS that next segment will be queried soon
                    _segments[index].advise(bip::mapped_region::advice_willneed);
                if (index > 0) //ignore for zero segment, when Header not allocated yet
                {
                    auto& zero_mapping = _segments[0];
                    assert(zero_mapping.get_address()); //segment 0 must be always valid
                    reinterpret_cast<Header*>(zero_mapping.get_address())->_segments_count =
                        static_cast<segment_idx_t>(_segments.size());
                }
            }

            if (predict_next == ensure_hint_t::query_next)
            { //95% threshold reached, start background task to prepare next segment
                auto next_index = index + 1;
                if ( 
                    (_segments.size() <= next_index || !_segments[next_index].get_address())
                    && _future_segments_queue.insert(next_index).second )
                {
                    // need use shared_ptr to avoid situation when
                    // A0l is deallocated but some work is pending yet
                    _thread_pool.one_way([zhis = shared_from_this()]() {
                        guard_t guard_header(zhis->_header_lock);
                        for(auto seg_idx: zhis->_future_segments_queue)
                            zhis->ensure_segment(seg_idx, ensure_hint_t::next_soon);
                        });
                }
            }

            return _segments[index];
        }

        [[nodiscard]] static bip::file_mapping init_file_mapping(
            const std::filesystem::path& file_name)
        {
            try
            {
                std::string char_cast_name = file_name.string();
                return bip::file_mapping(char_cast_name.c_str(), bip::read_write);
            }
            catch (boost::interprocess::interprocess_exception& e)
            {
                throw Exception(vtm::ErrorCodes::er_memory_mapping, e.what());
            }
        }

    };


}//ns: OP::vtm

#endif //_OP_VTM_APPENDONLYLOG__H_
