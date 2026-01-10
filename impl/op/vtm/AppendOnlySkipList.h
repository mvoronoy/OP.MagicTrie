#pragma once

#ifndef _OP_VTM_APPENDONLYSKIPLIST__H_
#define _OP_VTM_APPENDONLYSKIPLIST__H_

#include <memory>
#include <mutex>
#include <array>

#include <op/common/ValueGuard.h>

#include <boost/lockfree/spsc_queue.hpp>

#include <op/vtm/AppendOnlyLog.h>

namespace OP::vtm
{
    namespace bip = boost::interprocess;

    /** Controls bucket lookup behavior for indexed navigation */
    enum class BucketNavigation
    {
        /** tells scanner that current bucket doesn't contain expected and need go next */
        next,
        /** tells scanner to stop */
        stop,
        /** tells scanner that bucket potentially contains expected items */
        worth,
        /** similar to `worth` but tells scanner that other indexer also should be applied */
        not_sure
    };
    /**
    *   \brief On top of AppendOnlyLog allows create thread safe indexed access improvement using skip-list conception.
    *
    * All records are appended to Skip List must evaluate hash code that creates small Bloom filter demarcating
    *   each bucket. Implementation allows you iterate items in 2 ways:
    *   - indexed, when a lot of items are filtered out but you still need doublecheck item;
    *   - plain, just all stored items.
    * In any case iteration happens in strong history order as items were appended.

    *   \tparam TIndexer ... - user defined types to conduct item indexing operations. This must follow the
    *       next design rules:
    *   \code
    * struct UserIndexer
    * {
    *       UserIndexer() = default; // default constructible
    *
    *       std::uint64_t _some_state = 0; //some plain type to persist state inside single bucket
    *       ... //more states if needed
    *
    *       ///
    *       /// method to index new item and save index in state members, is called strongly under mutex lock.
    *       ///
    *       template <class T>
    *       void index(const T& item_to_index) ...
    *
    *       ///
    *       /// method to check if bucket worth to check
    *       ///
    *       template <class T>
    *       BucketNavigation check(const T& item) const ...
    *
    * };
    * \endcode
    */
    template <std::uint16_t bucket_size_c, class T, class ... TIndexer>
    class AppendOnlySkipList 
        : public std::enable_shared_from_this<AppendOnlySkipList<bucket_size_c, T, TIndexer...> >
    {
        using w_guard_t = std::unique_lock<std::shared_mutex>;
        using r_guard_t = std::shared_lock<std::shared_mutex>;

        struct Header
        {
            FarAddress _start_bucket, _last_bucket;
            std::uint64_t _size = 0;

            explicit Header(FarAddress last_bucket, std::uint64_t size)
                : _start_bucket(last_bucket)
                , _last_bucket(last_bucket)
                , _size(size)
            {
            }
        };

        struct Bucket
        {
            constexpr static size_t capacity_c = bucket_size_c;
            constexpr static size_t buffer_size_c = capacity_c * sizeof(T);

            using indexers_t = std::tuple<TIndexer...>;

            /** unique order of bucket in the the total list */
            const std::uint64_t _order = 0; 
            indexers_t _indexers{}; //all indexers must be default constructible
            FarAddress _next_bucket{};
            //
            // simulate c++26 std::inplace_vector:
            //
            /** occupied size so far  */
            std::uint16_t _size = 0;
            /** raw buffer for persisted types */
            alignas(T) unsigned char _buffer[buffer_size_c]{};

            Bucket() = default;

            explicit Bucket(std::uint64_t order) noexcept
                :_order(order)
            {
            }

            template <class ...Ax>
            [[maybe_unused]] T* emplace(Ax&& ...ax) noexcept(noexcept(T(std::forward<Ax>(ax)...)))
            {
                assert(_size < capacity_c);
                T* all = reinterpret_cast<T*>(_buffer);
                return ::new (all + _size++) T(std::forward<Ax>(ax)...);
            }

            T* data() noexcept
            {
                return reinterpret_cast<T*>(_buffer);
            }

            const T* data() const noexcept
            {
                return reinterpret_cast<const T*>(_buffer);
            }

            T& at(size_t i) noexcept
            {
                assert(i < _size);
                T* all = reinterpret_cast<T*>(_buffer);
                return *(all + i);
            }

            const T& at(size_t i) const noexcept
            {
                return const_cast<Bucket*>(this)->at(i);
            }

            constexpr bool has_capacity() const noexcept
            {
                return _size < capacity_c;
            }
        };

        using bucket_t = Bucket;
        using bucket_range_t = std::pair<T*, T*>;

        struct BucketSequence : OP::flur::Sequence<const bucket_t*>
        {
            std::shared_ptr<AppendOnlySkipList> _owner;
            const FarAddress _start_bucket;
            const bucket_t *_current;

            BucketSequence(std::shared_ptr<AppendOnlySkipList> owner, FarAddress start_bucket) noexcept
                : _owner(std::move(owner))
                , _start_bucket(start_bucket)
                , _current(nullptr)
            {
            }

            /** Start iteration from the beginning. If iteration was already in progress it resets.  */
            virtual void start()
            {
                _current = nullptr;
                auto [guard, from] = _owner->bucket_read(_start_bucket);
                _current = from;
            }
            
            virtual bool in_range() const noexcept
            {
                return _current != nullptr;
            }
            
            virtual const bucket_t* current() const noexcept
            {
                assert(_current);
                return _current;
            }
            
            virtual void next()
            {
                assert(_current);
                auto current_guard(_owner->shared_mutex(_current));
                if (!_current->_next_bucket.is_nil())
                    _current = _owner->_append_log->at<Bucket>(_current->_next_bucket);
                else
                    _current = nullptr;
            }

        };

        std::shared_ptr<AppendOnlyLog> _append_log;
        Header& _header;
        mutable std::shared_mutex _header_acc;

        mutable std::array<std::shared_mutex, 7> _bucket_acc;

        constexpr auto for_each_bucket() const
        {
            std::shared_ptr<this_t> zhis = std::const_pointer_cast<this_t>(shared_from_this());
            r_guard_t g(_header_acc); // just to read start pos

            return OP::flur::SimpleFactory<BucketSequence, std::shared_ptr<this_t>, FarAddress>{
                zhis, _header._start_bucket
            };
        }

        template <class Q>
        constexpr auto indexed_buckets(Q&& query) const
        {
            using namespace OP::flur;
            auto zhis = shared_from_this();
            return make_lazy_range(
                for_each_bucket(),
                then::filter([zhis, query = std::forward<Q>(query)](const bucket_t* bucket, SequenceState& sequence_state) ->bool {
                    BucketNavigation check_bucket = std::apply([&](const auto& ...indexer)->BucketNavigation {
                        BucketNavigation nav = BucketNavigation::worth;
                        (((nav = indexer.check(query)) == BucketNavigation::not_sure) && ...);
                        return nav;
                        }, bucket->_indexers);
                    switch (check_bucket)
                    {
                    case BucketNavigation::not_sure:
                        [[fallthrough]];
                    case BucketNavigation::worth:
                        return true;
                    case BucketNavigation::stop:
                        sequence_state.stop();//stop buckets iteration
                        break;
                    case BucketNavigation::next:
                        break;
                    }; //end:switch
                    return false; //reject item
                    })
            );
        }

        std::uint64_t bucket_size() const
        {
            r_guard_t g(_header_acc);
            return _header._size;
        }

        FarAddress start_bucket() const
        {
            r_guard_t g(_header_acc);
            return _header._start_bucket;
        }

    public:
        using this_t = AppendOnlySkipList;
        using element_t = T;

        using iterator = T*;
        using const_iterator = const T*;
        using a0log_ptr = std::shared_ptr<AppendOnlyLog>;

        constexpr static std::uint16_t bucket_capacity_c = bucket_size_c;

        static std::pair<FarAddress, std::shared_ptr<this_t>> create_new(a0log_ptr append_log)
        {
            auto [result_address, new_header] = append_log->construct<Header>(
                append_log->construct<Bucket>().first, 1
            );
            return { result_address,
                    std::shared_ptr<this_t>(
                        new this_t{
                            std::move(append_log),
                            *new_header
                        })
            };
        }

        static std::shared_ptr<this_t> open(std::shared_ptr<AppendOnlyLog> append_log, FarAddress previous_store)
        {
            auto persisted_header = append_log->at<Header>(previous_store);
            return std::shared_ptr<this_t>(
                new this_t{
                    std::move(append_log),
                    *persisted_header
                });
        }

        a0log_ptr append_log() const
        {
            return _append_log;
        }

        template <class ...Ux>
        void emplace(Ux&& ...args)
        {
            auto [wr_bucket_guard, bucket] = last_bucket();

            auto new_instance = bucket->emplace(std::forward<Ux>(args)...);
            //apply all registered indexers to the new item
            std::apply([&](auto& ...indexer) {
                (indexer.index(*new_instance), ...);
                }, bucket->_indexers);
        }

        /**
        *
        * \tparam FCallback A callback functor that can be invoked in one of two ways:
        *   - `f(const T&)` — called for each individual item in the bucket.
        *   - `f(InputIterator begin, InputIterator end)` — called once for a range of items.
        *   The callback return type may be:
        *     - `void` — indicating that the entire bucket should always be scanned.
        *     - A type convertible to `bool` — where `true` means continue scanning,
        *         and `false` means stop iteration early.
        *
        *   \return true if next bucket must be processed and false to early stop.
        */
        template <class Q, class FCallback>
        void indexed_for_each(const Q& query, FCallback&& f)
        {
            for_each_bucket([&](const Bucket& bucket) -> bool {
                return bucket_scan(bucket, query, f);
                });
        }

        /**
        * \tparam Q query object that can be processed by `TIndexer::check`. Must be copy or move constructible.
        */ 
        template <class Q>
        auto indexed_for_each(Q&& query) const
        {
            using namespace OP::flur;
            return indexed_buckets(std::forward<Q>(query))
                >> then::flat_mapping([zhis = shared_from_this()](const bucket_t* bucket) {
                    auto guard = zhis->shared_mutex(bucket);
                    return src::of_iterators(bucket->data(), bucket->data() + bucket->_size);
                });
        }

        template <class Q>
        struct AsyScan : OP::flur::Sequence<const bucket_t*>
        {
            std::shared_ptr<AppendOnlySkipList> _owner;
            boost::lockfree::spsc_queue<const bucket_t*, boost::lockfree::capacity<5> > _queue;
            std::future<void> _producer_job;
            std::atomic_bool _done;
            Q _query;

            AsyScan(std::shared_ptr<AppendOnlySkipList> owner, Q query) noexcept
                : _owner(std::move(owner))
                , _query(std::move(query))
            {
            }
            ~AsyScan()
            {
                _done = true;
                if (_producer_job.valid()) //may be previous iteration, wait before complete
                {
                    clear_queue();
                    _producer_job.get(); //ignore result
                }
            }
            AsyScan(const AsyScan& src) noexcept = delete;

            constexpr AsyScan(AsyScan&& src) /*noexcept - safe_take may raise*/
                : _owner(std::move(src._owner))
                , _query(std::move(src._query))
            {
            }


            void clear_queue()
            {
                const bucket_t* bucket = nullptr;
                while (_queue.pop(bucket))
                    static_cast<void>(bucket);
            }

            bool bucket_callback(const bucket_t* bucket)
            {
                BucketNavigation check_bucket = std::apply([&](const auto& ...indexer)->BucketNavigation {
                    BucketNavigation nav = BucketNavigation::worth;
                    (((nav = indexer.check(_query)) == BucketNavigation::not_sure) && ...);
                    return nav;
                    }, bucket->_indexers);
                switch (check_bucket)
                {
                case BucketNavigation::not_sure:
                    [[fallthrough]];
                case BucketNavigation::worth:
                    break;
                case BucketNavigation::next:
                    return true; //continue buckets iteration
                case BucketNavigation::stop:
                    return false; //stop buckets iteration
                }; //end:switch

                while (!_done && !_queue.push(bucket))
                    ;
                return true;
            }

            /** Start iteration from the beginning. If iteration was already in progress it resets.  */
            virtual void start()
            {
                if (_producer_job.valid()) //may be previous iteration, wait before complete
                {
                    _done = true;
                    clear_queue();
                    _producer_job.get(); //ignore result
                }
                _done = false;
                _producer_job = _owner->_append_log->thread_pool().async([this]() {
                    _owner->for_each_bucket([this](const bucket_t& bucket) { return bucket_callback(&bucket); });
                    _done = true;
                });
            }

            virtual bool in_range() const noexcept
            {
                while (!_done)
                {
                    if (_queue.read_available())
                        return true;
                }
                return _queue.read_available();
            }

            virtual const bucket_t* current() const noexcept
            {
                assert(_queue.read_available());
                return _queue.front();
            }

            virtual void next()
            {
                while (!_done)
                {
                    if (_queue.pop())
                        return;
                }
                assert(_queue.read_available());
                _queue.pop();
            }
        };

        template <class Q>
        auto async_indexed_for_each(Q&& query)
        {
            using namespace OP::flur;
            using unreq_query_t = std::decay_t<Q>;

            std::shared_ptr<this_t> zhis = shared_from_this();
            
            return make_lazy_range(
                OP::flur::SimpleFactory<AsyScan<unreq_query_t>, std::shared_ptr<this_t>, unreq_query_t>{
                    zhis, std::move(query)
                },
                then::flat_mapping([zhis = shared_from_this()](const bucket_t* bucket) {
                    auto guard = zhis->shared_mutex(bucket);
                    return src::of_iterators(bucket->data(), bucket->data() + bucket->_size);
                    })
            );

        }

        template <class Q, class FCallback>
        void async_indexed_for_each(Q&& query, FCallback&& callback)
        {
            constexpr size_t bucket_threshold_c = 3;
            if (bucket_size() <= bucket_threshold_c)
                return indexed_for_each(std::forward<Q>(query), std::forward<FCallback>(callback));
            boost::lockfree::spsc_queue<const bucket_t*, boost::lockfree::capacity<bucket_threshold_c> > queue;
            std::atomic_bool done = false;
            auto producer_job = _append_log->thread_pool().async([this]() {
                for_each_bucket([&](const bucket_t& bucket) { 
                    ValueGuard<std::atomic_bool, bool> turn_true(done, true); //ensure true at exit to indicate stop for outer thread
                    BucketNavigation check_bucket = std::apply([&](const auto& ...indexer)->BucketNavigation {
                        BucketNavigation nav = BucketNavigation::worth;
                        (((nav = indexer.check(query)) == BucketNavigation::not_sure) && ...);
                        return nav;
                        }, bucket->_indexers);
                    switch (check_bucket)
                    {
                    case BucketNavigation::not_sure:
                        [[fallthrough]];
                    case BucketNavigation::worth:
                        break;
                    case BucketNavigation::next:
                        return true; //continue buckets iteration
                    case BucketNavigation::stop:
                        return false; //stop buckets iteration
                    }; //end:switch

                    while (!done && !queue.push(bucket))
                        ;
                    return true;

                    });
                done = true;
            });
            auto consume_bucket = [this](const Bucket* bucket) {
                r_guard_t guard = shared_mutex(bucket);
                auto begin = bucket->data(), end = bucket->data() + bucket->_size;
                guard.unlock();
                call_lambda_scan(callback, begin, end);
                };
            while (!done)
            {
                queue.consume_all(consume_bucket);
            }

            producer_job.get();
            //take the rest of items
            queue.consume_all(consume_bucket);
        }

    protected:

        explicit AppendOnlySkipList(
            a0log_ptr append_log,
            Header& header
        )
            : _append_log(std::move(append_log))
            , _header(header)
        {
            //
        }

        
        template <class FCallback>
        void for_each_bucket(FCallback&& f)
        {
            r_guard_t header_guard(_header_acc);
            auto i = _header._start_bucket;
            header_guard.unlock();

            while(!i.is_nil())
            {
                Bucket* bucket = _append_log->at<Bucket>(i);
                r_guard_t guard = shared_mutex(bucket);
                if (!f(*bucket))
                    break;
                i = bucket->_next_bucket;
            }
        }

    private:
       
        r_guard_t shared_mutex(const Bucket* instance) const
        {
            return r_guard_t{ _bucket_acc.at(instance->_order % _bucket_acc.size()) };
        }

        std::pair<r_guard_t, const Bucket*> bucket_read(FarAddress addr) const
        {
            const Bucket* last_instance = _append_log->at<Bucket>(addr);
            return { shared_mutex(last_instance), last_instance };
        }

        std::pair<w_guard_t, Bucket*> bucket_write(FarAddress addr) const
        {
            Bucket* last_instance = _append_log->at<Bucket>(addr);
            return { w_guard_t{_bucket_acc.at(last_instance->_order % _bucket_acc.size())}, last_instance };
        }

        /**
        * \pre w-lock on _header_acc
        */
        std::pair<w_guard_t, Bucket*> last_bucket()
        {
            w_guard_t header_guard(_header_acc);
            auto [guard, last_instance] = bucket_write(_header._last_bucket);
            if (!last_instance->has_capacity())
            {//need new bucket
                Bucket* previous_bucket = last_instance;
                std::tie(_header._last_bucket, last_instance) = _append_log->construct<Bucket>(_header._size++);
                previous_bucket->_next_bucket = _header._last_bucket;
                return { w_guard_t{ _bucket_acc.at(last_instance->_order % _bucket_acc.size())},  last_instance };
            }
            //keep locked
            return { std::move(guard), last_instance };
        }

        template <class FCallback, class Iterator,
            std::enable_if_t<std::is_invocable_v<FCallback, Iterator, Iterator>, int> = 0>
        bool call_lambda_scan(FCallback& f, Iterator begin, Iterator end)
        {
            using lambda_res_t = std::decay_t<decltype(f(begin, end))>;

            if constexpr (std::is_convertible_v<lambda_res_t, bool>)
            {//lambda signature has an indicator of iteration stop
                return static_cast<bool>(f(begin, end));
            }
            else //lambda has no indicator to stop, just iterate all
            {
                f(begin, end);
                return true; //continue buckets iteration
            }
        }

        template <class FCallback, class Iterator,
            std::enable_if_t<std::is_invocable_v<FCallback, decltype(*std::declval<Iterator>())>, int> = 0>
        bool call_lambda_scan(FCallback& f, Iterator begin, Iterator end)
        {
            using lambda_res_t = std::decay_t<decltype(f(*begin))>;
            for (; begin != end; ++begin)
            {
                if constexpr (std::is_convertible_v<lambda_res_t, bool>)
                {//lambda signature has an indicator of iteration stop
                    if (!static_cast<bool>(f(*begin)))
                        return false;
                }
                else //lambda has no indicator to stop, just iterate all
                {
                    f(*begin);
                }
            }
            return true; //continue buckets iteration
        }

        template <class ...Ux>
        bool call_lambda_scan(Ux&&...)
        {
            static_assert(false,
                "callback supposed to match signature either f(const T&) or f(iterator, iterator)");
        }

        /**
        * Apply indexers to a single bucket, and if it matches the given `query`,
        * invoke the callback `f` on the bucket's items.
        *
        * \tparam FCallback A callback functor that can be invoked in one of two ways:
        *   - `f(const T&)` — called for each individual item in the bucket.
        *   - `f(InputIterator begin, InputIterator end)` — called once for a range of items.
        *   The callback return type may be:
        *     - `void` — indicating that the entire bucket should always be scanned.
        *     - A type convertible to `bool` — where `true` means continue scanning,
        *         and `false` means stop iteration early.
        *
        *   \return true if next bucket must be processed and false to early stop.
        */
        template <class FCallback>
        bool bucket_scan(const Bucket& bucket, const T& query, FCallback& f)
        {
            BucketNavigation check_bucket = std::apply([&](const auto& ...indexer)->BucketNavigation {
                BucketNavigation nav = BucketNavigation::worth;
                (((nav = indexer.check(query)) == BucketNavigation::not_sure) && ...);
                return nav;
                }, bucket._indexers);
            switch (check_bucket)
            {
            case BucketNavigation::not_sure:
                [[fallthrough]];
            case BucketNavigation::worth:
                break;
            case BucketNavigation::next:
                return true; //continue buckets iteration
            case BucketNavigation::stop:
                return false; //stop buckets iteration
            }; //end:switch
            auto begin = bucket.data(), end = bucket.data() + bucket._size;
            return call_lambda_scan(f, begin, end);
        }
    };

    constexpr inline std::uint16_t a0l_skip_list_bucket_size = 32;

    template <class T, class ...Tindexer>
    inline auto create_a0_skip_list(std::shared_ptr<AppendOnlyLog> append_log)
    {
        using skip_list_t = AppendOnlySkipList<a0l_skip_list_bucket_size, T, Tindexer...>;
        return skip_list_t::create_new(append_log);
    }

    template <class T, class ...Tindexer>
    inline auto open_a0_skip_list(std::shared_ptr<AppendOnlyLog> append_log, FarAddress list_position)
    {
        using skip_list_t = AppendOnlySkipList<a0l_skip_list_bucket_size, T, Tindexer...>;
        return skip_list_t::open(append_log, list_position);
    }

}//ns: OP::vtm

#endif //_OP_VTM_APPENDONLYSKIPLIST__H_
