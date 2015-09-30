
#pragma once

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <op/trie/Containers.h>
#include <op/trie/Node.h>
#include <op/trie/SegmentManager.h>

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
        template<class NodeManager>
        struct Trie
        {
        public:
            typedef TrieIterator iterator;
            typedef size_t idx_t;
            typedef NodeManager node_manager_t;
            typedef typename NodeManager::node_t node_t;
            typedef typename NodeManager::node_ptr_t node_ptr_t;

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
            node_ptr_t _root;
            //node_manager_t& _node_manager;
        private:
            Trie()
            {
            }
            /** create new node from existing one by extending or even changing container type*/
            node_ptr_t extend(node_ptr_t node)
            {

            }
        };
    }
}
