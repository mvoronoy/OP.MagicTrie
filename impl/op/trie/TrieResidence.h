#ifndef _OP_TRIE_TRIERESIDENCE__H_
#define _OP_TRIE_TRIERESIDENCE__H_

#include <op/vtm/SegmentManager.h>
#include <op/common/Unsigned.h>
#include <op/trie/TriePosition.h>

namespace OP::trie
{
    namespace details
    {
        template <typename  ... Cx>
        constexpr std::uint64_t sign_code(Cx ... c) noexcept
        {
            std::uint64_t result = 0;
            auto step = [&](std::uint64_t ch){ result = (result << 8) | ch; };
            ((step(c)),...);
            return result;
        }

    }//ns::details

    /** Plain data structure to store metainformation of trie */
    struct TrieHeader
    {
        using FarAddress = vtm::FarAddress;

        constexpr static inline std::uint64_t signature_c = details::sign_code('O', 'P', 'M', 't', 'r', 'i', 'e');

        TrieHeader()
            : _root{}
            , _count(0)
            , _nodes_allocated(0)
            , _version(0)
        {
        }

        constexpr bool is_valid() const noexcept
        {
            return _signature == signature_c;
        }

        const std::uint64_t _signature = signature_c;

        /**Where root resides*/
        FarAddress _root;
        /**Total count of terminal entries*/
        std::uint64_t _count;
        /**Number of nodes (pages) allocated*/
        std::uint64_t _nodes_allocated;
        /** Total version of trie */
        trie_version_t _version;
    };

}//ns:OP::trie

#endif //_OP_TRIE_TRIERESIDENCE__H_
