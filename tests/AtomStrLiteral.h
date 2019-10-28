#pragma once
#include <op/common/typedefs.h>

constexpr const OP::trie::atom_t* operator "" _atom(const char* str, size_t n)
{
    return reinterpret_cast<const OP::trie::atom_t*>(str);
}

inline OP::trie::atom_string_t operator "" _astr(const char* str, size_t n)
{
    return OP::trie::atom_string_t{reinterpret_cast<const OP::trie::atom_t*>(str), n};
}