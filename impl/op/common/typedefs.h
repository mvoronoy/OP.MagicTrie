#ifndef _OP_TRIE_TYPEDEFS__H_
#define _OP_TRIE_TYPEDEFS__H_
#include <cstdint>
#include <cassert>
#include <limits>
#include <cstddef>
#include <string>
#include <iomanip>

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

#define OP_TEMPL_METH(method) template method

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


namespace OP
{
    namespace trie
    {
        using atom_t = std::uint8_t;
        using nullable_atom_t = std::pair<bool, atom_t>;
        using atom_string_t = std::basic_string<atom_t>;
        using atom_string_view_t = std::basic_string_view<atom_t>;

        inline bool operator! (const nullable_atom_t& test)
        {
            return !test.first;
        }
        
        inline constexpr atom_t operator "" _atom(char c)
        {
            return (atom_t)c;
        }
        inline const atom_t* operator "" _atom(const char* str, size_t n)
        {
            return (const atom_t*)(str);
        }

        inline OP::trie::atom_string_t operator "" _astr(const char* str, size_t n)
        {
            return OP::trie::atom_string_t{reinterpret_cast<const atom_t*>(str), n};
        }

    }//ns:trie
}//ns:OP

#endif //_OP_TRIE_TYPEDEFS__H_
