#pragma once
#include <op/ranges/PrefixRange.h>
#include <memory>

namespace OP
{
    namespace trie
    {
        /**
        *   Allows trie to mimic OP::ranges::PrefixRange capabilities
        */
        template <class TTrie>
        struct TrieRangeAdapter : public OP::ranges::OrderedRange< typename TTrie::iterator >
        {
            using trie_t = TTrie;
            using iterator = typename trie_t::iterator;

            TrieRangeAdapter(std::shared_ptr<const trie_t> parent) noexcept
                : TrieRangeAdapter(parent, parent->begin(), parent->end())
            {
            }

            iterator begin() const override
            {
                return _begin;
            }
            iterator end() const
            {
                return _end;
            }
            bool in_range(const iterator& check) const override
            {
                return _parent->in_range(check);
            }
            void next(iterator& pos) const override
            {
                _parent->next(pos);
            }

            iterator lower_bound(const typename iterator::key_type& key) const override
            {
                return _parent->lower_bound(key);
            }
        protected:
            TrieRangeAdapter(std::shared_ptr<const trie_t> parent, iterator begin, iterator end) noexcept
                : _parent(parent)
                , _begin(begin) 
                , _end(end)
            {
            }
            const std::shared_ptr<const trie_t>& get_parent() const
            {
                return _parent;
            }
        private:
            std::shared_ptr<const trie_t> _parent;
            iterator _begin, _end;
        };
        
        template <class TTrie>
        struct TakewhileTrieRangeAdapter : public TrieRangeAdapter<TTrie>
        {
            using in_range_predicate_t = std::function<bool(const iterator& check)>;
            using base_t = TrieRangeAdapter<TTrie>;
            
            TakewhileTrieRangeAdapter(std::shared_ptr<const trie_t> parent, iterator begin, iterator end, in_range_predicate_t in_range_predicate)
                : base_t(parent, iterator begin, iterator end)
                , _in_range_predicate(std::forward<in_range_predicate_t>(in_range_predicate))
            {}
            bool in_range(const iterator& check) const override
            {
                return base_t::in_range(check) && _in_range_predicate(check);
            }
        private:
            in_range_predicate_t _in_range_predicate;
        };
        template <class Trie, class String>
        struct PrefixSubrangeAdapter : public OP::ranges::OrderedRange< typename Trie::iterator >
        {
            using trie_t = Trie;
            using iterator = typename trie_t::iterator;

            PrefixSubrangeAdapter(std::shared_ptr<const trie_t> parent, iterator start_from, String prefix) noexcept
                : _parent(parent)
                , _prefix(prefix)
                , _start_from(start_from)
            {
            }

            iterator begin() const override
            {
                return _start_from;
            }
            iterator end() const
            {
                return _parent->end();
            }
            bool in_range(const iterator& check) const override
            {
                return _parent->in_range(check) && starts_with(check);
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }

            iterator lower_bound(const typename iterator::key_type& key) const
            {
                auto result = _parent->lower_bound(key);
                return starts_with(result) ? result : end();
            }
        protected:
            const std::shared_ptr<const trie_t>& get_parent() const
            {
                return _parent;
            }
        private:
            bool starts_with(const iterator& i) const
            {
                const auto & key = i.key();
                if (key.length() < _prefix.length())
                    return false;
                return std::equal(_prefix.begin(), _prefix.end(), key.begin());
            }
            std::shared_ptr<const trie_t> _parent;
            String _prefix;
            iterator _start_from;
        };

        /**Used to iterate over immediate children of parent iterator*/
        template <class Trie>
        struct ChildRangeAdapter : public PrefixSubrangeAdapter<Trie, typename Trie::iterator::key_type>
        {
            typedef PrefixSubrangeAdapter<Trie, typename Trie::iterator::key_type> supert_t;

            ChildRangeAdapter(std::shared_ptr<const trie_t> parent, iterator begin, iterator prefix)
                : supert_t(parent, begin, prefix.key())
            {
            }

            void next(iterator& pos) const override
            {
                get_parent()->next_sibling(pos);
            }

        };

    } //ns:trie
}//ns:OP
