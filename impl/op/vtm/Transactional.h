#pragma once
#ifndef _OP_VTM_TRANSACTIONAL__H_
#define _OP_VTM_TRANSACTIONAL__H_

#include <type_traits>
#include <iostream>
#include <unordered_map>
#include <string>
#include <typeinfo>

namespace OP::vtm
{
        /**Exception is raised when imposible to obtain lock over memory block*/
        struct ConcurentLockException : public OP::trie::Exception
        {
            ConcurentLockException() :
                Exception(OP::trie::er_transaction_concurent_lock){}
            ConcurentLockException(const char* debug) :
                Exception(OP::trie::er_transaction_concurent_lock, debug){}
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
            typedef std::uint64_t transaction_id_t;
            friend struct TransactionGuard;

            Transaction() = delete;
            Transaction(const Transaction&) = delete;
            Transaction& operator = (const Transaction&) = delete;
            Transaction& operator = (Transaction&& other) = delete;

            Transaction(Transaction && other) noexcept :
                _transaction_id(other._transaction_id)
            {
            }

            virtual ~Transaction() noexcept
            {

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
        private:
            const transaction_id_t _transaction_id;
        };
        
        using transaction_ptr_t = std::shared_ptr<Transaction>;

        /** No-op transaction implementation */
        struct NoOpTransaction : public Transaction
        {

            NoOpTransaction(transaction_id_t id)
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
        *   Destructor is responsible to rollback transaction if user did not commit it explictly before.
        */
        struct TransactionGuard
        {
            template <class Sh>
            TransactionGuard(Sh && instance, bool do_commit_on_close = false) 
                : _instance(std::forward<Sh>(instance))
                , _is_closed(!_instance)//be polite to null transactions
                , _do_commit_on_close(do_commit_on_close)
            {}
            
            TransactionGuard(const TransactionGuard&) = delete;
            TransactionGuard& operator = (const TransactionGuard&) = delete;

            void commit()
            {
                if (!!_instance)
                {
                    assert(!_is_closed);//be polite to null transactions
                    _instance->commit();
                    _is_closed = true;
                }
            }
            void rollback()
            {
                if (!!_instance)
                {
                    assert(!_is_closed);//be polite to null transactions
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
        *   Utility to repeat some operation after ConcurentLockException has been raised.
        *   Number of repeating peformed by N template parameter, after this number 
        *   exceeding ConcurentLockException exception just propagated to caller
        */
        template <auto N, typename  F, typename  ... Args>
        inline typename std::result_of<F(Args ...)>::type transactional_retry_n(F f, Args ... ax)
        {
            for (auto i = 0; i < N; ++i)
            {
                try
                {
                    return f(ax...);//as soon this is a loop move/forward args must not be used
                }
                catch (const OP::vtm::ConcurentLockException &e)
                {
                    /*ignore exception for a while*/
                    e.what();
                }
            }
            throw OP::vtm::ConcurentLockException("10");
        }
        template <size_t N, typename F, typename  ... Args>
        inline typename std::result_of<F(Args ...)>::type transactional_yield_retry_n(F f, Args ... ax)
        {
            constexpr size_t limit = N - 1;
            for (auto i = 0; i < limit; ++i)
            {
                try
                {
                    return f(ax...);//as soon this is a loop move/forward args must not be used
                }
                catch (const OP::vtm::ConcurentLockException &)
                {
                    /*ignore exception for a while*/
                    if ((i+1) < limit)
                    {
                        std::this_thread::yield();
                        continue;
                    }
                    throw;
                }
            }
            return f(ax...);
        }

} //end of namespace OP::vtm
#endif //_OP_VTM_TRANSACTIONAL__H_

