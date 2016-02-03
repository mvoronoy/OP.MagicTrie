#ifndef _OP_TRIE_TRIEITERATOR__H_
#define _OP_TRIE_TRIEITERATOR__H_
#include <op/trie/TrieNode.h>
#include <vector>
#include <iterator>
namespace OP
{
    namespace trie
    {
        template <class Container>
        class TrieIterator : public std::iterator<
            std::bidirectional_iterator_tag,
            typename Container::value_type
        >
        {
            friend class Container;
            typedef std::vector<TriePosition> node_stack_t;
            typedef Container::value_type value_type;
            node_stack_t _position_stack;
            Container * _container;
        public:
            typedef TrieIterator<Container> this_t;

            TrieIterator(Container * container, TriePosition initial)
                : _container(container)
            {
                _position_stack.emplace_back(std::move(initial));
            }
            TrieIterator()
            {
            }
            inline this_t& operator ++ ()
            {
                _container->next(*this);
                return *this;
            }
            inline this_t operator ++ (int)
            {
                this_t result(*this);
                _container->next(*this);
                return result;
            }
            inline value_type operator * () const
            {
                return _container->value_of(_position_stack.back());
            }
            
            inline operator == (const this_t& other) const
            {
                if (is_end())
                    return other.is_end();
                if (other.is_end())
                    return false;
                // []

                return _position_stack.back() == other._position_stack.back();
            }
            inline operator != (const this_t& other) const
            {
                return !operator == (other);
            }
            inline bool is_end() const
            {
                return _position_stack.empty();
            }
        protected:
            /**Add position to iterator*/
            void emplace(TriePosition&& position)
            {
                _position_stack.emplace_back(std::move(initial));
            }
        };
    } //ns:trie
} //ns:OP
#endif //_OP_TRIE_TRIEITERATOR__H_

