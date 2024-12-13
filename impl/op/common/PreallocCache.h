#pragma once

#ifndef _OP_COMMON_PREALLOCCACHE__H_
#define _OP_COMMON_PREALLOCCACHE__H_

#include <memory>
#include <memory>
#include <array>
#include <cassert>

#include <op/common/StackAlloc.h>

namespace OP::common
{
    namespace details
    {
        /** non-template interface allows specify method to recycle element by index */
        struct CachedInstanceService
        {
            using index_t = std::uint16_t;
            /** mark instance specified by index as free (recycle) as available for next allocation */
            virtual void recycle(index_t) = 0;

            /** just to support statistic */
            virtual void on_dealloc() = 0;
        };

        /** deleter for std::unique_ptr to recycle instance back to cache */
        struct CacheRecycleDeleter
        {
            using index_t = typename CachedInstanceService::index_t;

            constexpr static index_t from_heap_c = ~index_t{ 0 };

            CachedInstanceService* _recycle_service = nullptr;
            index_t _index = from_heap_c;

            constexpr CacheRecycleDeleter() = default;

            explicit CacheRecycleDeleter(
                CachedInstanceService* recycle_service, index_t index = from_heap_c) noexcept
                : _recycle_service(recycle_service)
                , _index(index)
            {
            }

            /** Delete instance pointed by U.
            * Decision of using template concerned with possibility to use polymorph hierarchy,
            * assuming U parent of T
            */
            template <class U>
            void operator()(U* ptr) const
            {
                if(_recycle_service)
                {
                    _recycle_service->on_dealloc();
                    if (_index == from_heap_c)
                    {
                        delete ptr;
                    }
                    else
                        _recycle_service->recycle(_index);
                }
            }
        };



    } //ns:details

    template <class T, typename details::CachedInstanceService::index_t limit_c>
    class PreallocCache : public details::CachedInstanceService
    {
        using this_t = PreallocCache<T, limit_c>;
        using index_t = typename details::CachedInstanceService::index_t;

        constexpr static index_t nil_c = details::CacheRecycleDeleter::from_heap_c;

        struct PreallocEntry
        {
            constexpr PreallocEntry() = default;

            std::uint16_t _next = nil_c;
            OP::MemBuf<T> _value;
        };

    public:
        using deleter_t = details::CacheRecycleDeleter;
        using entry_ptr = std::unique_ptr<T, deleter_t>;

        constexpr static index_t cache_size_c = limit_c;
        static_assert(cache_size_c > 0, "prealloc cache must preserve at least 1 item");

        struct Statistic
        {
            /** number of times prealloc allocated memory for `T` */ 
            size_t _total_allocations = 0;
            /** Number of times when pre-allocated entities were over, so prealloc should use regular heap instead.
             *  To calculate number of time when prealloc was succeeded just use: `(_total_allocations - _times_allocation_used_heap)`.
             */
            size_t _times_allocation_used_heap = 0;

            /** number of times prealloc was used to deallocate `T` */ 
            size_t _total_deallocations = 0;
            /** number of times prealloc recycled instance of `T` back to cache */ 
            size_t _total_recycle = 0;

            /** number of free slots to allocate. Number of occupied can be calculated 
            * as `(PreallocCache::cache_size_c - _in_free)` 
            */
            index_t _in_free = 0;
        };

        constexpr PreallocCache() noexcept
        {
            _free_idx = nil_c;
            //init array
            for (auto& entry : _cache)
            {
                entry._next = _free_idx++;
            }
            //_free_idx = 0;
            _statistic._in_free = cache_size_c;
        }

        /**
        *   Construct new object `T` from arguments `Tx...`. If possible allocation happens in 
        *   cached part of this container, otherwise regular `new T{...}` is used.
        *   Note, that allocated instance must have less life-span than this PreallocCache instance.
        *   @return std::unique_ptr<T, details::CacheRecycleDeleter> with new instance of T.
        */
        template <class ... Tx>
        entry_ptr allocate(Tx&& ...args)
        {
            ++_statistic._total_allocations;
            if (_free_idx == nil_c) //no more cache
            {
                ++_statistic._times_allocation_used_heap;
                return entry_ptr{ new T(std::forward<Tx>(args)...), deleter_t{this, _free_idx} };
            }
            else
            {
                -- _statistic._in_free;
                auto& entry = _cache[_free_idx];
                auto old_idx = _free_idx;
                _free_idx = entry._next;
                entry._value.construct(std::forward<Tx>(args)...);
                return entry_ptr{ entry._value.data(), deleter_t{this, old_idx} };
            }
        }

        /** recycle object previously allocated by #allocate (on condition it was constructed in cache). */
        virtual void recycle(index_t index) override
        {
            assert(index < _cache.size());
            auto& entry = _cache[index];
            assert(entry._value.has_value()); //must be allocated

            entry._next = _free_idx;
            _free_idx = index;

            entry._value.destroy();

            ++_statistic._total_recycle;
            ++_statistic._in_free;
        }

        virtual void on_dealloc() override
        {
            ++_statistic._total_deallocations;
        }

        const Statistic& statistic() const
        {
            return _statistic;
        }

    private:
        std::array<PreallocEntry, limit_c> _cache;
        index_t _free_idx;
        Statistic _statistic = {};
    };

}//ns:OP::common

#endif //_OP_COMMON_PREALLOCCACHE__H_
