#ifndef _OP_TRIE_TYPEDEFS__H_
#define _OP_TRIE_TYPEDEFS__H_

#include <string>


namespace OP::common
{
    using atom_t = std::uint8_t;
    using atom_string_t = std::basic_string<atom_t>;
    using atom_string_view_t = std::basic_string_view<atom_t>;

    inline constexpr atom_t operator "" _atom(char c)
    {
        return (atom_t)c;
    }
    inline const atom_t* operator "" _atom(const char* str, size_t n)
    {
        return (const atom_t*)(str);
    }

    inline atom_string_t operator "" _astr(const char* str, size_t n)
    {
        return atom_string_t{reinterpret_cast<const atom_t*>(str), n};
    }

}//ns:OP

#endif //_OP_TRIE_TYPEDEFS__H_
