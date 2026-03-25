#ifndef _OP_TRIE_TRIEITERATOR__H_
#define _OP_TRIE_TRIEITERATOR__H_

#include <op/trie/TrieNode.h>
#include <op/trie/TriePosition.h>

#include <string>
#include <vector>
#include <iterator>
namespace OP::trie
{
    namespace _trie_position_args
    {
        struct AppendKeyToIterator
        {
            OP::common::fast_atom_t _key;
            constexpr AppendKeyToIterator(OP::common::fast_atom_t key) noexcept
                : _key(key)
            {
            }

            template <class TIterator>
            void operator()(TIterator& iter)
            {
                auto& back = iter._position_stack.back();
                iter._prefix.push_back(_key);
                ++back._chunk_size;
            }
        };
    }

    constexpr inline auto key(OP::common::fast_atom_t key) noexcept
    {
        return _trie_position_args::AppendKeyToIterator(key);
    }


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
        using fast_atom_t = OP::common::fast_atom_t;
        using dim_t = OP::vtm::dim_t;
        using fast_dim_t = OP::vtm::fast_dim_t;

    private:
        friend Container;
        friend typename Container::node_t;
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

        friend _trie_position_args::AppendKeyToIterator;


        //template <class Iter>
        //friend constexpr inline auto stem(Iter begin, Iter end) noexcept
        //{
        //    return AppendStemToIterator<Iter>(std::move(begin), std::move(end));
        //}

        //template <class Iter>
        //friend constexpr inline auto key_and_stem(Iter begin, Iter end) noexcept
        //{
        //    return AppendStemToIterator<Iter>(std::move(begin), std::move(end));
        //}

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
            return _container->value_of(*this);
        }

#ifdef OP_CPP20_FEATURES
        auto operator <=> (const this_t& other) const 
        {
            if (is_end())
                return other.is_end() ? std::strong_ordering::equal : std::strong_ordering::greater;
            if (other.is_end())
                return std::strong_ordering::less;
            return _prefix <=> other._prefix;
        }
#endif //OP_CPP20_FEATURES

        inline bool operator == (const this_t& other) const noexcept
        {
            if (is_end())
                return other.is_end();
            if (other.is_end())
                return false;
            const auto& back = _position_stack.back();
            const auto& other_back = other._position_stack.back();
            return back.address() == other_back.address() 
                && rat_key() == other.rat_key();
        }

        inline bool operator < (const this_t& other) const noexcept
        {
            if (is_end())
                return false; //even if other is 'end' too it is not less
            if (other.is_end())
            {
                return true;  //when other is end then this less anyway
            }
            return _prefix < other._prefix;
        }

        inline bool operator != (const this_t& other) const noexcept
        {
            return !operator == (other);
        }

        inline bool is_end() const noexcept
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
            return _container->value_of(*this);
        }

        /**Set iterator equal to end()*/
        void clear()
        {
            _position_stack.clear();
            _prefix.clear();
        }

    protected:
        template<typename T>
        void _dispatch_mutator_call(T&& t) 
        {
            if constexpr (requires { t(*this); }) 
            {
                t(*this);
            } 
            else 
            {
                // Logic for the TriePosition mutator
                t(_position_stack.back());
            }
        }
        /** Reverse `at` allows assign named values to contained TriePosition
        *   using negative offset (-1) from back
        */
        template <CallableWithTriePosition... Tx>
        TriePosition& rat(Tx&& ... tx)
        {
            (_dispatch_mutator_call(std::forward<Tx>(tx)), ...);
            return _position_stack.back();
        }

        TriePosition& rat()
        {
            return _position_stack.back();
        }

        const TriePosition& rat() const
        {
            return _position_stack.back();
        }

        atom_t rat_key() const
        {
            assert(_prefix.size() >= _position_stack.back()._chunk_size);
            //take from prefix (last-stem_size) character
            return _prefix.at(_prefix.size() - _position_stack.back()._chunk_size);
        }

        template <class Iterator>
        void update_stem(Iterator begin, Iterator end)
        {
            auto size = std::distance(begin, end);
            assert(size < std::numeric_limits<dim_t>::max());
            _position_stack.back()._chunk_size += static_cast<fast_dim_t>(size);
            _prefix.append(begin, end);
        }

        ///**Add position to iterator*/
        //template <class Iterator>
        //void _emplace(TriePosition&& position, Iterator begin, Iterator end)
        //{
        //    assert(position.key() <= std::numeric_limits<atom_t>::max());
        //    _prefix.append(1, (atom_t)position.key());
        //    _position_stack.emplace_back(std::move(position));
        //    update_stem(begin, end);
        //}

        void emplace(const TriePosition& position)
        {
            _position_stack.push_back(position);
        }

        /**Update last entry in this iterator, then add rest tail to iterator*/
        void update_back(const TriePosition& position)
        {
            pop();
            _position_stack.push_back(position);
        }

        template <CallableWithTriePosition... Tx>
        TriePosition& push(Tx&& ... tx)
        {
            _position_stack.emplace_back(TriePosition{});
            (_dispatch_mutator_call(std::forward<Tx>(tx)), ...);
            return _position_stack.back();
        }

        void pop()
        {
            dim_t cut_len = _position_stack.back()._chunk_size;
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
            OP::vtm::fast_dim_t cut_len = 0;
            while((_prefix.length() - cut_len) > desired)
            {
                cut_len += _position_stack.back()._chunk_size + 1;
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

