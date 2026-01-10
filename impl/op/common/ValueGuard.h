#pragma once
#ifndef _OP_COMMON_VALUEGUARD__H_
#define _OP_COMMON_VALUEGUARD__H_

namespace OP::raii
{
    /** RAII pattern to assign expected value at scope exit.
    Usage: \code
        int critical_value = ...;
        {//RAII scope
           OP::raii::ValueGuard guard( critical_value, 42 ); //need 42 at exit from scope
           for(; critical_value < 100; ++critical_value) // represent changes in value
            ;
        } //end scope
        assert(critical_value == 42); //expected 42 is restored
    \endcode
    */
    template <class T, class TFinal = T>
    struct ValueGuard
    {
        ValueGuard(const ValueGuard&) = delete; // no copy state is allowed
        ValueGuard(ValueGuard&&) = default; // move is allowed and ok
        ValueGuard() = delete; //defaulting is disabled

        template <class U>
        ValueGuard(T& control_unit, U&& restore_to)
            : _control_unit(control_unit)
            , _restore_to(std::forward<U>(restore_to))
        {
        }

        explicit ValueGuard(T& control_unit) noexcept
            : _control_unit(control_unit)
            , _restore_to(control_unit)
        {
        }

        ~ValueGuard()
        {
            _control_unit = _restore_to;
        }

    private:
        T& _control_unit;
        TFinal _restore_to;
    };

    // explicit deduction guide (not needed as of C++20)
    template<class T> ValueGuard(T&) -> ValueGuard<T>;


} //end of namespace OP::raii

#endif //_OP_COMMON_VALUEGUARD__H_
