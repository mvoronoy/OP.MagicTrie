#pragma once

#ifndef _OP_COMMON_THREAD_POOL__H_
#define _OP_COMMON_THREAD_POOL__H_

#include <iostream>
#include <vector>
#include <deque>
#include <thread>
#include <functional>
#include <future>
#include <op/common/SysLog.h>
#include <op/common/StackAlloc.h>

namespace OP::utils
{
    using namespace std::string_literals;

    /** 
    * Implements thread pool with reusable thread.
    *
    *  In compare with std::async thread pool (tp) allows reuse already allocated threads. Be accurate 
    *  with use of thread-local variables.
    * There are 2 ways to involve job to tp:
    * \li ThreadPool::async  - that is similar how std::async works and the caller have to care 
    *   about returned std::future value. Because without managing return value code: 
    *   \code
    *       thread_pool.async([](){ ... some long proc ... });
    *   \endcode
    *   will wait until "some long proc" complete.
    * \li ThreadPool::one_way - allows create background job without waiting accomplishment of 
    *   result value.
    */
    class ThreadPool
    {
        template< class F, class... Args>
        using function_res_t = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        struct Task
        {
            virtual ~Task() = default;
            virtual void run() = 0;
        };

        template< class F>
        struct Functor : public Task
        {
            using result_t = function_res_t<F>;
            using this_t = Functor<F>;

            Functor(F&& f)
                : _f(std::move(f))
            {
            }

            Functor(const Functor&) = delete;

            std::future<result_t> get_future()
            {
                return _promise.get_future();
            }

            void run() override
            {
                try
                {
                    if constexpr (std::is_void_v< result_t >)
                    {
                        std::invoke(_f);
                        _promise.set_value();
                    }
                    else // result is non-void
                    {
                        _promise.set_value(std::invoke(_f));
                    }
                }
                catch (const std::exception& ex)
                {
                    pack_exception();
                    OP::utils::SysLog log;
                    log.print("Unhandled thread exception hides that:");
                    log.print(ex.what());
                }
                catch (...)
                {
                    pack_exception();
                }
            }

        private:

            void pack_exception()
            {
                try
                {
                    // store anything thrown in the promise
                    _promise.set_exception(std::current_exception());
                }
                catch (...)
                { // set_exception() may throw too
                    OP::utils::SysLog log;
                    log.print("Unhandled thread exception has been hiden");
                }
            }

            std::promise<result_t> _promise;
            F _f;
        };

        template <class F>
        struct Action : Task
        {
            Action(F&& f)
                : _f(std::move(f))
            {
            }

            void run() override
            {
                _f();
            }
            F _f;
        };

        using task_t = std::unique_ptr<Task>;

        using thread_t = std::thread;
        using tstore_t = std::vector<thread_t>;
        using task_list_t = std::deque< task_t >;
        using guard_t = std::lock_guard<std::mutex>;
        using uniq_guard_t = std::unique_lock<std::mutex>;

        tstore_t _thread_depo;
        std::mutex _acc_thread_depo;

        task_list_t _task_list;
        std::mutex _acc_tasks;
        std::condition_variable _cv_task;

        const unsigned _grow_factor;
        std::atomic< size_t > _busy, _allocated, _task_count;
        std::atomic<bool> _end = false;

    public:

        /** Allocate thread pool
        *   \param initial - number of threads to allocate at constructor time;
        *   \param grow_by - number of threads to allocate after exhausting available threads. 
        *                   If 0 is specified thread pool will not grow and unhandled jobs will be queued.
        */
        ThreadPool(size_t initial, unsigned grow_by = 0)
            : _grow_factor(grow_by)
            , _busy( 0 )
            , _allocated( 0 )
            , _task_count(0)
        {
            allocate_thread(initial);
        }

        /** Construct thread pool with initial number of threads equal to 
        * `std::thread::hardware_concurrency()` number, grow factor set to 0.
        */ 
        ThreadPool()
            : ThreadPool(std::thread::hardware_concurrency(), 0)
        {
        }

        /** not copy able */
        ThreadPool(const ThreadPool&) = delete;

        /** At exit pool will join without queued task completion */
        ~ThreadPool()
        {
            join(); 
        }
        
        /** Place job to thread pool without waiting accomplishment 
        * \tparam F - function to execute in thread pool. If return value is needed use `async` method instead.
        * \tparam Args - variadic optional args for function F
        */
        template <class F, class... Args>
        void one_way(F&& f, Args&&... args)
        {
            ensure_workers();
            auto action_bind = make_bind(std::forward<F>(f), std::forward<Args>(args)...);
            put_task(
                task_t{ new Action<decltype(action_bind)>(std::move(action_bind)) }
            );
        }

        /**
        *  Start function in background on the pool of available threads.
        * \return std::future to control completion and get result or exception.
        */
        template< class F, class... TArgs>
        [[nodiscard]] std::future<function_res_t<F, TArgs...> > async( F&& f, TArgs&&... args )
        {
            ensure_workers();
            using bind_t = decltype(make_bind(std::forward<F>(f), std::forward<TArgs>(args)...));
            using task_impl_t = Functor<bind_t>;
            auto impl = std::unique_ptr<task_impl_t>{
                new task_impl_t(
                    make_bind(std::forward<F>(f), std::forward<TArgs>(args)...)
                )
            };

            auto result = impl->get_future();
            put_task( std::move(impl) );
            return result;
        }
        
        /** Join all allocated threads. If some task was queued but not started, 
        * it will remain non started.
        */
        void join()
        {
           _end = true;
           _cv_task.notify_all();
           guard_t g(_acc_thread_depo);
           for(auto& t : _thread_depo)
               t.join();
           _thread_depo.clear();
        }

    private: 
        
        template <class F, class ...TArgs>
        static auto make_bind(F&& f, TArgs&&... args)
        {
            return 
            [f = std::forward<F>(f), ax = std::make_tuple(std::forward<TArgs>(args) ...)]
            () mutable -> auto
                {
                    return std::apply([&](auto& ... x) {
                        //this function can be called only once, so use std::move instead forward
                        return f(std::move(x)...);
                        }, ax);
                };
        }

        template <class TaskPtr>
        void put_task(TaskPtr&& t)
        {
            uniq_guard_t g(_acc_tasks);
            _task_list.emplace_back(std::move(t));
            g.unlock();
            _cv_task.notify_one();
            ++_task_count;
        }
        
        static void thread_routine(ThreadPool *owner)
        {
            while(!owner->_end)
            {
                uniq_guard_t g(owner->_acc_tasks);
                
                if(owner->_task_list.empty())
                    owner->_cv_task.wait(g, [owner]{return owner->_end || !owner->_task_list.empty();});
                
                if( owner->_task_list.empty() )
                    continue;
                
                ++owner->_busy;
                auto task = std::move(owner->_task_list.front());
                owner->_task_list.pop_front();
                --owner->_task_count;
                g.unlock();
                
                try
                {
                    task->run();
                } catch(...)
                {
                    using namespace std::string_literals;
                    //hard intercept all errors
                    OP::utils::SysLog log;
                    log.print("Unhandled thread exception from thread pool, hide it");
                }
                --owner->_busy;
            }
        }
       
        void ensure_workers()
        {
            if(_grow_factor && ((_allocated -_busy) < _task_count ) )
                allocate_thread(_grow_factor);
        }
        
        void allocate_thread(size_t n)
        {
            guard_t g(_acc_thread_depo);
            _thread_depo.reserve(_thread_depo.size() + n);
            for(; n; --n)
            {
                _thread_depo.emplace_back(
                    std::thread(thread_routine, this));
                ++_allocated;
            }
        }
    };

} //ns:OP::utils

#endif //_OP_COMMON_THREAD_POOL__H_
