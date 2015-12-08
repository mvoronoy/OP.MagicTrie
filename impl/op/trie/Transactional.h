#ifndef _OP_TR_TRANSACTIONAL__H_
#define _OP_TR_TRANSACTIONAL__H_

#include <type_traits>
#include <iostream>
#include <unordered_map>
#include <string>
#include <typeinfo>

namespace OP
{
    namespace vtm{
        /**Exception is raised when imposible to obtain lock over memory block*/
        struct ConcurentLockException : public OP::trie::Exception
        {
            ConcurentLockException() :
                Exception(OP::trie::er_transaction_concurent_lock){}
            ConcurentLockException(const char* debug) :
                Exception(OP::trie::er_transaction_concurent_lock, debug){}
        };
        /**
        *   Declare general definition of transaction as identifiable object with pair of operations commit/rollback
        */
        struct Transaction
        {
            typedef std::uint64_t transaction_id_t;

            Transaction() = delete;
            Transaction(const Transaction&) = delete;
            Transaction& operator = (const Transaction&) = delete;
            Transaction& operator = (Transaction&& other) = delete;

            Transaction(Transaction && other) OP_NOEXCEPT :
                _transaction_id(other._transaction_id)
            {

            }
            virtual ~Transaction() OP_NOEXCEPT
            {

            }
            virtual transaction_id_t transaction_id() const
            {
                return _transaction_id;
            }
            
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
        
        /**
        *   Guard wrapper that grants transaction accomplishment.
        *   Destructor is responsible to rollback transaction if user did not commit it explictly before.
        */
        struct TransactionGuard
        {
            template <class Sh>
            TransactionGuard(Sh && instance) :
                _instance(std::forward<Sh>(instance)),
                _is_closed(!_instance)//be polite to null transactions
            {}
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
                    _instance->rollback();
            }
        private:
            std::shared_ptr<Transaction> _instance;
            bool _is_closed;
        };

        /**
        *   Utility to repeat some operation after ConcurentLockException has been raised.
        *   Number of repeating peformed by N template parameter, after this number 
        *   exceeding ConcurentLockException exception just propagated to caller
        */
        template <std::uint16_t N, typename  F, typename  ... Args>
        inline typename std::result_of<F && (Args &&...)>::type transactional_retry_n(F && f, Args&& ... ax)
        {
            for (auto i = 0; i < N; ++i)
            {
                try
                {
                    return std::forward<F>(f)(std::forward<Args>(ax)...);
                }
                catch (const OP::vtm::ConcurentLockException &e)
                {
                    /*ignore exception for a while*/
                    e.what();
                }
            }
            throw OP::vtm::ConcurentLockException("10");
        }
        template <std::uint16_t N, typename  F, typename  ... Args>
        inline typename std::result_of<F && (Args &&...)>::type transactional_yield_retry_n(F && f, Args&& ... ax)
        {
            for (auto i = 0; i < N; ++i)
            {
                try
                {
                    return std::forward<F>(f)(std::forward<Args>(ax)...);
                }
                catch (const OP::vtm::ConcurentLockException &)
                {
                    /*ignore exception for a while*/
                    std::this_thread::yield();
                }
            }
            throw OP::vtm::ConcurentLockException("10");
        }

    } //namespace vtm
} //end of namespace OP
#endif //_OP_TR_TRANSACTIONAL__H_

