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
    template <class T, class TDeleter, class ...TIndexer>
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
            using item_t = std::unique_ptr<T, TDeleter>;

            constexpr static usize_t capacity_c = 8;
            std::shared_mutex _data_acc;
            std::atomic<usize_t> _presence = 0;
            std::atomic<Status> _status{ Status::valid };
            /** indicate size occupied so far */
            std::atomic<usize_t> _size = 0;
            std::array<item_t, capacity_c> _data = _init_array(std::make_index_sequence<capacity_c>{});

            indexers_t _indexers;
            template <size_t ...Ix>
            static constexpr auto _init_array(std::index_sequence<Ix...>) {
                const auto mk_item = [](auto) {
                    return item_t{ nullptr, TDeleter{} };
                    };
                return std::array<item_t, capacity_c> { mk_item(Ix/*formal argument*/)... };
            }
            Bucket(std::in_place_t) {}
            Bucket(const Bucket&) = delete;
            
            ////bucket moved only once on construction by std::list, so no contribution to lock by `_data_acc`
            //Bucket(Bucket&& other) noexcept
            //    : _presence(other._presence)
            //    , _status(other._status)
            //    , _size(other._size)
            //    , _data(std::move(other._data))
            //    , _indexers(std::move(other._indexers))
            //{
            //}

            ~Bucket()
            {
                //std::unique_lock guard(_data_acc);
                //for (auto& ref: _data)
                //{
                //    ref.reset()
                //}
            }
            /** \tparam F must be a functor: `bool(T*, usize_t position)` */
            template <class F>
            bool bunch_for_each(F&& callback)
            {
                std::shared_lock guard(_data_acc);
                usize_t local_mask = _presence.load(std::memory_order_acquire);
                for (auto i = 0; i < capacity_c && local_mask; local_mask = (_presence.load(std::memory_order_acquire) >> ++i))
                {
                    if (local_mask & 1)
                    {
                        auto* value = _data[i].get();
                        assert(value);//mask must grant value exists
                        if (!callback(*value, i))
                            return false; //forced stop
                    }
                }
                return true; //don't stop
            }

            bool append(std::unique_ptr<T, TDeleter>& item)
            {
                //quick check without locking on atomic variables
                if (_status.load(std::memory_order_acquire) != Status::valid 
                    || _size.load(std::memory_order_acquire) == capacity_c)
                    return false;
                
                std::unique_lock wr_guard(_data_acc);
                auto ins_index = _size.load(std::memory_order_acquire);
                if (_status.load(std::memory_order_acquire) != Status::valid 
                    || ins_index == capacity_c)
                    return false;
                _data[ins_index] = std::move(item);
                T* new_data = _data[ins_index].get();
                //update indexers to include new item
                std::apply([&](auto& ...indexer) {
                    (indexer.index(*new_data), ...);
                    }, _indexers);
                _presence.fetch_or(usize_t(1) << ins_index, std::memory_order_release); //indicate item available for scan
                _size.fetch_add(1, std::memory_order_release);
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

            /** Mark for deletion first item where predicate F return true. Return true if item was matched 
            * with predicate and marked as removed, false when not found. 
            */
            template <class F>
            bool soft_remove_if_first(F&& predicate)
            {
                bool item_has_removed = false;
                bunch_for_each([&](T& value, usize_t i)->bool {
                    if (predicate(value))
                    {//clear presence
                        usize_t new_mask = ~(usize_t(1) << i);
                        if (!(new_mask & _presence.fetch_and(new_mask, std::memory_order_release))
                            && _size.load(std::memory_order_acquire) == capacity_c //don't allow erase bunch not fully occupied
                            )
                        {//indicate bunch available for later delete
                            _status.store(Status::garbage, std::memory_order_release);
                        }
                        item_has_removed = true;//stop at first
                    }
                    return !item_has_removed; //continue until first item been removed
                    });
                return item_has_removed;
            }

            template <class F>
            size_t soft_remove_if_all(F&& predicate)
            {
                size_t result = 0;
                bunch_for_each([&](T& value, usize_t i)->bool {
                    if (predicate(value))
                    {//clear presence
                        ++result;
                        usize_t new_mask = ~(usize_t(1) << i);
                        if (!(new_mask & _presence.fetch_and(new_mask, std::memory_order_release))
                            && _size.load(std::memory_order_acquire) == capacity_c //don't allow erase bunch not fully occupied
                            )
                        {//indicate bunch available for later delete
                            _status.store(Status::garbage, std::memory_order_release);
                            return false; //bucket is already empty, no need for loop
                        }
                        //go next step
                    }
                    return true;
                    });
                return result;
            }
            
        };

        using index_list_t = std::list<Bucket>;
        index_list_t _buckets;
        mutable std::shared_mutex _buckets_acc;
        std::atomic<size_t> _empty_buckets = 0;

        // just dummy stub to check if `index.check(args...)` is invocable.
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

        //fallback when index.check(T) is not supported, then it is always-true result
        template <class ...Tx>
        static bool _call_indexer_check(Tx&& ...)
        {
            return true;
        }
        /** \return true if all buckets were iterated and false if forcible stopped */
        template <template<typename ...> typename Guard, class F>
        bool for_each_bucket(F&& f)
        {
            Guard<std::shared_mutex> r_guard(_buckets_acc); //yes, even for soft-remove RO lock is enough
            for (auto& bucket : _buckets)
            {
                if (bucket._status.load(std::memory_order_acquire) == Status::valid
                    && !f(bucket))
                    return false;
            }
            return true;
        }

        template <template<typename ...> typename Guard, class Q, class F>
        bool indexed_for_each_bucket(const Q& query, F&& f)
        {
            return for_each_bucket<Guard>([&](Bucket& bucket) ->bool {
                if (std::apply([&](const auto& ...indexer) -> bool {
                    return (_call_indexer_check(indexer, query) && ...);
                    }, bucket._indexers)
                    && !f(bucket)
                    )
                    return false;
                else
                    return true;
            });
        }
    public:

        BucketIndexedList() = default;

        void append(std::unique_ptr<T, TDeleter> item)
        {
            //optimistic scenario, use RO only if existing bucket has capacity
            {
                std::shared_lock r_guard(_buckets_acc);
                if (!_buckets.empty() && _buckets.back().append(item))
                    return;
            }
            // pessimistic scenario to change the bucket-list
            std::unique_lock w_guard(_buckets_acc);
            for (auto* current = &(_buckets.empty() ? _buckets.emplace_back(std::in_place) : _buckets.back());
                !current->append(item); )
            {
                current =  &_buckets.emplace_back(std::in_place);
            } 
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
            for_each_bucket<std::shared_lock>([&](Bucket& bucket)->bool {
                return bucket.bunch_for_each([&](T& value, typename Bucket::usize_t)->bool {
                    if constexpr (std::is_convertible_v<decltype(callback(value)), bool>)
                    {//callback support stop iteration result
                        return callback(value);
                    }
                    else
                    {
                        callback(value);
                        return true;
                    }
                    });
                });
        }

        template <class Query, class F>
        void indexed_for_each(const Query& query, F&& callback)
        {
            const auto callback_adapter = [&](T& value, typename Bucket::usize_t)->bool {
                if constexpr (std::is_convertible_v<decltype(callback(value)), bool>)
                {//callback support stop iteration result
                    return callback(value);
                }
                else
                {
                    callback(value);
                    return true;
                }
            };
            indexed_for_each_bucket<std::shared_lock>(query, [&](Bucket& bucket)->bool {
                return bucket.bunch_for_each(callback_adapter);
            });
        }

        template <class Query, class F>
        bool soft_remove_if_first(const Query& query, F&& callback)
        {
            //indication of remove is forcible stop
            return !indexed_for_each_bucket<std::shared_lock>(query, //YES, for soft-remove shared lock is used
                [&](Bucket& bucket)->bool {

                    if (bucket.soft_remove_if_first(callback))
                    {
                        if (bucket._status.load(std::memory_order_acquire) == Status::garbage) //bucket reached empty state
                        {
                            _empty_buckets.fetch_add(1, std::memory_order_release);
                        }
                        return false;
                    }
                    return true;
                });
        }

        template <class Query, class F>
        [[maybe_unused]] size_t soft_remove_if_all(const Query& query, F&& callback)
        {
            size_t result = 0;
            indexed_for_each_bucket<std::shared_lock>(query, //YES, for soft-remove shared lock is used
                [&](Bucket& bucket)->bool {
                    if (auto inc = bucket.soft_remove_if_all(callback); inc)
                    {
                        result += inc;
                        if (bucket._status.load(std::memory_order_acquire) == Status::garbage) //bucket reached empty state
                        {
                            _empty_buckets.fetch_add(1, std::memory_order_release);
                        }
                    }
                    return true;//always return true for all remove
            });
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
            std::unique_lock w_guard(_buckets_acc);
            _buckets.clear();
            _empty_buckets.store(0, std::memory_order_release);
        }
    };

}//ns:

#endif //_OP_VTM_MANAGERS_BUCKETINDEXED_LIST__H_
