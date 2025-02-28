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

    
} //OP::utils
#endif //_OP_COMMON_UNSIGNED__H_
