#pragma once

#ifndef _OP_VTM_TRANSACTIONAL__H_
#define _OP_VTM_TRANSACTIONAL__H_

#include <type_traits>
#include <iostream>
#include <unordered_map>
#include <string>
#include <typeinfo>
#include <cassert>

#include <op/vtm/vtm_error.h>

namespace OP::vtm
{
    /**Exception is raised when impossible to obtain lock over memory block*/
    struct ConcurrentLockException 
    {
        ConcurrentLockException() = default;
        
        Exception to_exception()
        {
            return Exception(vtm::ErrorCodes::er_transaction_concurrent_lock);
        }
    };

    /**Handler allows intercept end of transaction*/
    struct BeforeTransactionEnd
    {
        virtual ~BeforeTransactionEnd() = default;
        virtual void on_commit() = 0;
        virtual void on_rollback() = 0;
    };

    /**
    *   Declare general definition of transaction as identifiable object with pair of operations commit/rollback
    */
    struct Transaction
    {
        using transaction_id_t = std::uint64_t;
        using ref_count_t = std::ptrdiff_t;

        friend struct TransactionGuard;

        Transaction() = delete;
        Transaction(const Transaction&) = delete;
        Transaction& operator = (const Transaction&) = delete;
        Transaction& operator = (Transaction&& other) = delete;

        //Transaction(Transaction && other) noexcept :
        //    _transaction_id(other._transaction_id)
        //{
        //}

        ref_count_t add_ref() noexcept
        {
            return ++_ref_count;
        }

        ref_count_t release() noexcept
        {
            auto r = --_ref_count;
            assert(r >= 0);
            if (!r)
                delete this;
            return r;
        }

        virtual transaction_id_t transaction_id() const noexcept
        {
            return _transaction_id;
        }

        /**Register handler that is invoked during rollback/commit process*/
        virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) = 0;
        virtual void rollback() = 0;
        virtual void commit() = 0;

    protected:
        Transaction(transaction_id_t id) :
            _transaction_id(id)
        {
        }
        virtual ~Transaction() noexcept = default;

    private:

        const transaction_id_t _transaction_id;
        std::atomic<ref_count_t> _ref_count = { 1 };
    };

    struct TransactionShareMode {};

    template <class T = Transaction>
    struct TransactionPtr
    {
        constexpr TransactionPtr() noexcept
            :_instance{ nullptr }
        {
        }

        constexpr explicit TransactionPtr(T* instance) noexcept
            :_instance{ instance }
        {
        }

        /** special constructor to capture instance with automatic `add_ref`.
        *
        */
        constexpr TransactionPtr(TransactionShareMode, T* instance) noexcept
            :_instance{ instance }
        {
            if (_instance)
                _instance->add_ref();
        }


        constexpr TransactionPtr(const TransactionPtr<T>& other) noexcept
            :_instance{ other.get() }
        {
            if (_instance)
                _instance->add_ref();
        }

        template <class U>
        constexpr TransactionPtr(const TransactionPtr<U>& other) noexcept
            :_instance{ other.get() }
        {
            if (_instance)
                _instance->add_ref();
        }

        template <class U>
        constexpr TransactionPtr(TransactionPtr<U>&& other) noexcept
            :_instance{ static_cast<T*>(other.detach()) }
        {
        }

        ~TransactionPtr() noexcept
        {
            destroy();
        }

        void destroy() noexcept
        {
            if (_instance)
            {
                _instance->release();
                _instance = nullptr;
            }
        }

        T* detach()
        {
            T* temp = _instance;
            _instance = nullptr;
            return temp;
        }

        template <class U>
        TransactionPtr& operator = (const TransactionPtr<U>& other) noexcept
        {
            if (other._instance)
                other._instance->add_ref();
            destroy();
            _instance = other._instance;
            return *this;
        }

        template <class U>
        TransactionPtr& operator = (TransactionPtr<U>&& other) noexcept
        {
            destroy();
            _instance = other.detach();
            return *this;
        }

        T* get() const noexcept
        {
            return _instance;
        }

        T* operator ->() noexcept
        {
            return _instance;
        }

        const T* operator ->() const noexcept
        {
            return _instance;
        }

        T& operator *() const noexcept
        {
            assert(_instance);
            return *_instance;
        }

        const T& operator *() noexcept
        {
            assert(_instance);
            return *_instance;
        }

        constexpr operator bool() const noexcept
        {
            return _instance;
        }

        constexpr bool operator !() const noexcept
        {
            return !_instance;
        }

        constexpr bool operator ==(std::nullptr_t) const noexcept
        {
            return !_instance;
        }

        constexpr bool operator !=(std::nullptr_t) const noexcept
        {
            return operator bool();
        }

        template <class U>
        auto static_pointer_cast() const& noexcept
        {
            using dest_type_t = std::remove_pointer_t<U>;
            using dest_ptr_t = std::add_pointer_t<U>;
            using dest_t = TransactionPtr<dest_type_t>;
            if (_instance)
            {
                _instance->add_ref();
                return dest_t{ static_cast<dest_ptr_t>(_instance) };
            }
            return dest_t{};
        }

        template <class U>
        auto static_pointer_cast() && noexcept
        {
            using dest_type_t = std::remove_pointer_t<U>;
            using dest_ptr_t = std::add_pointer_t<U>;
            using dest_t = TransactionPtr<dest_type_t>;
            if (_instance)
            {
                auto temp = _instance;
                _instance = nullptr;
                return dest_t{ static_cast<dest_ptr_t>(temp) };
            }
            return dest_t{};
        }

    private:
        T* _instance;
    };

    using transaction_ptr_t = TransactionPtr<Transaction>;

    /** No-op transaction implementation */
    struct NoOpTransaction : public Transaction
    {

        explicit NoOpTransaction(transaction_id_t id) noexcept
            : Transaction(id)
        {
        }

        virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
        {
            _end_listener.emplace_back(std::move(handler));
        }

        virtual void rollback() override
        {
            //invoke events on transaction end
            for (auto& ev : _end_listener)
            {
                ev->on_rollback();
            }
        }

        virtual void commit() override
        {
            //invoke events on transaction end
            for (auto& ev : _end_listener)
            {
                ev->on_commit();
            }
        }


    private:
        using listeners_t = std::vector<std::unique_ptr<BeforeTransactionEnd>>;
        listeners_t _end_listener;
    };

    /**
    *   Guard wrapper that grants transaction accomplishment.
    *   Depending on policy `commit_on_close` this RAII pattern implementation is responsible to
    *   automatically rollback or commit transaction at exit.
    */
    struct TransactionGuard
    {
        template <class Sh>
        explicit TransactionGuard(Sh&& instance, bool do_commit_on_close = false)
            : _instance(std::forward<Sh>(instance))
            , _is_closed(!_instance)//be polite to null transactions
            , _do_commit_on_close(do_commit_on_close)
        {
        }

        TransactionGuard(const TransactionGuard&) = delete;
        TransactionGuard& operator = (const TransactionGuard&) = delete;

        void commit()
        {
            if (!!_instance)//be polite to null transactions
            {
                assert(!_is_closed);
                _instance->commit();
                _is_closed = true;
            }
        }

        void rollback()
        {
            if (!!_instance)//be polite to null transactions
            {
                assert(!_is_closed);
                _instance->rollback();
                _is_closed = true;
            }
        }

        ~TransactionGuard()
        {
            if (!_is_closed)
                _do_commit_on_close ? _instance->commit() : _instance->rollback();
        }

    private:
        transaction_ptr_t _instance;
        bool _is_closed;
        bool _do_commit_on_close;
    };

    /**
    *   Utility to repeat some operation after ConcurrentLockException has been raised.
    *   Number of repeating peformed by N template parameter, after this number
    *   exceeding ConcurrentLockException exception just propagated to caller
    */
    template <auto N, typename  F, typename  ... Args>
    inline decltype(auto) transactional_retry_n(F f, Args ... ax) 
    {
        for (auto i = 0; i < N; ++i)
        {
            try
            {
                return f(ax...);//as soon this is a loop move/forward args must not be used
            }
            catch (const OP::vtm::ConcurrentLockException&)
            {
                /*ignore exception for a while*/
            }
        }
        throw Exception(vtm::ErrorCodes::er_transaction_concurrent_lock);
    }

    template <size_t N, typename F, typename  ... Args>
    inline decltype(auto) transactional_yield_retry_n(F f, Args ... ax)
    {
        static_assert(N > 0, "set number of retries greater than zero");
        constexpr size_t limit = N - 1;
        for (auto i = 0; i < limit; ++i)
        {
            try
            {
                return f(ax...);//as soon this is a loop move/forward args must not be used
            }
            catch (const OP::vtm::ConcurrentLockException&)
            {
                /*ignore exception for a while*/
                if ((i + 1) < limit)
                {
                    std::this_thread::yield();
                    continue;
                }
                throw;
            }
        }
        return f(ax...);
    }

    /** \brief calculate Bloom filter hash code for transaction id. 
    */
    constexpr inline std::uint64_t bloom_filter_code(typename Transaction::transaction_id_t tid) noexcept
    {
        //good spreading of sequential bits for average case when transaction_id grows monotony
        return tid * 0x5fe14bf901200001ull; //(0x60ff8010405001)
    }

} //end of namespace OP::vtm
#endif //_OP_VTM_TRANSACTIONAL__H_

