#pragma once
#ifndef _OP_COMMON_FTRAITS__H_
#define _OP_COMMON_FTRAITS__H_

#include <tuple>

namespace OP
{
namespace utils{

        template<typename TResult, typename... TArgs> 
        struct function_traits_defs
        {
            /** Holder of all function types */
            using arguments_t = std::tuple<TArgs...>;
            /** Function result type */
            using result_t = TResult;
            /** Number of function arguments */
            static constexpr size_t arity_c = sizeof...(TArgs);

            /** 
                Allows resolve type of i'th function argument.
                For example: \code
                  int f(int, double, float);
                  ...
                  using d_t = function_traits<decltype(f)>::arg_i<1>;
                  static_assert(tsd::is_same<d_t, double>::value, "Must be double");
                  \endcode
            */
            template <size_t i>
            using arg_i = typename std::tuple_element<i, arguments_t>::type;
        };


        template <typename T>
        struct function_traits_impl;

        template <typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(Args...)>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(*)(Args...)>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...)>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) const>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::* const)(Args...) const>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) const&>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) const&&>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) volatile>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) volatile&>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) volatile&&>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) const volatile>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) const volatile&>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits_impl<ReturnType(ClassType::*)(Args...) const volatile&&>
            : function_traits_defs<ReturnType, Args...> {};

        template <typename T, typename V = void>
        struct function_traits
            : function_traits_impl<T> {};

        template <typename T>
        struct function_traits<T, decltype((void)&T::operator())>
            : function_traits_impl<decltype(&T::operator())> {};

}//ns:utils
} //ns:OP
#endif //_OP_COMMON_FTRAITS__H_
