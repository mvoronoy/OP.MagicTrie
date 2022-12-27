#pragma once
#ifndef _OP_TRIE_TRIEPOSITION__H_
#define _OP_TRIE_TRIEPOSITION__H_

#include <op/common/typedefs.h>
#include <cstdint>
#include <op/common/typedefs.h>

namespace OP::trie
{    
        
    /** Decodes if TriePosition points to value, children or combination of this or nothing of this
    */
    enum class Terminality : std::uint8_t
    {
        /** No any information there */
        term_no = 0,
        /** TriePosition contains reference to child position */
        term_has_child = 0x1,
        /** TriePosition contains reference to position with a value */
        term_has_data = 0x2
    };
    
    /** Quick check for Terminality::term_no */
    inline bool operator !(Terminality check)
    {
        return check == Terminality::term_no;
    }

    inline Terminality operator & (Terminality left, Terminality right)
    {
        return static_cast<Terminality>(  static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right) );
    }
    inline Terminality operator | (Terminality left, Terminality right)
    {
        return static_cast<Terminality>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    inline Terminality& operator &= (Terminality& left, Terminality right)
    {
        left = left & right;
        return left;
    }
    inline Terminality& operator |= (Terminality& left, Terminality right)
    {
        left = left | right;
        return left;
    }
    inline Terminality operator ~ (Terminality v)
    {
        return static_cast<Terminality>(~static_cast<std::uint8_t>(v));
    }

    template <class T>
    constexpr inline bool all_set(T value, Terminality test)
    {
        return (value & test) == test;
    }

    template <class T>
    inline bool is_not_set(T value, Terminality test)
    {
        return (value & static_cast<T>(test)) != test;
    }

    /**
    * Describe result of stem compare
    *    |   src   |     stem    |
    *  1    ""          ""         duplicate
    *  2    ""           x         split stem on length of src, so terminal is for src and for x
    *  3     x          ""         add x to page pointed by stem
    *  4     x           y         create child with 2 entries: x, y (check if followed of y can be optimized)
    */
    enum class StemCompareResult
    {
        /**Source fully string matches to the existing stem*/
        equals,
        /**String fully fit to stem, but stem is longer*/
        string_end,
        /**Stem part is fully equal to string, but string is longer*/
        stem_end,
        /**Stem and string not fully matches*/
        unequals,
        no_entry
    };

    using node_version_t = std::uint32_t;

    template <class T>
    struct TriePositionArg
    {
        T _t;
    };

    struct TriePosition
    {
        constexpr TriePosition() noexcept = default;

        template <class ... Tx>
        TriePosition(TriePositionArg<Tx>&& ... tx) noexcept
        {
            (tx(*this), ... );
        }

        /** As soon this class consist of unique types only can assign values in arbitrary order */
        template <class ... Tx>
        void assign(TriePositionArg<Tx>&& ... tx) noexcept
        {
           (tx(*this), ... );
        }        

        constexpr inline bool operator == (const TriePosition& other) const noexcept
        {
            return _node_addr == other._node_addr //first compare node address as simplest comparison
                && _key == other._key //check in-node position then
                && _stem_size == other._stem_size
                ;
        }

        constexpr node_version_t version() const noexcept
        {
            return _version;
        }
        
        /**Offset inside node. May be nil_c - if this position points to the `end` */
        constexpr dim_t key() const noexcept
        {
            return _key;
        }
        
        constexpr FarAddress address() const noexcept
        {
            return _node_addr;
        }

        constexpr dim_t stem_size() const noexcept
        {
            return _stem_size;
        }
        /**
        * return combination of flag presence at current point
        * @see Terminality enum
        */
        constexpr Terminality terminality() const noexcept
        {
            return _terminality;
        }

        FarAddress _node_addr = {};
        /**horizontal position in node*/
        dim_t _key = dim_nil_c;
        /** Vertical position in node, for nodes without stem it is dim_nil_c*/
        dim_t _stem_size = dim_nil_c;
        Terminality _terminality = Terminality::term_no;
        node_version_t _version = ~node_version_t{};
        
    };

    template <>
    struct TriePositionArg<atom_t>
    {
        atom_t _t;
        void operator()(TriePosition& target){ target._key = _t; }
    };

    template <>
    struct TriePositionArg<FarAddress>
    {
        FarAddress _t;
        void operator()(TriePosition& target){ target._node_addr = _t; }
    };

    template <>
    struct TriePositionArg<Terminality>
    {
        Terminality _t;
        enum class Op
        {
            disjunct, conjunct, assign
        } _operation;
        void operator()(TriePosition& target) 
        {
            switch (_operation)
            {
            case Op::disjunct:
                target._terminality |= _t;
                break;
            case Op::conjunct:
                target._terminality &= _t;
                break;
            case Op::assign:
                target._terminality = _t;
                break;
            default:
                assert(false);
            }
        }
    };

    template <>
    struct TriePositionArg<dim_t>
    {
        dim_t _t;
        void operator()(TriePosition& target)
        {
            //be aware! something already changed stem size 
            assert( target._stem_size == dim_nil_c );
            target._stem_size = _t; 
        }
    };

    template <>
    struct TriePositionArg<node_version_t>
    {
        node_version_t _t;
        void operator()(TriePosition& target) { target._version = _t; }
    };
    

    inline TriePositionArg<atom_t> key(atom_t c)
    {
        return TriePositionArg<atom_t>{c};    
    }

    inline auto address(FarAddress addr)
    {
        return TriePositionArg<FarAddress>{std::move(addr)};
    }

    inline auto terminality(Terminality c)
    {
        using arg_t = TriePositionArg<Terminality>;
        return arg_t{c, arg_t::Op::assign};
    }

    inline auto terminality_and(Terminality c)
    {
        using arg_t = TriePositionArg<Terminality>;
        return arg_t{c, arg_t::Op::conjunct};
    }

    inline auto terminality_or(Terminality c)
    {
        using arg_t = TriePositionArg<Terminality>;
        return arg_t{c, arg_t::Op::disjunct};
    }

    inline auto stem_size(dim_t c)
    {
        return TriePositionArg<dim_t>{c};
    }

    inline auto node_version(node_version_t c)
    {
        return TriePositionArg<node_version_t>{c};
    }
} //ns:OP::trie
#endif //_OP_TRIE_TRIEPOSITION__H_

