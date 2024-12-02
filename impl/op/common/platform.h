#ifndef _OP_COMMON_PLATFORM__H_
#define _OP_COMMON_PLATFORM__H_

#include <cstddef>

#if defined( _MSC_VER ) 
    #if defined(max)
    #undef max
    #endif

    #if defined(min)
    #undef min
    #endif
#endif 

#define OP_EMPTY_ARG

#ifdef _MSC_VER
#if _MSC_VER <= 1800
#define OP_CONSTEXPR(alt) alt 
#define OP_NOEXCEPT
#else
#define OP_CONSTEXPR(alt) constexpr
#define OP_NOEXCEPT noexcept 
#endif
#else
#define OP_CONSTEXPR(alt) constexpr
#define OP_NOEXCEPT noexcept 
#endif //

#if __cplusplus >= 202002L
    #define OP_CPP20_FEATURES
    #define OP_VIRTUAL_CONSTEXPR constexpr virtual
    #define OP_CONSTEXPR_CPP20 constexpr 
    #define OP_CPP20_CONCEPT(inl) inl
    #define OP_CPP20_CONCEPT_REF(ref) ref
    #define OP_CPP20_REQUIRES(inl) requires inl
#else
    #define OP_VIRTUAL_CONSTEXPR virtual
    #define OP_CONSTEXPR_CPP20
    #define OP_CPP20_CONCEPT(inl)
    #define OP_CPP20_CONCEPT_REF(ref) typename
    #define OP_CPP20_REQUIRES(inl)
#endif //c++20

#endif //_OP_COMMON_PLATFORM__H_
