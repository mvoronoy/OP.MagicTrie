#pragma once
#ifndef _OP_FLUR_QUEUESEQUENCE__H_
#define _OP_FLUR_QUEUESEQUENCE__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/common/Utils.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRange.h>
#include <op/flur/SimpleFactory.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    /** Provide queue implmentation that can be used as a source async element 
        of a flur pipeline.
        Usage example:
        \code
            QueueSequence<int> queue;

            auto process = std::async([&]()
                (src::outer(std::ref(queue))  //avoid copying the queue, use by ref
                >> then::mapping([](int a){
                    return a * a + 1;
                })
                ) . for_each([](int a){ std::cout << "n ^2 +1=" << a << std::endl; });
            queue.push(100);
            queue.push(101);
            queue.push(102);
            queue.stop();
            process.get();
        \encode

        Queue implmentation cannot be re-started second time, if needed use `repeater()` 
        element:
        \code
            QueueSequence<int> queue;
            ...
            src::outer(std::ref(queue)) >> then::repeater() ...
        \endcode

    */
    template <class T>
    struct QueueSequence : OP::flur::Sequence<T>
    {
        using element_t = T;
        using container_t = std::deque<T>;
        using guard_t = std::unique_lock<std::mutex>;

        container_t _queue;
        mutable std::mutex _m;
        mutable std::condition_variable _access_condition;
        std::atomic_bool _stop = false;
        std::atomic<size_t> _count_cache = 0;
        
        /**
        *   Thread safe method to push element to queue from any thread
        * \param v value to add
        */
        template <class ...Tx>
        void push(Tx&& ...values)
        {
            if (_stop)
                throw std::logic_error("push data after 'stop' has been invoked");

            guard_t g(_m);
            ((_queue.emplace_back(std::forward<Tx>(values))), ...);
            _count_cache = _queue.size();
            g.unlock();
            _access_condition.notify_one();
        }

        /** Thread safe method to notify that no more items will be posted to this queue */
        void stop()
        {
            _stop.store(true);
            _access_condition.notify_one();
        }

        /** Do nothing */
        virtual void start() override
        {
        }

        /** Check if Sequence is in valid position and may call `next` safely */
        virtual bool in_range() const override
        {
            if (_count_cache.load() == 0)
            {
                guard_t g(_m);
                _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
                return !_queue.empty();
            }
            else
                return true;
        }
        
        /** Return copy of item on front of queue*/
        virtual element_t current() const override
        {
            guard_t g(_m);
            if (_count_cache.load() == 0)
            {
                _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
            }
            if (_stop && _queue.empty())
                throw std::out_of_range("queue is empty and stop flag been used");
            return _queue.front();
        }
        
        /** Position iterable to the next step */
        virtual void next() override
        {
            guard_t g(_m);
            _queue.pop_front();
            _count_cache = _queue.size();
            g.unlock();
            _access_condition.notify_one();
        }

    private:
        struct PopGuard : guard_t
        {
            container_t& _queue;
            bool _must_pop;

            PopGuard(std::mutex& m, container_t& queue)
                : guard_t(m)
                , _queue(queue)
                , _must_pop(true)
            {
            }

            ~PopGuard()
            {
                if (_must_pop)
                    _queue.pop_front();
            }
        };

    };

    /** namespace for function that are source of LazyRange */
    namespace src
    {
        template <class T>
        constexpr auto outer(T t) noexcept
        {
            return make_lazy_range(SimpleFactory<T, T>(std::move(t)));
        }
        
    }


} //ns:OP::flur

#endif //_OP_FLUR_QUEUESEQUENCE__H_
