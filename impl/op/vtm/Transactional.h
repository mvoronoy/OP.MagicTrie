#pragma once

#ifndef _OP_VTM_TRANSACTIONAL__H_
#define _OP_VTM_TRANSACTIONAL__H_

#include <type_traits>
#include <iostream>
#include <unordered_map>
#include <string>
#include <typeinfo>
#include <cassert>
#include <thread>

#include <op/common/EventSupplier.h>
#include <op/common/Assoc.h>

#include <op/vtm/vtm_error.h>

namespace OP::vtm
{
    struct Transaction;

    /** Events of transaction lifecycle */
    struct TransactionEvent
    {
        using transaction_id_t = std::uint64_t;
        enum
        {
            /** transaction has been started, argument: Transaction& */
            started = 1,
            /** transaction going to commit, argument: Transaction& */
            before_commit,
            /** transaction has been committed, argument: Transaction& */
            committed,
            /** transaction going to rollback, argument: Transaction& */
            before_rollback,
            /** transaction has been rolled back, argument: Transaction& */
            rolledback,
        };
        
        using event_supplier_t = OP::events::EventSupplier<
            Assoc<started, transaction_id_t>,
            Assoc<before_commit, transaction_id_t>,
            Assoc<committed, transaction_id_t>,
            Assoc<before_rollback, transaction_id_t>,
            Assoc<rolledback, transaction_id_t>
        >;
    };

    /**
    *   Declare general definition of transaction as identifiable object with pair of operations commit/rollback
    */
    struct Transaction
    {
        using transaction_id_t = typename TransactionEvent::transaction_id_t;

        explicit Transaction(transaction_id_t id) noexcept:
            _transaction_id(id)
        {
        }

        virtual ~Transaction() noexcept = default;


        Transaction() = delete;
        Transaction(const Transaction&) = delete;
        Transaction& operator = (const Transaction&) = delete;
        Transaction& operator = (Transaction&& other) = delete;

        constexpr transaction_id_t transaction_id() const noexcept
        {
            return _transaction_id;
        }

        virtual std::shared_ptr<Transaction> recurrent() = 0;

        virtual void rollback() = 0;
        virtual void commit() = 0;
        virtual std::shared_ptr<Transaction> merge_thread() = 0;
        virtual void unmerge_thread() = 0;

    private:
        const transaction_id_t _transaction_id;
    };

    /**Exception is raised when impossible to obtain lock over memory block*/
    struct ConcurrentLockException
    {
        using transaction_id_t = typename Transaction::transaction_id_t;
        
        ConcurrentLockException() = default;
        ConcurrentLockException(
            FarAddress requested,
            transaction_id_t requesting_transaction,
            FarAddress locked_range,
            transaction_id_t locking_transaction
        )
            : _requested(requested)
            , _requesting_transaction(requesting_transaction)
            , _locked_range(locked_range)
            , _locking_transaction(locking_transaction)
        {
        }

        FarAddress _requested;
        transaction_id_t _requesting_transaction;
        vtm::FarAddress _locked_range;
        transaction_id_t _locking_transaction;

        Exception to_exception()
        {
            return Exception(vtm::ErrorCodes::er_transaction_concurrent_lock);
        }
    };



    using transaction_ptr_t = std::shared_ptr<Transaction>;

    /** No-op transaction implementation */
    struct NoOpTransaction : public Transaction, std::enable_shared_from_this<Transaction>
    {

        explicit NoOpTransaction(transaction_id_t id) noexcept
            : Transaction(id)
        {
        }

        virtual std::shared_ptr<Transaction> recurrent() override
        {
            return shared_from_this();
        }

        virtual void rollback() override
        {
            //invoke events on transaction end
        }

        virtual void commit() override
        {
            //invoke events on transaction end
        }

        virtual std::shared_ptr<Transaction>  merge_thread() override
        {
            return shared_from_this();
        }

        virtual void unmerge_thread() override
        {
            //do nothing
        }
    };

    /** \brief RAII guard that grants pair methods Transaction::merge_thread/Transaction::unmerge_thread be called.
    */
    struct ThreadMergeGuard final
    {
        template <class T>
        explicit ThreadMergeGuard(T&& ro_transaction_ptr)
            : _ro_instance(std::forward<T>(ro_transaction_ptr))
            , _is_merged(true)
        {
        }

        ThreadMergeGuard(ThreadMergeGuard&& other) noexcept
            : _ro_instance(std::move(other._ro_instance))
            , _is_merged(other._is_merged)
        {
            other._is_merged = false;
        }

        ThreadMergeGuard& operator = (ThreadMergeGuard&& other)
        {
            unmerge_thread();
            _ro_instance = std::move(other._ro_instance);
            _is_merged = other._is_merged;
            other._is_merged = false;

            return *this;
        }

        ThreadMergeGuard(const ThreadMergeGuard&) = delete;
        ThreadMergeGuard& operator = (const ThreadMergeGuard&) = delete;

        ~ThreadMergeGuard()
        {
            unmerge_thread();
        }

        void unmerge_thread()
        {
            if(_is_merged)
            {
                _ro_instance->unmerge_thread();
                _is_merged = false;
            }
        }

    private:
        transaction_ptr_t _ro_instance;
        bool _is_merged;
    };


    /**
    *   Guard wrapper that grants transaction accomplishment.
    *   Depending on policy `commit_on_close` this RAII pattern implementation is responsible to
    *   automatically rollback or commit transaction at exit.
    */
    struct TransactionGuard final
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

        ~TransactionGuard()
        {
            if (!_is_closed)
                _do_commit_on_close ? _instance->commit() : _instance->rollback();
        }

        void commit()
        {
            if (!!_instance)//tolerate null transactions
            {
                assert(!_is_closed);
                _instance->commit();
                _is_closed = true;
            }
        }

        void rollback()
        {
            if (!!_instance)//tolerate null transactions
            {
                assert(!_is_closed);
                _instance->rollback();
                _is_closed = true;
            }
        }

        [[nodiscard]] ThreadMergeGuard merge_thread()
        {
            return ThreadMergeGuard(_instance->merge_thread());
        }

        transaction_ptr_t transaction()
        {
            return _instance;
        }
    private:
        transaction_ptr_t _instance;
        bool _is_closed;
        bool _do_commit_on_close;
    };

    /**
    *   Utility to repeat some operation after ConcurrentLockException has been raised.
    *   Number of repeating performed by N template parameter, after this number
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

