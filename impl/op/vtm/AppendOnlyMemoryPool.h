#pragma once

#ifndef _OP_VTM_APPENDONLYMEMORYPOOL__H_
#define _OP_VTM_APPENDONLYMEMORYPOOL__H_

#include <type_traits>
#include <cstdint>
#include <mutex>
#include <memory>
#include <deque>
#include <set>
#include <fstream>
#include <filesystem>


#include <op/common/Utils.h>
#include <op/common/DependencyManager.h>
#include <op/common/ftraits.h>
#include <op/common/Exceptions.h>
#include <op/common/ThreadPool.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/typedefs.h>
#include <op/vtm/vtm_error.h>

namespace OP::vtm
{
    namespace opc = OP::common;

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
    struct AppendOnlyMemoryPool
        : opc::DependencyManager<
            opc::expecting<OP::vtm::SegmentManager>(), 
            opc::expecting<OP::utils::ThreadPool>()
        >
    {
        constexpr explicit AppendOnlyMemoryPool(segment_idx_t root_segment = 0) noexcept
            : _make_new(true) 
            , _root_segment(root_segment)
        {
        }

        explicit AppendOnlyMemoryPool(FarAddress open_address, bool make_new = false) noexcept
            : _make_new(make_new)
            , _header_addr(open_address)//_root_segment - will be assigned on virtual constructor
        {
        }

        virtual ~AppendOnlyMemoryPool() = default;


        FarAddress root_address() const noexcept
        {
            return _header_addr;
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
            using decayed_type_t = std::decay_t<arg0_t>;
            using lambda_res_t = typename ftraits_t::result_t;

            auto& segment_manager = resolve<OP::vtm::SegmentManager>();
            auto [begin_addr, user_space_size] = segment_manager.user_space(_root_segment);

            guard_t top_lock(_header_acc);
            auto header_block = segment_manager.view<Header>(begin_addr);
            auto& header = *header_block;
            //as soon A0l only grows, _end item can be cached and then checked with double-check pattern
            auto check_last = header._end;
            auto i = header._first;
            auto current_segment = segment_manager.segment_of(i);
            ensure_segment(current_segment);
            top_lock.unlock();

            bool notify_prepare_next = true; //don't notify twice for the next segment
            for (;;)
            {
                if (i == check_last)
                { //apply double-check pattern 
                    guard_t guard_header(_header_acc);
                    check_last = header->_end; //update indicator, if changed
                    if (i == check_last)
                        break;
                }
                const auto current_segment = segment_manager.segment_of(i);

                auto [this_seg_addr, user_space_size] = segment_manager.user_space(current_segment);
                auto last_byte_addr = this_seg_addr + user_space_size;

                if(notify_prepare_next && segment_manager.offset_of(i)*10 >= 9*segment_manager.offset_of(last_byte_addr) )
                {
                    ensure_segment(current_segment, ensure_hint_t::query_next);//notify to prepare next
                    notify_prepare_next = false; //prevent next checks inside this segment
                }

                auto ro_block = segment_manager.accessor<LogEntry>(i);
                auto next_offset = ro_block->_byte_size;
                FarAddress buffer_addr = i + 
                    OP::utils::aligned_sizeof<LogEntry>(SegmentDef::align_c)
                    ;

                //per spec: If T is a reference type then is_const<T>::value is always false. The 
                // proper way to check a potentially-reference type for constness is to remove 
                // the reference: is_const<typename remove_reference<T>::type>
                if constexpr(std::is_const_v<std::remove_reference_t<arg0_t>>) 
                {//arg of callback suppose const&
                    auto buffer_view = segment_manager.view<decayed_type_t>(buffer_addr);
                    if(!iteration_step<std::is_convertible_v<lambda_res_t, bool>>(f, *buffer_view))
                        break;
                }
                else
                {//arg of callback expects modification
                    auto wr_block = segment_manager.accessor<decayed_type_t>(buffer_addr);
                    if(!iteration_step<std::is_convertible_v<lambda_res_t, bool>>(f, *wr_block))
                        break;
                }

                i += next_offset;
                if (i >= last_byte_addr)
                {
                    auto next_segment = 1 + current_segment;
                    std::tie(i, user_space_size) = segment_manager.user_space( next_segment );//query next segment
                    ensure_segment(next_segment);
                    notify_prepare_next = true; //reset checking
                }
            }
        }

        /**
         * Allocates a byte buffer in the file-mapped space.
         *
         * \param byte_count Number of bytes to allocate. The method will align this value
         *        up to `SegmentDef::align_c`.
         *
         * \return A `std::pair<FarAddress, std::uint8_t*>` where:
         *         - The first element is a `FarAddress` that can be persisted and later
         *           converted back to a buffer using the #at method.
         *         - The second element is a pointer to the newly allocated byte buffer
         *           in memory.
         */
        [[nodiscard]] FarAddress allocate(segment_pos_t byte_count)
        {
            byte_count = OP::utils::align_on(byte_count, SegmentDef::align_c);
            const auto real_size =
                OP::utils::aligned_sizeof<LogEntry>(SegmentDef::align_c)
                + byte_count
                ;
            auto& segment_manager = resolve<OP::vtm::SegmentManager>();

            guard_t guard_header(_header_acc);
            auto wr_block = segment_manager.accessor<Header>(_header_addr);
            auto& header = *wr_block;
            assert(header.check_signature());
            auto next_addr = header._end + real_size;
            auto [end_addr, user_space_size] = 
                segment_manager.user_space(segment_manager.segment_of(next_addr));
            auto last_byte_addr = end_addr + user_space_size;

            ensure_hint_t ensure_hint = ensure_hint_t::no;
            if(segment_manager.offset_of(next_addr)*10 >= 9*segment_manager.offset_of(last_byte_addr))
                ensure_hint = ensure_hint_t::next_soon;//prepare to promote next segment

            if (next_addr < last_byte_addr)
            {
                ensure_segment(segment_manager.segment_of(next_addr), ensure_hint);
                header._last = header._end;
                header._end = next_addr;
            }
            else
            {
                //some time gap can appear between last and new segment, in this case include this 
                //tail to the last LogEntry to avoid size decoherence.
                auto last_log_block = segment_manager.accessor<LogEntry>(header._last);
                LogEntry& last_log = *last_log_block;
                auto new_segment = segment_manager.segment_of(header._last) + 1;
                last_log._byte_size = segment_manager.offset_of(last_byte_addr) - segment_manager.offset_of(header._last);
                auto [new_addr, new_space_size] = segment_manager.user_space(new_segment);
                ensure_segment(new_segment, ensure_hint);

                header._end = new_addr + real_size;
                header._last = new_addr;
            }

            auto new_block = segment_manager.accessor<LogEntry>(header._last);
            new_block.make_new(real_size); //construct new LogEntry
            return header._last + 
                OP::utils::aligned_sizeof<LogEntry>(SegmentDef::align_c)
                ;
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
            auto addr = allocate(sizeof(T));
            auto& segment_manager = resolve<OP::vtm::SegmentManager>();
            auto inst_block = segment_manager.accessor<T>(addr);
            inst_block.make_new(std::forward<TArgs>(constructor_args)...);
            return {addr, &inst_block};
        }

    protected:
        virtual void on_complete() override
        {
            auto& segment_manager = resolve<OP::vtm::SegmentManager>();

            guard_t guard_header(_header_acc);
            if(_make_new)
            {
                ensure_segment(_root_segment);
                constexpr auto header_size =
                    OP::utils::aligned_sizeof<Header>(SegmentDef::align_c);
                if(_header_addr.is_nil())
                    std::tie(_header_addr, std::ignore) = segment_manager.user_space(_root_segment);
                
                auto wr_block = segment_manager.accessor<Header>(_header_addr);
                constexpr auto zero_offset =
                    OP::utils::aligned_sizeof<Header>(SegmentDef::align_c);
                FarAddress begin_addr = _header_addr + zero_offset;
                //assign new Header
                wr_block.make_new(begin_addr, begin_addr);
            }
            else
            {
                _root_segment = segment_manager.segment_of(_header_addr);
                auto ro_block = segment_manager.view<Header>(_header_addr);
                auto& header = *ro_block;
                if (!header.check_signature())
                {
                    throw Exception(vtm::ErrorCodes::er_invalid_signature, "header signature check failed");
                }
            }
        }

    private:

        template <bool use_return_value, class F, class T>
        static bool iteration_step(F& callback, T&& value)
        {
            if constexpr(use_return_value)
            {
                return callback(std::forward<T>(value));
            }
            else
            {
                callback(std::forward<T>(value));
                return true;
            }
        }

        struct Header
        {
            using presence_t = segment_pos_t;

            constexpr static std::uint32_t signature_value_c = ((((std::uint32_t{ 'A' } << 8 | '0') << 8) | 'M') << 8) | 'P';

            const std::uint32_t _sig = signature_value_c;

            FarAddress _first{}, _end{}, _last{};

            constexpr Header() noexcept = default;

            constexpr Header(
                FarAddress first,
                FarAddress end) noexcept
                :  _first(first)
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
            static constexpr std::uint16_t signature = (std::uint16_t{ 'l' } << 8) | 'e';

            const std::uint16_t _sig = signature;
            segment_pos_t _byte_size; // this + align + buffer

            explicit LogEntry(segment_pos_t byte_size) noexcept
                : _byte_size(byte_size)
            {
            }

            bool check_signature() const noexcept
            {
                return _sig == signature;
            }
        };

        using guard_t = std::unique_lock<std::mutex>;

        segment_idx_t _root_segment{};
        bool _make_new;
        FarAddress _header_addr;
        mutable std::mutex _header_acc;

        /** \brief create new segment if needed.
        */
        void ensure_segment(
            segment_idx_t index, ensure_hint_t predict_next = ensure_hint_t::no)
        {

            auto& segment_manager = resolve<OP::vtm::SegmentManager>();
            segment_manager.ensure_segment(index, predict_next);

            if (predict_next == ensure_hint_t::next_soon)
            { //95% threshold reached, start background task to prepare next segment
                auto& thread_pool = resolve<OP::utils::ThreadPool>();

                thread_pool.one_way([&segment_manager, next_index = index + 1]() {
                    segment_manager.ensure_segment(next_index, ensure_hint_t::next_soon);
                });
            }
        }

    };


}//ns: OP::vtm

#endif //_OP_VTM_APPENDONLYMEMORYPOOL__H_
