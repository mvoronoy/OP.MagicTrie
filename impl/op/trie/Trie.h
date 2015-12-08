
#pragma once

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <op/trie/Containers.h>
#include <op/trie/Node.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Bitset.h>

namespace OP
{
    namespace trie
    {
        struct TrieIterator
        {
            unsigned version() const
            {
                return _version.load();
            }
        private:
            std::atomic<unsigned> _version;
        };

        template <class T>
        struct PersistedReference
        {
            typedef T type;
            FarAddress address;
        };

        template <class Payload>
        struct TrieNode
        {
            typedef Bitset<4, std::uint64_t> presence_t;
            //typedef PersistedReference<> stems_t;
            typedef std::tuple<presence_t> node_def_t;
        };
        template <class Payload>
        struct SubTrie
        {
            typedef TrieIterator iterator;
            /**start lexicographical ascending iteration over trie content. Following is a sequence of iteration:
            *   - a
            *   - aaaaaaaaaa
            *   - abcdef
            *   - b
            *   - ...
            */
            virtual iterator begin() = 0;
            virtual iterator end() = 0;
            
            
            virtual std::unique_ptr<SubTrie<Payload> > subtree(const atom_t*& begin, const atom_t* end) const = 0;
            
        };
        template <class SegmentTopology>
        struct Trie
        {
        public:
            typedef TrieIterator iterator;
            typedef size_t idx_t;

            virtual ~Trie()
            {
            }
            static void create_new(std::unique_ptr<SegmentManager>&& segment_manager)
            {
                //make root for trie
                auto r = _node_manager.create_new(trie_c);
            }
            static void open(std::unique_ptr<SegmentManager>&& segment_manager)
            {

            }
            iterator end() const
            {
                return iterator();
            }

            bool insert(const atom_t* begin, const atom_t* end, iterator * result = nullptr)
            {
                if (begin == end)
                    return false; //empty string cannot be inserted
                node_ptr_t node = _root;
                while (begin != end)
                {
                    _node_manager.grant_state(*node, node_manager_t::findable_c);

                }
                const atom_t* prev_it = begin;
                unsigned pos = node->accommodate(begin, end);
                if (pos == ~0u)//no capacity to insert
                {
                    node = extend(node);
                    pos = node->accommodate(begin, end);
                }
            }
        private:
            //typedef NodeManager<atom_t> node_manager_t;
            //node_manager_t& _node_manager;
        private:
            Trie()
            {
            }
        };
    }
}
