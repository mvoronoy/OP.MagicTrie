#pragma once
#ifndef _OP_COMMON_ATOMIC_UTILS__H_
#define _OP_COMMON_ATOMIC_UTILS__H_

#include <atomic>
#include <utility>
#include <concepts>


namespace OP::utils
{
    template <class T, std::predicate<const T&, const T&> TPredicate>
    [[maybe_unused]] T cas_extremum(std::atomic<T>& target, T new_value, TPredicate cmp) noexcept
    {
        T old_value = target.load(std::memory_order_acquire);
        while (cmp(new_value, old_value))
        {
            if (target.compare_exchange_weak(old_value, new_value,
                std::memory_order_acq_rel, std::memory_order_acquire))
            {
                break;
            }
        }
        return old_value;
    }

    /** \brief Lock-free atomic extremum update (min/max) using CAS.
     *
     * Provides a compare-and-swap (CAS) based implementation of atomic
     * minimum or maximum assignment.
     *
     * C++26 introduces native `std::atomic::fetch_max` and `fetch_min`.
     * This function provides equivalent functionality for earlier
     * language standards.
     *
     * The function updates the atomic value only if the supplied
     * predicate determines that `new_value` is a stronger extremum
     * than the current value.
     *
     * \par Predicate Semantics
     *
     * The template parameter `Op` defines the comparison logic:
     *
     * - compute minimum: \code
     *      std::atomic<int> x = 1;
     *      OP::utils::cas_extremum<std::less>(x, 0);
     *      assert(x == 0);
     *      \endcode
     * - compute maximum: \code 
     *      std::atomic<int> y = 1;
     *      OP::utils::cas_extremum<std::greater>(y, 2);
     *      assert(y == 2);
     *      \endcode
     *
     * The predicate is evaluated as:
     *
     *     Op{}(new_value, current_value)
     *
     * If the predicate returns `true`, the atomic value is replaced
     * with `new_value` using a CAS loop.
     *
     *
     * \tparam Op Binary comparison predicate template
     *            (e.g., `std::less`, `std::greater`).
     * \tparam T  Type supported by `std::atomic<T>` and the predicate.
     *
     * \param target    Atomic value to update.
     * \param new_value Candidate value for extremum update.
     *
     * \return The value held by `target` prior to modification
     *         (matching semantics of fetch-style atomic operations).
     */
    template <template<typename ...> typename Op, class T>
    [[maybe_unused]] T cas_extremum(std::atomic<T>& target, T new_value) noexcept
    {
        constexpr Op<T> cmp;
        return cas_extremum(target, std::move(new_value), cmp);
    }

    struct WaitableSemantic
    {
        struct one
        {
            template <class T>
            static inline void notify(std::atomic<T>& target){ target.notify_one(); }
        };
        struct all
        {
            template <class T>
            static inline void notify(std::atomic<T>& target){ target.notify_all(); }
        };
    };
    /** \brief Makes usage of C++20 `std::atomic::wait` safer and simpler.
    *   RAII pattern allows never forget to call `std::atomic::notify_one` or `std::atomic::notify_all` while
    *   there is a waiter.
    */
    template<class T, class NotifySemantic = WaitableSemantic::all>
    struct Waitable
    {
        Waitable() noexcept = default;
        
        /**
        *
        *   \param on_load - If is not std::memory_order_relaxed, std::memory_order_consume, 
        *       std::memory_order_acquire or std::memory_order_seq_cst, the behavior is undefined.
        */
        Waitable(
            T desired, 
            std::memory_order on_assign = std::memory_order_seq_cst, 
            std::memory_order on_load = std::memory_order_seq_cst ) noexcept
            : _target(std::move(desired))
            , _on_assign(on_assign)
            , _on_load(on_load)
        {
        }
        
        ~Waitable() noexcept
        {
            if(_under_wait.load(std::memory_order_acquire))
            {//give last chance to exit wait 
                NotifySemantic::notify(_target);
            }
        }

        Waitable& operator = (T new_value) noexcept
        {
            this->store(new_value);
            return *this;
        }

        operator T() const noexcept
        {
            return load();
        }

        T load() const noexcept
        {
            return _target.load(_on_load);
        }

        void store(T new_value) noexcept
        {
            _target.store(new_value, _on_assign);
            NotifySemantic::notify(_target);
        }

        T exchange(T desired) noexcept
        {
            T result = _target.exchange(desired, _on_assign);
            NotifySemantic::notify(_target);
            return result;
        }

        /** Get orignal std::atomic for more complex operations */
        std::atomic<T>& raw()
        {
            return _target;
        }

        void wait(T old) noexcept
        {
            _under_wait.store(true, std::memory_order_relaxed);
            _target.wait(old, _on_load);
            _under_wait.store(false, std::memory_order_release);
        }

        /** \brief Wait until atomic value meets condition specified by predicate.
        *
        *   The implementation is similar to C++20 std::atomic::wait, but in compare with standart, it 
        *   allows control condition with help of binary predicate `Op`.
        *   Caller is responsible to wake up waiting by calling one of `std::atomic::notify_one` or `std::atomic::notify_all`.
        */
        template <std::predicate<const T&, const T&> Op>
        [[maybe_unused]] T wait_condition(T expected, Op cmp) noexcept
        {
            T result = _target.load(_on_load);
            for(;cmp(result, expected); result = _target.load(_on_load))
            {
                this->wait(result);
            }
            return result;
        }

        template <template<typename ...> typename Op>
        [[maybe_unused]] T wait_condition(T expected) noexcept
        {
            constexpr Op cmp;
            return wait_condition(std::move(expected), cmp);
        }

    private:
        std::atomic<T> _target;
        std::atomic<bool> _under_wait = false;
        std::memory_order _on_assign, _on_load;
     };

}//ns:OP::utils

#endif //_OP_COMMON_ATOMIC_UTILS__H_
