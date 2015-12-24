#ifndef _OP_TRIE_TRIE__H_
#define _OP_TRIE_TRIE__H_

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <memory>
#include <future>
#include <op/trie/Containers.h>
#include <op/trie/FixedSizeMemoryManager.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/Bitset.h>
#include <op/trie/HashTable.h>
#include <op/trie/StemContainer.h>
#include <op/trie/MemoryManager.h>

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

        struct TrieNode
        {
            typedef Bitset<4, std::uint64_t> presence_t;
            typedef OP::trie::containers::HashTableData stem_hash_t;
            typedef OP::trie::stem::StemData stems_t;
            typedef std::tuple<presence_t, stem_hash_t, stems_t> node_def_t;
            node_def_t _data;
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
        
        template <class TSegmentManager, class Payload, std::uint32_t initial_node_count = 1024>
        struct Trie
        {
        public:
            typedef TrieIterator iterator;
            typedef Trie<TSegmentManager, Payload, initial_node_count> this_t;

            virtual ~Trie()
            {
            }
            
            static std::shared_ptr<Trie> create_new(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                //make root for trie
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                return r;
            }
            static std::shared_ptr<Trie> open(std::shared_ptr<TSegmentManager>& segment_manager)
            {
                auto r = std::shared_ptr<this_t>(new this_t(segment_manager));
                return r;
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
            typedef FixedSizeMemoryManager<TrieNode, initial_node_count> node_manager_t;
            typedef SegmentTopology<node_manager_t, MemoryManager/*Memory manager must go last*/> topology_t;
            std::unique_ptr<topology_t> _topology_ptr;
        private:
            Trie(std::shared_ptr<TSegmentManager>& segments)
            {
                _topology_ptr = std::make_unique<topology_t>(segments);
            }
        };
    }
}
#endif //_OP_TRIE_TRIE__H_
