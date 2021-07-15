#pragma once
#ifndef _OP_FLUR_QUEUESRC__H_
#define _OP_FLUR_QUEUESRC__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/common/Utils.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRange.h>
#include <op/flur/SimpleFactory.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** Provide queue implmentation that can be used as a source async element 
        of a flur pipeline.
        Usage example:
        \code
            QueueSrc<int> queue;

            auto process = std::async([&]()
                (src::outer(std::ref(queue))  //don't copy queue, use by ref
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
            QueueSrc<int> queue;
            ...
            src::outer(std::ref(queue)) >> then::repeater() ...
        \endcode

    */
    template <class T>
    struct QueueSrc : OP::flur::Sequence<T>
    {
        std::deque<int> _queue;

        mutable std::mutex _m;
        using guard_t = std::unique_lock<std::mutex>;
        mutable std::condition_variable _access_condition;
        std::atomic_bool _stop = false;

        QueueSrc()
        {
        }
        /**
        *   Thread safe method to push element to queue from any thread
        * \param v value to add
        */
        void push(T v)
        {
            if (_stop)
                throw std::logic_error("push data after 'stop' has been invoked");

            guard_t g(_m);
            _queue.emplace_back(std::move(v));
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
        virtual void start()
        {
        }
        /** Check if Sequence is in valid position and may call `next` safely */
        virtual bool in_range() const
        {
            guard_t g(_m);
            if (_queue.empty())
            {
                _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
                return !_queue.empty();
            }
            else
                return true;
        }
        /** Return current item */
        virtual element_t current() const
        {
            guard_t g(_m);
            if (_queue.empty())
            {
                _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
                return _queue.front();
            }
            else
                return _queue.front();
        }
        /** Position iterable to the next step */
        virtual void next()
        {
            guard_t g(_m);
            if (_queue.empty())
            {
                _access_condition.wait(g, [this] {return _stop || !_queue.empty(); });
                _queue.pop_front();
            }
            else
                _queue.pop_front();
        }
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


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_QUEUESRC__H_
