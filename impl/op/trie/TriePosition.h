#pragma once
#ifndef _OP_TRIE_TRIEPOSITION__H_
#define _OP_TRIE_TRIEPOSITION__H_

#include <op/common/typedefs.h>
#include <cstdint>
#include <op/common/typedefs.h>
#include <op/trie/ValueArray.h>

namespace OP
{
    namespace trie
    {
        
        typedef std::uint32_t node_version_t;
        struct TriePosition
        {
            TriePosition(FarAddress node_addr, dim_t key, dim_t deep, node_version_t version, Terminality term = term_no) noexcept
                : _node_addr(node_addr)
                , _key(key)
                , _deep(deep)
                , _terminality(term)
                , _version(version)
            {}
            TriePosition() noexcept
                : _node_addr{}
                , _key(dim_nil_c)
                , _terminality(term_no)
                , _deep{0}
            {
            }
            inline bool operator == (const TriePosition& other) const
            {
                return _node_addr == other._node_addr //first compare node address as simplest comparison
                    && _key == other._key //check in-node position then
                    && _deep == other._deep
                    ;
            }
            node_version_t version() const
            {
                return _version;
            }
            /**Offset inside node. May be nil_c - if this position points to `end` */
            dim_t key() const
            {
                return _key;
            }
            FarAddress address() const
            {
                return _node_addr;
            }
            dim_t deep() const
            {
                return _deep;
            }
            /**
            * return combination of flag presence at current point
            * @see Terminality enum
            */
            Terminality terminality() const
            {
                return _terminality;
            }
            FarAddress _node_addr;
            /**horizontal position in node*/
            dim_t _key;
            /**Vertical position in node, for nodes without stem it is 0, for nodes with stem it is 
            a stem's position + 1*/
            dim_t _deep;
            /**Relates to ValueArrayData::has_XXX flags - codes what this iterator points to*/
            Terminality _terminality;
            node_version_t _version;
        };

    } //ns:trie

} //ns:OP
#endif //_OP_TRIE_TRIEPOSITION__H_

