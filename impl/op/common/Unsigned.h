#ifndef _OP_COMMON_UNSIGNED__H_
#define _OP_COMMON_UNSIGNED__H_

#include <type_traits>

namespace OP::utils
{

    /** Calculate signed difference between two unsigned 
    * \throw std::overflow_error if operation raises undefined behaviour of overflow
    * \tparam T any type satisfying std::is_arithmetic and std::is_unsigned
    */
    template <class T >
    constexpr typename std::enable_if_t<std::is_unsigned<T>::value, typename std::make_signed<T>::type> uint_diff_int(T first, T second)
    {
        using sint_t = typename std::make_signed<T>::type;
        using uint_t = typename std::make_unsigned<T>::type;
        sint_t diff = static_cast<sint_t>(first - second);
        bool overflowed = (diff < 0) ^ (first < second);
        if(overflowed)
            throw std::overflow_error(std::to_string(first) + " - " + std::to_string(second) + " raised overflow");
        return diff;
    }

    template <class T>
    constexpr typename std::enable_if_t<std::is_signed<T>::value, T> uint_diff_int(T first, T second)
    {
        T diff = first - second;
        bool overflowed = (diff < 0) ^ (first < second);
        if(overflowed)
            throw std::overflow_error(std::to_string(first) + " - " + std::to_string(second) + " raised overflow");
        return diff;
    }    

    /** Produce signed value (-1, 0, 1) as result of 3 way comparison between unsigned integer types */
    template <class T>
    constexpr typename std::enable_if_t<std::is_unsigned_v<T>, std::make_signed_t<T>> uint_3way_cmp(T first, T second) noexcept
    {
        return (first < second)
            ? -1 
            : (first == second ? 0 : -1);
    }

    namespace details
    {
        template <typename T> 
        inline constexpr int signum(T x, std::false_type) noexcept
        {
            return T{0} < x;
        }

        template <typename T> 
        inline constexpr int signum(T x, std::true_type is_signed) noexcept
        {
            return (T{0} < x) - (x < T{0});
        }
    }
    /** 
    * @return -1 or 1 depending on sign of `x`. Special thanks to https://stackoverflow.com/a/4609795/149818.
    */
    template <typename T> 
    inline constexpr int signum(T x) noexcept
    {
        return details::signum(x, std::is_signed<T>());
    }

} //OP::utils
#endif //_OP_COMMON_UNSIGNED__H_
