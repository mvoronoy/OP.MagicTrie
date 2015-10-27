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
        struct serialized_t{};
        struct Compensation;
        /**Registry for types and them factories*/
        struct type_registry
        {
            typedef Compensation* (*factory_t)(serialized_t s);
            inline static type_registry& registry_instance()
            {
                static type_registry _r;
                return _r;
            }
            void declare(const std::string& key, factory_t f)
            {
                _map.insert(factory_map_t::value_type(key, f));
            }
            Compensation* create_instance(const std::string& key)
            {
                return  (*_map[key])(serialized_t());
            }
        private:
            typedef std::unordered_map<std::string, factory_t> factory_map_t;
            type_registry() = default;
            factory_map_t _map;
        };
        /**Marker that must be declared as static const in each serializable class
        * \code
        *   struct MySerial : public Compensation
        *   {
        *       ...
        *       static const serial_declare<MySerial> serial_info;
        *   }
        * \endcode
        */
        template <class T>
        struct serial_declare
        {
            serial_declare()
            {
                type_registry::registry_instance().declare(id, type_factory);
            }
            static T* type_factory()
            {
                return new T;
            }
            static const std::string id;
        };

        template <class T>
        const std::string serial_declare<T>::id = typeid(T).name();

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
        //typedef operation_guard_t< std::shared_ptr<Transaction>, 

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
        struct TransactionMedia
        {
            /**@throws std::exception when write fails*/
            virtual void write(const std::uint8_t *buffer, std::uint32_t size) = 0;
        };
        struct Transactable
        {
            virtual void on_commit(TransactionMedia& media) = 0;
            virtual void on_rollback(TransactionMedia& media) = 0;
        };



        struct Compensation
        {
            Compensation(){}
            Compensation(serialized_t){}
            virtual ~Compensation() = default;
            virtual void write(std::ostream& os) const = 0;
            virtual void read(std::istream& os) = 0;
            virtual void compensate(Transactable &instance) = 0;
        protected:
            template<class Scalar>
            static void _write(std::ostream& os, Scalar obj)
            {
                os.write(reinterpret_cast<const char*>(&obj), sizeof(Scalar));
            }

            static void _write(std::ostream& os, const std::string& str)
            {
                _write(os, str.length());
                os.write(reinterpret_cast<const char*>(str.c_str()),
                    str.length()*sizeof(std::string::traits_type::char_type));
            }
            template<class Scalar>
            static void _read(std::istream& is, Scalar& obj)
            {
                is.read(reinterpret_cast<char*>(&obj), sizeof(Scalar));
            }
            static void _read(std::istream& is, std::string& str)
            {
                std::string::size_type size;
                _read(is, size);
                str.resize(size);
                is.read(reinterpret_cast<char*>(&str[0]),
                    size*sizeof(std::string::traits_type::char_type));
            }

        };

        struct TrnsactionManager
        {
            friend struct Transaction;
            virtual void log_operation(Compensation &op)
            {
                //do nothing
            }
            virtual void log_operation()
            {
                //do nothing
            }

        private:

        };
    } //namespace vtm
} //end of namespace OP
#endif //_OP_TR_TRANSACTIONAL__H_

