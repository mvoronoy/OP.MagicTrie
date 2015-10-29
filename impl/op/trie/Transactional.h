#ifndef _OP_TR_TRANSACTIONAL__H_
#define _OP_TR_TRANSACTIONAL__H_

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#include <type_traits>
#include <iostream>
#include <unordered_map>
#include <string>
#include <typeinfo>

namespace OP
{
    namespace vtm{
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
            transaction_id_t transaction_id() const
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
                _is_closed(false){}
            void commit()
            {
                assert(!_is_closed);
                _instance->commit();
                _is_closed = true;
            }
            void rollback()
            {
                assert(!_is_closed);
                _instance->rollback();
                _is_closed = true;
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
        
    } //namespace vtm
} //end of namespace OP
#endif //_OP_TR_TRANSACTIONAL__H_

