#pragma once
#ifndef _OP_FLUR_MINIBATCH__H_
#define _OP_FLUR_MINIBATCH__H_

#include <functional>
#include <memory>
#include <optional>
#include <future>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    namespace details 
    {
        template <class T, size_t N>
        struct CyclicBuffer
        {
            using this_t = CyclicBuffer<T, N>;

            constexpr CyclicBuffer() noexcept = default;
            
            CyclicBuffer(CyclicBuffer&& other) noexcept
            {
                while ( other._r_current < other._w_current)
                    _buffer[_w_current++] = std::move(other._buffer[other._r_current++ % N]);
            }

            bool has_elements() const
            {
                return _r_current < _w_current;
            }

            void emplace(T&& t)
            {
                if ((_w_current - _r_current) < N) //prevent outrun by 1 cycle
                {
                    _buffer[_w_current++ % N] = std::move(t);
                    return;
                }
                throw std::out_of_range("CyclicBuffer::emplace");
            }

            const T& pick() const
            {
                if(_r_current < _w_current)
                    return _buffer[_r_current % N];
                throw std::out_of_range("CyclicBuffer::pick");
            }

            void next()
            {
                if (_r_current < _w_current)
                {
                    ++_r_current;
                    return;
                }
                throw std::out_of_range("CyclicBuffer::next");
            }

        private:
            std::atomic_size_t _r_current = 0, _w_current = 0;
            std::array<T, N> _buffer = {};
        };

    }//ns:details

    /**
    *   Minibatch async intermediate part that can be used to smooth delay between 
    *   slow producer and fast consumer.
    *   Minibatch introduce internal buffer of elements that are populated on 
    *   background with std::async jobs. From design perspective this class introduce 
    *   minimal synchronization footprint by avoiding locks.
    */
    template <class TThreads, class Base, class Src, size_t N>
    class Minibatch : public Base
    {
        static_assert(N >= 2, "to small N for drain buffer");

    public:
        using this_t = Minibatch<TThreads, Base, Src, N>;

        using base_t = Base;
        using src_element_t = typename Src::element_t;
        using element_t = typename base_t::element_t;

    private:
        using background_t = std::future<void>;

        Src _src;
        TThreads& _thread_pool;
        details::CyclicBuffer< src_element_t, N> _batch;

        background_t _work;
        mutable std::mutex _work_acc;

        /**Apply move operator to source, but wait until all background work done*/
        Src&& safe_take()
        {
            std::lock_guard guard(_work_acc);
            if (_work.valid())
                _work.get();
            return std::move(_src);
        }

    public:
        constexpr Minibatch(Src&& src, TThreads& thread_pool) noexcept
            : _src(std::move(src))
            , _thread_pool(thread_pool)
        {
        }

        Minibatch(const this_t& src) noexcept = delete;

        constexpr Minibatch(Minibatch&& src) /*noexcept - safe_take may raise*/
            : _src(src.safe_take())
            , _batch(std::move(src._batch))
            , _thread_pool(src._thread_pool)
        {
        }

        Minibatch& operator = (const this_t& src) = delete;
        Minibatch& operator = (this_t&& src)
        {
            _src = std::move(src.safe_take());
            _batch = std::move(src._batch);
            return *this;
        }

        virtual void start() override
        {
            _src.start();
            drain(1);
            if (_src.in_range())
            {
                async_drain(N - 1);
            }
        }

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override 
        {
            return _src.is_sequence_ordered();
        }

        /** Check if Sequence is in valid position and may call `next` safely */
        virtual bool in_range() const override
        {
            if (_batch.has_elements())
                return true;
            //cover potential issue: t0{r == w}, t1{drain, now r < w}, t1{src_in_range = false, but r < w}
            std::lock_guard guard(_work_acc);
            if (_work.valid())
            {
                const_cast<background_t&>(_work).get();
                return _batch.has_elements();
            }
            return false;
        }

        /** Return current item */
        virtual element_t current() const override
        {
            return _batch.pick();
        }

        /** Position iterable to the next step */
        virtual void next() override
        {
            if (_batch.has_elements())
            {
                _batch.next();
                async_drain(1);
            }
            else
            {
                std::lock_guard guard(_work_acc);
                if (_work.valid())
                {
                    _work.get();
                    if (_batch.has_elements())
                        return;
                }
                throw std::out_of_range("fail on next");
            }
        }

    private:
        void drain(size_t lim)
        {
            for (; lim && _src.in_range(); _src.next(), --lim)
            {
                _batch.emplace(std::move(_src.current()));
            }
        }
        void async_drain(size_t lim)
        {
            std::lock_guard guard(_work_acc);
            if (_work.valid()) //something already in progress
                _work = std::move(
                    _thread_pool.async([this](size_t lim2, background_t b) {
                        b.get();
                        drain(lim2);
                    }, lim, std::move(_work)));
            else //no previous job
                _work = std::move(
                    _thread_pool.async([this](size_t lim2) {
                        drain(lim2);
                        }, lim)
                );
        }
    };

    /**
    * Factory for Minibatch template
    * 
    * \tparam N - number of items in intermedia buffer. 
    * \tparam TThreads - type of thread-pool conception, this must support `async` method
    *                   with signatire similar to std::async that returns std::future object.
    *                   As a default implementation you can use OP::utils::ThreadPool
    */
    template <size_t N, class TThreads>
    struct MinibatchFactory : FactoryBase
    {
        
        explicit MinibatchFactory(TThreads& thread_pool)
            :_thread_pool(thread_pool)
        {}

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using base_minibatch_t = 
                Sequence< const typename Src::element_t& >;

            return Minibatch<TThreads, base_minibatch_t, Src, N>(std::move(src), _thread_pool);
        }

    private:
        TThreads& _thread_pool;
    };


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_MINIBATCH__H_
