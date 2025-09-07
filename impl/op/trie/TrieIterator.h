#ifndef _OP_TRIE_TRIEITERATOR__H_
#define _OP_TRIE_TRIEITERATOR__H_

#include <op/trie/TrieNode.h>
#include <op/trie/TriePosition.h>

#include <string>
#include <vector>
#include <iterator>
namespace OP::trie
{

    template <class Container>
    class TrieIterator
    {
    public:
        using container_t = Container;

        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;

        using prefix_string_t = typename Container::key_t;
        using key_type = prefix_string_t;
        using key_t = prefix_string_t;
        using value_type = typename Container::value_type;
        using this_t = TrieIterator<Container>;

        using atom_t = OP::common::atom_t;

    private:
        friend Container;
        friend typename Container::node_t;

        using node_stack_t = std::vector<TriePosition>;
        node_stack_t _position_stack;
        const Container* _container = nullptr;
        prefix_string_t _prefix;
        node_version_t _version = { 0 };
        struct end_marker_t {};

        TrieIterator(const Container* container, const end_marker_t&) noexcept
            : _container(container)
        {
        }

    public:

        explicit TrieIterator(const Container* container) noexcept
            : _container(container)
            , _version(_container->version())
        {
        }

        TrieIterator() noexcept = default;

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
        
        value_type value() const
        {
            return _container->value_of(_position_stack.back());
        }

        /**Set iterator equal to end()*/
        void clear()
        {
            _position_stack.clear();
            _prefix.clear();
        }

    protected:

        template <class T>
        void _apply_arg_to_this(TriePositionArg<T>& arg)
        {
            if constexpr (std::is_same_v<atom_t, std::decay_t<T>>)
            {
                _prefix.append(1, arg._t);
            }    
        }

        /** Reverse `at` allows assign named values to contained TriePosition
        *   using negative offset (-1) from back
        */
        template <class ... Tx>
        const TriePosition& rat(TriePositionArg<Tx>&& ... tx)
        {
            (_apply_arg_to_this(tx), ...);
            auto& res = _position_stack.back();
            res.assign(std::move(tx)...);
            return res;
        }

        template <class Iterator>
        void update_stem(Iterator begin, Iterator end)
        {
            assert( _position_stack.back()._stem_size == dim_nil_c );
            auto size = end - begin;
            assert(size < std::numeric_limits<dim_t>::max());
            _position_stack.back()._stem_size = static_cast<dim_t>(size);
            _prefix.append(begin, end);
        }

        /**Add position to iterator*/
        template <class Iterator>
        void _emplace(TriePosition&& position, Iterator begin, Iterator end)
        {
            assert(position.key() <= std::numeric_limits<atom_t>::max());
            _prefix.append(1, (atom_t)position.key());
            _position_stack.emplace_back(std::move(position));
            update_stem(begin, end);
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
            auto& back = _position_stack.back();
            dim_t prev_delta = back.stem_size() + 1; 
            dim_t next_delta = position.stem_size() + 1;
            _prefix.reserve(_prefix.size() - prev_delta + next_delta + (end - begin));
            //leave only common part with previous state
            _prefix.resize(_prefix.size() - prev_delta);
            back = position;
            _prefix.append(1, (atom_t)position.key());
            update_stem(begin, end);
        }

        /** Upsert (insert or update) */
        void upsert_back(TriePosition&& position, const atom_t* begin, const atom_t* end)
        {
            assert(position.key() <= std::numeric_limits<atom_t>::max());

            if (_position_stack.empty() || _position_stack.back().address() == position.address())
            {
                emplace(std::move(position), begin, end);
                return;
            }
            update_back(std::move(position), begin, end);
        }

        template <class ... Tx>
        TriePosition& push(TriePositionArg<Tx>&& ... tx)
        {
            (_apply_arg_to_this(tx), ...);
            _position_stack.emplace_back(std::move(tx)...);
            return _position_stack.back();
        }

        void pop()
        {
            dim_t cut_len = _position_stack.back()._stem_size + 1; //+1 in the name of retained key
            _prefix.resize(_prefix.size() - cut_len);
            _position_stack.pop_back();
        }

        /** by poping back shrinks current iterator until it not bigger than `desired` (may be less with 
        *   respect to align to node's stem length)
        @param desired number of chars to leave.
        @return desired aligned that was really shrunk (aligned on deep value of last node)
        */
        void pop_until_fit(dim_t desired)
        {
            if (_prefix.length() <= desired)
                return; //nothing to do
            dim_t cut_len;
            for (cut_len = 0;
                (_prefix.length() - cut_len) > desired;
                )
            {
                cut_len += _position_stack.back()._stem_size + 1;
                _position_stack.pop_back();
            }
            _prefix.resize(_prefix.length() - cut_len);
        }

        size_t node_count() const
        {
            return _position_stack.size();
        }

        /**Snapshot version of trie modification when iterator was created*/
        node_version_t version() const
        {
            return this->_version;
        }
    };
    

} //ns:OP::trie
#endif //_OP_TRIE_TRIEITERATOR__H_

