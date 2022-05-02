#ifndef _OP_TRIE_TRIEITERATOR__H_
#define _OP_TRIE_TRIEITERATOR__H_

#include <op/trie/TrieNode.h>
#include <op/common/typedefs.h>
#include <op/trie/ValueArray.h>
#include <op/trie/TriePosition.h>

#include <string>
#include <vector>
#include <iterator>
namespace OP
{
    namespace trie
    {

        template <class Container>
        class TrieIterator 
        {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using difference_type   = std::ptrdiff_t;

            typedef OP::trie::atom_string_t prefix_string_t;
            typedef prefix_string_t key_type;
            typedef prefix_string_t key_t;
            typedef typename Container::value_type value_type;
            typedef TrieIterator<Container> this_t;

        private:
            friend Container;
            friend typename Container::node_t;

            typedef std::vector<TriePosition> node_stack_t;
            node_stack_t _position_stack;
            const Container * _container = nullptr;
            prefix_string_t _prefix;
            node_version_t _version;
            struct end_marker_t {};
            TrieIterator(const Container * container, const end_marker_t&) noexcept
                : _container(container)
                , _version(0)
            {
            }
        public:

            explicit TrieIterator(const Container * container) noexcept
                : _container(container)
                , _version(_container->version())
            {

            }
            TrieIterator() = default; 

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
                assert(false);//, "Not implemented yet");
                return *this;
            }
            inline this_t operator -- (int)
            {
                assert(false);//, "Not implemented yet");
                this_t result(*this);
                return result;
            }
            inline value_type operator * () const
            {
                return _container->value_of(_position_stack.back());
            }
            
            inline bool operator == (const this_t& other) const  noexcept
            {
                if (is_end())
                    return other.is_end();
                if (other.is_end())
                    return false;
                // []

                return _position_stack.back() == other._position_stack.back();
            }
            inline bool operator < (const this_t& other) const  noexcept
            {
                if (is_end())
                    return false; //even if other is 'end' too it is not less
                if (other.is_end())
                {
                    return true;  //when other is end then this less anyway
                }
                return _prefix < other._prefix;
            }

            inline bool operator != (const this_t& other) const  noexcept
            {
                return !operator == (other);
            }
            inline bool is_end() const  noexcept
            {
                return _position_stack.empty();
            }
            const prefix_string_t& key() const& noexcept
            {
                return _prefix;
            }
            prefix_string_t key() && noexcept
            {
                return std::move(_prefix);
            }
            value_type value () const
            {
                return _container->value_of(_position_stack.back());
            }
        protected:
            /**Add position to iterator*/
            template <class Iterator>
            void _emplace(TriePosition&& position, Iterator begin, Iterator end)
            {
                if (position.key() > std::numeric_limits<atom_t>::max())
                    throw std::out_of_range("Range must be in [0..255]");
                auto length = static_cast<dim_t>(end - begin);
                _prefix.append(1, (atom_t)position.key());
                _prefix.append(begin, end);
                _position_stack.emplace_back(
                    std::move(position));
            }
            void emplace(TriePosition&& position, const atom_t* begin, const atom_t* end)
            {
                _emplace<decltype(begin)>(std::move(position), begin, end);
            }
            /**Update last entry in this iterator, then add rest tail to iterator*/
            void update_back(const TriePosition& position, const atom_t* begin, const atom_t* end)
            {
                if (position.key() > std::numeric_limits<atom_t>::max())
                    throw std::out_of_range("Range must be in [0..255]");
                auto &back = _position_stack.back();
                _prefix.resize(_prefix.length() - back.deep()+1);

                _prefix.back() = (atom_t)position.key();
                _prefix.append(begin, end);
                back = position;
            }
            /**Upsert (insert or update) */
            void upsert_back(TriePosition&& position, const atom_t* begin, const atom_t* end)
            {
                if (position.key() > std::numeric_limits<atom_t>::max())
                    throw std::out_of_range("Range must be in [0..255]");
                if (_position_stack.empty() || _position_stack.back().address() == position.address() )
                {
                    emplace(std::move(position), begin, end);
                    return;
                }
                update_back(std::move(position), begin, end);
            }
            TriePosition& back()
            {
                return _position_stack.back();
            }
            const TriePosition& back() const
            {
                return _position_stack.back();
            }
            void pop()
            {
                auto cut_len = _position_stack.back()._deep;
                _prefix.resize(_prefix.length() - cut_len);
                _position_stack.pop_back();
            }
            /** by poping back shrinks current iterator until it not bigger than `desired` (may be less with respect to allign to node's stem length)
            @param desired number of chars to leave. 
            @return desired alligned that was really shrinked (alligned on deep value of last node)
            */
            void pop_until_fit(dim_t desired)
            {
                if( _prefix.length() <= desired )
                    return; //nothing to do
                dim_t cut_len;
                for(cut_len = 0; 
                    (_prefix.length() - cut_len) > desired; 
                    )
                {
                    cut_len += _position_stack.back()._deep;
                    _position_stack.pop_back();
                }
                _prefix.resize(_prefix.length() - cut_len);
            }
            /**Update last entry in iterator*/
            template <class Iterator>
            void correct_suffix(Iterator& new_suffix_begin, Iterator& new_suffix_end)
            {
                auto &back = _position_stack.back();
                auto cut_len = back._deep;
                _prefix.resize(_prefix.length() - back._deep);
                auto l = _prefix.length();
                _prefix.append(new_suffix_begin, new_suffix_end);
                back._deep = static_cast<dim_t>((_prefix.length()) - l);
            }
            size_t deep() const
            {
                return _position_stack.size();
            }
            /**Snapshot version of trie modification when iterator was created*/
            node_version_t version() const
            {
                return this->_version;
            }
            /**Set iterator equal to end()*/
            void clear()
            {
                _position_stack.clear();
                _prefix.clear();
            }
        };
    } //ns:trie

} //ns:OP
#endif //_OP_TRIE_TRIEITERATOR__H_

