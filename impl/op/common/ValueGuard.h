#pragma once
#ifndef _OP_COMMON_VALUEGUARD__H_
#define _OP_COMMON_VALUEGUARD__H_

namespace OP::raii
{
    /** RAII pattern to assign expected value at scope exit.
    Usage: \code
        int critical_value = ...;
        {//RAII scope
           OP::raii::RestoreValueGuard guard( critical_value, 42 ); //need 42 at exit from scope
           for(; critical_value < 100; ++critical_value) // represent changes in value
            ;
        } //end scope
        assert(critical_value == 42); //expected 42 is restored
    \endcode
    */
    template <class T, class TFinal = T>
    struct RestoreValueGuard
    {
        RestoreValueGuard(const RestoreValueGuard&) = delete; // no copy state is allowed
        constexpr RestoreValueGuard(RestoreValueGuard&&) = default; // move is allowed and ok
        RestoreValueGuard() = delete; //defaulting is disabled

        template <class U>
        constexpr RestoreValueGuard(T& control_unit, U&& restore_to) noexcept
            : _control_unit(control_unit)
            , _restore_to(std::forward<U>(restore_to))
        {
        }

        constexpr explicit RestoreValueGuard(T& control_unit) noexcept
            : _control_unit(control_unit)
            , _restore_to(control_unit)
        {
        }

        ~RestoreValueGuard() noexcept
        {
            _control_unit = _restore_to;
        }

    private:
        T& _control_unit;
        TFinal _restore_to;
    };

    // explicit deduction guide (not needed as of C++20)
    template<class T> RestoreValueGuard(T&) -> RestoreValueGuard<T>;

    /** Allowed template argument constant for RefCountGuard to increase value either on enter or leave */
    constexpr static inline auto op_increase = [](auto& v){++v;};
    /** Allowed template argument constant for RefCountGuard to decrease value either on enter or leave */
    constexpr static inline auto op_decrease = [](auto& v){--v;};
    /** Allowed template argument constant for RefCountGuard to do nothing with value either on enter or leave */
    constexpr static inline auto no_op = [](auto& ){};

    /**
    * RAII pattern to increase / decrease value at scope exit.
    * Usage: \code
    *     int critical_value = 42;
    *     {//RAII scope
    *        OP::raii::DecreaseValueGuard guard( ++critical_value ); 
    *        //need 42 at exit from scope
    *     } //end scope
    *     assert(critical_value == 42); //expected 42 is restored
    * \endcode
    *
    *  \tparam on_enter constant operator to specify what to to on enter to raii use: custom or one of:
    *       op_increase, op_decrease, no_op. Default is increase (op_increase).
    *  \tparam on_leave constant operator to specify what to to on leave of raii use: custom or one of:
    *       op_increase, op_decrease, no_op. Default is decrease (op_decrease).
    */
    template <class T, auto on_enter = op_increase, auto on_leave = op_decrease>
    struct RefCountGuard
    {
        explicit RefCountGuard(T& ref) noexcept
            : _control_unit(ref)
        {
            on_enter(_control_unit);
        }
        
        ~RefCountGuard() noexcept
        {
            on_leave(_control_unit);
        }

    private:
        T& _control_unit;
    };

} //end of namespace OP::raii

#endif //_OP_COMMON_VALUEGUARD__H_
