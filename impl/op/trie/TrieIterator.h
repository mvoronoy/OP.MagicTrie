#ifndef _OP_TRIE_TRIEITERATOR__H_
#define _OP_TRIE_TRIEITERATOR__H_
#include <op/trie/TrieNode.h>
#include <op/common/typedefs.h>
#include <op/trie/ValueArray.h>

#include <string>
#include <vector>
#include <iterator>
namespace OP
{
    namespace trie
    {
        typedef std::basic_string<atom_t> atom_string_t;
        
        struct TriePosition
        {
            
            TriePosition(FarAddress node_addr, NodeUid uid, dim_t key, dim_t deep, node_version_t version, Terminality term = term_no)
                : _node_addr(node_addr)
                , _uid(uid)
                , _key(key)
                , _deep(deep)
                , _terminality(term)
                , _version(version)
            {}
            TriePosition()
                : _node_addr{}
                , _uid{}
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
                    && _uid == other._uid //and only when all other checks succeeded make long check of uid
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
            /**Unique signature of node*/
            NodeUid _uid;
            /**horizontal position in node*/
            dim_t _key;
            /**Vertical position in node, for nodes without stem it is 0, for nodes with stem it is 
            a stem's position + 1*/
            dim_t _deep;
            /**Relates to ValueArrayData::has_XXX flags - codes what this iterator points to*/
            Terminality _terminality;
            node_version_t _version;
        };

        template <class Container>
        class TrieIterator : public std::iterator<
            std::bidirectional_iterator_tag,
            typename Container::value_type
        >
        {
            friend typename Container;
            friend typename Container::node_t;

            typedef std::vector<TriePosition> node_stack_t;
            node_stack_t _position_stack;
            const Container * _container;
            atom_string_t _prefix;
        public:
            typedef typename Container::value_type value_type;
            typedef TrieIterator<Container> this_t;

            TrieIterator(const Container * container)
                : _container(container)
            {
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
            inline this_t& operator -- ()
            {
                static_assert(false, "Not implemented yet");
                return *this;
            }
            inline this_t operator -- (int)
            {
                static_assert(false, "Not implemented yet");
                this_t result(*this);
                return result;
            }
            inline value_type operator * () const
            {
                return _container->value_of(_position_stack.back());
            }
            
            inline bool operator == (const this_t& other) const
            {
                if (is_end())
                    return other.is_end();
                if (other.is_end())
                    return false;
                // []

                return _position_stack.back() == other._position_stack.back();
            }
            inline bool operator != (const this_t& other) const
            {
                return !operator == (other);
            }
            inline bool is_end() const
            {
                return _position_stack.empty();
            }
            const atom_string_t& prefix() const
            {
                return _prefix;
            }
        protected:
            /**Add position to iterator*/
            template <class Iterator>
            void emplace(TriePosition&& position, Iterator begin, Iterator end)
            {
                if (position.key() >= std::numeric_limits<atom_t>::max())
                    throw std::out_of_range("Range must be in [0..255]");
                size_t length = end - begin;
                _prefix.append(1, (atom_t)position.key());
                _prefix.append(begin, end);
                _position_stack.emplace_back(
                    std::move(std::make_pair(std::move(position), length+1)));
            }
            void emplace(TriePosition&& position, const atom_t* begin, const atom_t* end)
            {
                emplace<decltype(begin)>(std::move(position), begin, end);
            }
            /**Add position to iterator*/
            void update_back(TriePosition&& position, const atom_t* begin, const atom_t* end)
            {
                if (position.key() >= std::numeric_limits<atom_t>::max())
                    throw std::out_of_range("Range must be in [0..255]");
                auto &back = _position_stack.back();
                _prefix.resize(_prefix.length() - back.deep()+1);

                auto length = static_cast<decltype(position._deep)>(end - begin);
                _prefix.back() = (atom_t)position.key();
                _prefix.append(begin, end);
                _position_stack.back() = std::move(position);
                _position_stack.back()._deep = length+1;
            }
            TriePosition& back()
            {
                return _position_stack.back();
            }
            void pop()
            {
                auto cut_len = _position_stack.back()._deep;
                _prefix.resize(_prefix.length() - cut_len);
                _position_stack.pop_back();
            }
            template <class Iterator>
            void correct_suffix(Iterator& new_suffix_begin, Iterator& new_suffix_end)
            {
                auto cut_len = _position_stack.back().second;
                //_prefix.replace(_prefix.length() - cut_len, cut_len);
                auto l = _prefix.length();
                _prefix.append(new_suffix_begin, new_suffix_end);
                _position_stack.back().second += (_prefix.length() - l);
                //_position_stack.pop_back();
            }
            size_t deep() const
            {
                return _position_stack.size();
            }
        };
    } //ns:trie
} //ns:OP
#endif //_OP_TRIE_TRIEITERATOR__H_

