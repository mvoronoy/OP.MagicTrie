#pragma once

#ifndef _OP_COMMON_THREAD_POOL__H_
#define _OP_COMMON_THREAD_POOL__H_

#include <iostream>
#include <vector>
#include <deque>
#include <thread>
#include <functional>
#include <future>

namespace OP{
namespace utils{

/** 
* Implements thread pool with reusable thread.
*
*  In compare with std::async thread pool (tp) allows reuse already allocated threads. Be accurate 
*  with use of thread-local variables.
* There are 2 ways to involve job to tp:
* \li ThreadPool::async  - that is similar how std::async works and the caller have to care about returned
*       std::future value. Because without managing return value code: \code
*       thread_pool.async([](){ ... some long proc ... });
*       \endcode
*       will wait until "some long proc" complete.
* \li ThreadPool::one_way - allows create background job without waiting acomplishment of result value
*/
class ThreadPool
{
    using thread_t = std::thread;
    using tstore_t = std::vector<thread_t>;
    
    struct Task
    {
        virtual ~Task() = default;
        virtual void run() = 0;
    };
    using task_t = std::unique_ptr< Task >;
    
    using task_list_t = std::deque< task_t >;
    
    using guard_t = std::lock_guard<std::mutex>;
    using uniq_guard_t = std::unique_lock<std::mutex>;
    
    tstore_t _thread_depo;
    task_list_t _task_list;
    std::mutex _acc_thread_depo, _acc_tasks;
    const unsigned _grow_factor;
    std::condition_variable _cv_task;
    std::atomic< size_t > _busy, _allocated, _task_count;
    std::atomic<bool> _end = false;
    template< class F, class... Args>
    using function_res_t = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    
    template< class F, class... Args>
    struct Functor : Task
    {
        using result_t = function_res_t<F, Args...> ;
        std::packaged_task<result_t()> _pack;
        Functor( F&& f, Args&&... args )
            : _pack(std::bind(std::forward<F>(f), std::forward<Args>(args)...))
            {}
        Functor(const Functor&) = delete;
        Functor(Functor&&other) noexcept 
            : _pack( std::move( other._pack ))
            {}
                
        
        std::future<result_t> get_future()
        {
            return _pack.get_future();
        }
        void run() override
        {
            _pack();
        }
    };
    
    template< class F, class... Args>
    struct F2 : Task
    {
        using result_t = function_res_t<F, Args...>;
        std::promise<result_t> _promise;
        F _f;
        std::tuple<Args ...> _args;
        F2(F&& f, Args&&... args)
            : _f(std::move(f)),
            _args(std::forward<Args>(args)...)
        {}
        F2(const F2&) = delete;

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
                    std::apply([this](Args &... tupleArgs) {
                        _f(std::forward<Args>(tupleArgs) ...);
                        _promise.set_value();

                        }, _args
                    );
                }
                else
                {
                    std::apply([this](Args &... tupleArgs) {
                        _promise.set_value(std::move(
                            _f(std::forward<Args>(tupleArgs) ...)));

                        }, _args
                    );
                }
            }
            catch (...)
            {
                try {
                    // store anything thrown in the promise
                    _promise.set_exception(std::current_exception());
                }
                catch (...) {} // set_exception() may throw too
            }
        }
    };

    template <class F>
    struct Action : Task
    {
        F _f;
        Action(F&& f)
           : _f(std::move(f))
        {}
        void run() override
        {
            _f();
        }
    };
    
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
    ThreadPool()
        : ThreadPool(std::thread::hardware_concurrency(), 0)
        {}

    /** not copiable */
    ThreadPool(const ThreadPool&) = delete;

    /** At exit pool will join without queued task completion */
    ~ThreadPool()
    {
        join(); 
    }
    
    
    /** Place job to thread pool without waiting acomplishment 
    * \tparam F - function to execute in thread pool. If return value is needed use `async` method instead.
    * \tparam Args - variadic optional args for function F
    */
    template <class F, class... Args>
    void one_way(F&& f, Args&&... args)
    {
        ensure_workers();
        auto lbind = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        task_t impl (new Action<decltype(lbind)>( std::move(lbind) ) );
        put_task(std::move(impl));
    }
    /**
    *  Start function in background and expose std::future to control completion.
    */
    template< class F, class... Args>
    std::future<function_res_t<F, Args...> > async( F&& f, Args&&... args )
    {
        ensure_workers();
        using result_t = function_res_t<F, Args...> ;
        //using task_impl_t = Functor<F, Args ...>;
        using task_impl_t = F2<F, Args ...>;
        std::unique_ptr<task_impl_t> impl (new task_impl_t(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<result_t> result = impl->get_future();
        put_task( std::move(impl) );
        return result; 
    }
    /** Join all allocated threads. If some tasks has been queued before then they are not completed */
    void join()
    {
       _end = true;
       _cv_task.notify_all(); 
       for(auto& t : _thread_depo)
           t.join();
    }
private: 
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
                owner->_cv_task.wait(g, [owner]{return  owner->_end || !owner->_task_list.empty();});
            
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
                //hard intercept all errors
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
} //ns:utils
} //ns:OP

#endif //_OP_COMMON_THREAD_POOL__H_
