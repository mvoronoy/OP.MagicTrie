#pragma once

#ifndef _OP_VTM_MANAGERS_BUCKETINDEXED_LIST__H_
#define _OP_VTM_MANAGERS_BUCKETINDEXED_LIST__H_

#include <shared_mutex>
#include <array>
#include <list>
#include <atomic>
#include <memory>
#include <mutex>
#include <limits>

#include <op/common/Bitset.h>
#include <op/common/has_member_def.h>

namespace OP::vtm
{
    /** \brief thread safe container with append/remove only alter operations. Linear scan can be improved by adding arbitrary indexers 
    *
    */
    template <class T, class ...TIndexer>
    class BucketIndexedList
    {
        enum class Status
        {
            valid,
            garbage
        };

        struct Bucket
        {
            using indexers_t = std::tuple<TIndexer...>;
            using usize_t = std::uint_fast16_t;
            constexpr static usize_t capacity_c = 8;

            std::atomic<usize_t> _presence = 0;
            std::atomic<Status> _status{ Status::valid };
            /** indicate size occupied so far */
            std::atomic<usize_t> _size = 0;
            std::array<std::atomic<T*>, capacity_c> _data = {};
            indexers_t _indexers;

            ~Bucket()
            {
                _presence.store(0, std::memory_order::relaxed/*don't care about mask anymore*/); 
                for (auto i = 0; i < _size; ++i)
                {
                    auto* prev = _data[i].exchange(nullptr, std::memory_order_acquire);
                    //don't need: if(prev) since _size controls only real slots
                    delete prev;
                }
            }

            template <class F>
            bool bunch_for_each(F& callback)
            {
                usize_t local_mask = _presence.load(std::memory_order_acquire);
                for (auto i = 0; i < capacity_c && local_mask; local_mask = (_presence.load(std::memory_order_acquire) >> ++i))
                {
                    if (local_mask & 1)
                    {
                        auto* value = _data[i].load(std::memory_order_acquire);
                        assert(value);//mask must grant value exists
                        if constexpr (std::is_convertible_v<decltype(callback(*value)), bool>)
                        {
                            if (!callback(*value))
                                return false; //forced stop
                        }
                        else //lambda of non-bool result
                        {
                            callback(*value);
                        }
                    }
                }
                return true; //don't stop
            }

            bool append(std::unique_ptr<T>& item)
            {
                if (_status != Status::valid)
                    return false;
                auto ins_index = _size.load(std::memory_order_acquire);
                do {
                    if (ins_index == capacity_c)
                        return false;  // need move to other bunch
                } while (!_size.compare_exchange_strong(
                    ins_index, ins_index + 1,
                    std::memory_order_acq_rel, std::memory_order_acquire));
                //now ins_index - exact previous valid value
                T* new_data = item.get();
                auto* prev = _data[ins_index].exchange(item.release(), std::memory_order_acquire);
                assert(prev == nullptr); // must not be conflict there
                //update indexers to include new item
                std::apply([&](auto& ...indexer) {
                    (indexer.index(*new_data), ...);
                    }, _indexers);
                _presence.fetch_or(usize_t(1) << ins_index, std::memory_order_release); //indicate item available for scan
                return true;
            }

            constexpr bool empty() const
            {
                return _presence == 0;
            }

            /** number of items presented in bucket (not removed) */
            usize_t presence_count() const
            {
                usize_t local_mask = _presence.load(std::memory_order_acquire);
                return OP::common::popcount_sideways32(local_mask); //number of 1 bits in presence mask
            }

            /** Mark for deletion items where predicate F return true. Return true if item was matched 
            * with predicate and marked as removed, false when not found. 
            */
            template <class F>
            bool soft_remove_if_first(F&& predicate)
            {
                usize_t local_mask = _presence.load(std::memory_order_acquire);
                for (auto i = 0; i < capacity_c && local_mask; local_mask = (_presence.load(std::memory_order_acquire) >> ++i))
                {
                    if (local_mask & 1)
                    {
                        auto* value = _data[i].load(std::memory_order_acquire);
                        assert(value);//mask must grant value exists
                        if (predicate(*value))
                        {//clear presence
                            usize_t new_mask = ~(usize_t(1) << i);
                            if (!(new_mask & _presence.fetch_and(new_mask, std::memory_order_release)))
                            {//indicate bunch available for later delete
                                _status.store(Status::garbage, std::memory_order_release);
                            }
                            return true;//stop at first
                        }
                    }
                }
                return false;
            }

            template <class F>
            size_t soft_remove_if_all(F&& predicate)
            {
                size_t result = 0;
                usize_t local_mask = _presence.load(std::memory_order_acquire);
                for (auto i = 0; i < capacity_c && local_mask; local_mask = (_presence.load(std::memory_order_acquire) >> ++i))
                {
                    if (local_mask & 1)
                    {
                        auto* value = _data[i].load(std::memory_order_acquire);
                        assert(value);//mask must grant value exists
                        if (predicate(*value))
                        {//clear presence
                            ++result;
                            usize_t new_mask = ~(usize_t(1) << i);
                            if (!(new_mask & _presence.fetch_and(new_mask, std::memory_order_release)))
                            {//indicate bunch available for later delete
                                _status.store(Status::garbage, std::memory_order_release);
                                return result; //bucket is already empty, no need for loop
                            }
                            //go next step
                        }
                    }
                }
                return result;
            }
            
        };

        using index_list_t = std::list<Bucket>;
        index_list_t _buckets;
        mutable std::shared_mutex _buckets_acc;
        std::atomic<size_t> _empty_buckets = 0;


        constexpr static inline auto _check_lifter_ = [](const auto& index, auto&&... args) 
            -> decltype(index.check(std::forward<decltype(args)>(args)...)) {
            return index.check(std::forward<decltype(args)>(args)...);
            };

        template <class TChecker, class Query>
        requires std::invocable<decltype(_check_lifter_), const TChecker&, const Query&>
        static bool _call_indexer_check(const TChecker& indexer, const Query& query)
        {
            return indexer.check(query);
        }

        template <class ...Tx>
        static bool _call_indexer_check(Tx&& ...)
        {
            return true;
        }

    public:

        BucketIndexedList() = default;

        void append(std::unique_ptr<T> item)
        {
            std::unique_lock w_guard(_buckets_acc);
            auto* current = &(_buckets.empty() 
                ? _buckets.emplace_back()
                : _buckets.back());
            while (!current->append(item))
                current = &(_buckets.emplace_back());
        }

        size_t buckets_count() const
        {
            std::shared_lock r_guard(_buckets_acc);
            return _buckets.size();
        }

        size_t empty_buckets_count() const
        {
            return _empty_buckets.load(std::memory_order_acquire);
        }

        template <class F>
        void for_each(F&& callback)
        {
            std::shared_lock r_guard(_buckets_acc);
            for(auto& bucket: _buckets)
            {
                if (bucket._status != Status::valid)
                    continue; //continue scan
                if constexpr (std::is_convertible_v<decltype(callback(*bucket._data[0])), bool>)
                {
                    if (!bucket.bunch_for_each(callback))
                        break;
                }
                else
                {
                    bucket.bunch_for_each(callback);
                }
            }
        }

        template <class Query, class F>
        void indexed_for_each(const Query& query, F&& callback)
        {
            std::shared_lock r_guard(_buckets_acc);
            for (auto& bucket : _buckets)
            {
                if (bucket._status != Status::valid)
                    continue; //continue scan
                if (!std::apply([&](const auto& ...indexer) -> bool {
                        return (_call_indexer_check(indexer, query) && ...);
                    }, bucket._indexers))
                    continue;
                if constexpr (std::is_convertible_v<decltype(callback(*bucket._data[0])), bool>)
                {
                    if (!bucket.bunch_for_each(callback))
                        break;
                }
                else
                {
                    bucket.bunch_for_each(callback);
                }
            }
        }

        template <class Query, class F>
        bool soft_remove_if_first(const Query& query, F&& callback)
        {
            std::shared_lock r_guard(_buckets_acc);
            for (auto& bucket : _buckets)
            {
                if (bucket._status.load(std::memory_order_acquire) != Status::valid)
                    continue; //continue scan
                if (!std::apply([&](const auto& ...indexer) -> bool {
                    return (indexer.check(query) && ...);
                    }, bucket._indexers))
                    continue;
                if (bucket.soft_remove_if_first(callback))
                {
                    if(bucket._status.load(std::memory_order_acquire) == Status::garbage) //bucket reached empty state
                    {
                        _empty_buckets.fetch_add(1, std::memory_order_release);
                    }
                    return true;
                }
            }
            return false;
        }

        template <class Query, class F>
        [[maybe_unused]] size_t soft_remove_if_all(const Query& query, F&& callback)
        {
            size_t result = 0;
            std::shared_lock r_guard(_buckets_acc);
            for (auto& bucket : _buckets)
            {
                if (bucket._status.load(std::memory_order_acquire) != Status::valid)
                    continue; //continue scan
                if (!std::apply([&](const auto& ...indexer) -> bool {
                        return (_call_indexer_check(indexer, query) && ...);
                    }, bucket._indexers))
                    continue;
                 
                if (auto inc = bucket.soft_remove_if_all(callback); inc)
                {
                    result += inc;
                    if(bucket._status.load(std::memory_order_acquire) == Status::garbage) //bucket reached empty state
                    {
                        _empty_buckets.fetch_add(1, std::memory_order_release);
                    }
                }
            }
            return result;
        }
        
        /** \brief Remove Buckets labeled as garbage 
        *
        *   \param limit_remove_by - restrict number of buckets evicted for deletion. 
        *           May be useful to reduce mutex capture time. Default value is std::numeric_limits<size_t>::max().
        *
        *   \return number of erased buckets.
        */
        size_t clean(size_t limit_remove_by = std::numeric_limits<size_t>::max())
        {
            size_t result = 0;
            std::unique_lock w_guard(_buckets_acc);
            for (auto i = _buckets.begin(); result < limit_remove_by && i != _buckets.end(); )
            {
                if (i->_status.load(std::memory_order_acquire) == Status::garbage)
                {
                    i = _buckets.erase(i);
                    ++result;
                    _empty_buckets.fetch_sub(1, std::memory_order_release);
                }
                else
                    ++i;
            }
            return result;
        }

        void clear()
        {
            _empty_buckets.store(0, std::memory_order_release);
            std::unique_lock w_guard(_buckets_acc);
            _buckets.clear();
        }
    };

}//ns:

#endif //_OP_VTM_MANAGERS_BUCKETINDEXED_LIST__H_
