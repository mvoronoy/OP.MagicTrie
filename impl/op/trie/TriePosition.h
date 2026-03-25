#pragma once
#ifndef _OP_TRIE_TRIEPOSITION__H_
#define _OP_TRIE_TRIEPOSITION__H_

#include <cstdint>

#include <op/common/Assoc.h>

#include <op/vtm/typedefs.h>

namespace OP::trie
{    
    /** Flags to check if position in the Trie contains value, children or nothing.
    */
    struct Terminality final 
    {
        /** No terminality (no child, no value) */
        constexpr static inline std::uint_fast8_t term_no = 0;
        /** TriePosition contains reference to child position */
        constexpr static inline std::uint_fast8_t term_has_child = 0x1;
        /** TriePosition contains reference to position with a value */
        constexpr static inline std::uint_fast8_t term_has_data = 0x2;
    };
    
    template <class T>
    constexpr inline bool all_set(T value, std::uint_fast8_t test) noexcept
    {
        return (value & test) == test;
    }

    template <class T>
    constexpr inline bool is_not_set(T value, std::uint_fast8_t test) noexcept
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
        stem_x,
        no_entry
    };

    using node_version_t = std::uint32_t;

    // Forward declare the class so the concept can refer to it
    struct TriePosition;

    /** Define the requirement: T must be callable with (TriePosition&) */
    template<typename T>
    concept CallableWithTriePosition = requires(T t, TriePosition& ref) {
        { t(ref) } -> std::same_as<void>;
    };



    struct TriePosition
    {
        using fast_dim_t = vtm::fast_dim_t;

        using FarAddress = vtm::FarAddress;

        constexpr TriePosition() noexcept = default;

        template <CallableWithTriePosition... Tx>
        explicit TriePosition(Tx&& ... tx) noexcept
        {
            (tx(*this), ... );
        }

        /** As soon this class consist of unique types only can assign values in arbitrary order */
        template <CallableWithTriePosition... Tx>
        void assign(Tx&& ... tx) noexcept
        {
           (tx(*this), ... );
        }        

        constexpr node_version_t version() const noexcept
        {
            return _version;
        }
        
        constexpr FarAddress address() const noexcept
        {
            return _node_addr;
        }

        constexpr fast_dim_t chunk_size() const noexcept
        {
            return _chunk_size;
        }
        /**
        * return combination of flag presence at current point
        * @see Terminality enum
        */
        constexpr uint_fast8_t terminality() const noexcept
        {
            return _terminality;
        }

        uint_fast8_t _terminality = Terminality::term_no;
        /** Length of prefix string stored in current node. Value 1 always stands for key entry and values bigger than 1 encodes also size of stem*/
        fast_dim_t _chunk_size = 0;
        node_version_t _version = ~node_version_t{};
        FarAddress _node_addr = {};
    };

    namespace _trie_position_args
    {
        template <auto member_c, class T>
        struct FieldAssign: OP::AssocVal<member_c, T>
        {
            using base_t = OP::AssocVal<member_c, T>;

            using base_t::base_t;

            constexpr void operator()(TriePosition& subject) const & noexcept
            {
                auto lift = base_t::code;
                (subject.*lift) = base_t::value;
            }

            constexpr void operator()(TriePosition& subject) && noexcept
            {
                auto lift = base_t::code;
                (subject.*lift) = std::move(base_t::value);
            }
        };

        template <auto member_c, class T>
        struct FieldDelta: OP::AssocVal<member_c, T>
        {
            using base_t = OP::AssocVal<member_c, T>;

            using base_t::base_t;

            constexpr void operator()(TriePosition& subject) const & noexcept
            {
                (subject.*base_t::code) += base_t::value;
            }

            constexpr void operator()(TriePosition& subject) && noexcept
            {
                (subject.*base_t::code) += std::move(base_t::value);
            }
        };

        template <auto member_c, class T>
        struct FieldOrAssign: OP::AssocVal<member_c, T>
        {
            using base_t = OP::AssocVal<member_c, T>;

            using base_t::base_t;

            constexpr void operator()(TriePosition& subject) noexcept
            {
                auto lift = base_t::code;
                (subject.*lift) |= base_t::value;
            }
        };

        template <auto member_c, class T>
        struct FieldAndAssign: OP::AssocVal<member_c, T>
        {
            using base_t = OP::AssocVal<member_c, T>;

            using base_t::base_t;

            constexpr void operator()(TriePosition& subject) noexcept
            {
                auto lift = base_t::code;
                (subject.*lift) &= base_t::value;
            }
        };
    } //ns:_trie_position_args

    constexpr inline auto address(vtm::FarAddress addr) noexcept
    {
        return _trie_position_args::FieldAssign<&TriePosition::_node_addr, vtm::FarAddress>(addr);
    }

    constexpr inline auto terminality(std::uint_fast8_t c) noexcept
    {
        return _trie_position_args::FieldAssign<&TriePosition::_terminality, std::uint_fast8_t >(c);
    }

    constexpr inline auto terminality_and(std::uint_fast8_t c) noexcept
    {
        return _trie_position_args::FieldAndAssign<&TriePosition::_terminality, std::uint_fast8_t >(c);
    }

    constexpr inline auto terminality_or(std::uint_fast8_t c) noexcept
    {
        return _trie_position_args::FieldOrAssign<&TriePosition::_terminality, std::uint_fast8_t >(c);
    }

    constexpr inline auto chunk_size(vtm::fast_dim_t c) noexcept
    {
        return _trie_position_args::FieldAssign<&TriePosition::_chunk_size, vtm::fast_dim_t>(c);
    }

    constexpr inline auto chunk_size_plus(vtm::fast_dim_t c) noexcept
    {
        return _trie_position_args::FieldDelta<&TriePosition::_chunk_size, vtm::fast_dim_t>(c);
    }

    constexpr inline auto node_version(node_version_t c) noexcept
    {
        return _trie_position_args::FieldAssign<&TriePosition::_version, node_version_t>(c);
    }

} //ns:OP::trie
#endif //_OP_TRIE_TRIEPOSITION__H_

