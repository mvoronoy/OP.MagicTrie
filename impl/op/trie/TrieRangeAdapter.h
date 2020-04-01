#pragma once
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <memory>
#include <functional>

namespace OP
{
    namespace trie
    {
        /**
        *   Helper functor that implements `f(x) = x`
        */
        template <class Iterator>
        struct IdentityFactory
        {
            IdentityFactory(Iterator i) :
                _iterator(std::move(i)) {}
            
            const Iterator& operator()() const
            {
                return _iterator;
            }
            Iterator& operator()()
            {
                return _iterator;
            }
        private:
            Iterator _iterator;
        };

        /**Implement functor for subrange method to implement predicate that detects end of range iteration*/
        template <class Iterator>
        struct StartWithPredicate
        {
            StartWithPredicate(atom_string_t && prefix)
                : _prefix(std::move(prefix))
            {
            }
            explicit StartWithPredicate(const atom_string_t& prefix)
                : _prefix(prefix)
            {
            }

            bool operator()(const Iterator& check) const
            {
                auto & str = OP::ranges::key_discovery::key(check);
                if (str.length() < _prefix.length())
                    return false;
                return std::equal(_prefix.begin(), _prefix.end(), str.begin());
            }
        private:
            atom_string_t _prefix;
        };

        /**
        *   Allows trie to mimic OP::ranges::PrefixRange capabilities
        */
        template <class TTrie>
        struct TrieRangeAdapter : public OP::ranges::OrderedRange< typename TTrie::iterator >
        {
            using trie_t = TTrie;
            using iterator = typename trie_t::iterator;
            using identity_factory_f = IdentityFactory<iterator>;
            using iterator_factory_f = std::function<iterator()>;

            TrieRangeAdapter(std::shared_ptr<const trie_t> parent, iterator_factory_f f_begin, iterator_factory_f f_end) noexcept
                : _parent(std::move(parent))
                , _begin_factory(f_begin) 
                , _end_factory(f_end)
            {
            }
            TrieRangeAdapter(std::shared_ptr<const trie_t> parent) noexcept
                : TrieRangeAdapter(
                    std::move(parent), 
                    [this]()->iterator {return this->get_parent()->begin(); } ,
                    [this]()->iterator {return this->get_parent()->end(); } 
                )
            {
            }
            TrieRangeAdapter(std::shared_ptr<const trie_t> parent, iterator begin, iterator end) noexcept
                : _parent(std::move(parent))
                , _begin_factory(identity_factory_f{ std::move(begin) })
                , _end_factory(identity_factory_f{ std::move(end) })
            {
            }

            iterator begin() const override
            {
                return _begin_factory();
            }
            iterator end() const
            {
                return _end_factory();
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
            const std::shared_ptr<const trie_t>& get_parent() const
            {
                return _parent;
            }

        private:
            std::shared_ptr<const trie_t> _parent;
            iterator_factory_f _begin_factory, _end_factory;
        };
        
        /**
        *   Ordered range that allows iteration until iterator position meets logical criteria
        */
        template <class TTrie>
        struct TakewhileTrieRangeAdapter : public TrieRangeAdapter<TTrie>
        {
            using in_range_predicate_t = std::function<bool(const iterator& check)>;
            using base_t = TrieRangeAdapter<TTrie>;
            
            TakewhileTrieRangeAdapter(std::shared_ptr<const trie_t> parent, iterator begin, iterator end, in_range_predicate_t in_range_predicate)
                : base_t(parent, std::move(begin), std::move(end))
                , _in_range_predicate(std::move(in_range_predicate))
            {}

            TakewhileTrieRangeAdapter(std::shared_ptr<const trie_t> parent, iterator_factory_f f_begin, iterator_factory_f f_end, in_range_predicate_t in_range_predicate)
                : base_t(parent, std::move(f_begin), std::move(f_end))
                , _in_range_predicate(std::move(in_range_predicate))
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

            PrefixSubrangeAdapter(std::shared_ptr<const trie_t> parent, String prefix) noexcept
                : _parent(parent)
                , _prefix(std::forward<String>(prefix))
            {
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
            const typename iterator::key_type& prefix() const
            {
                return _prefix;
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
        };

        /**Used to iterate over immediate children of parent iterator*/
        template <class Trie>
        struct ChildRangeAdapter : public TrieRangeAdapter<Trie>
        {
            using super_t = TrieRangeAdapter<Trie> ;

            ChildRangeAdapter(std::shared_ptr<const trie_t> parent, iterator_factory_f f_begin)
                : super_t(parent, std::move(f_begin), [this]() {return this->get_parent()->end(); })
            {
            }
            iterator begin() const override
            {
                iterator super_i = super_t::begin();
                return get_parent()->first_child(super_i);
            }

            void next(iterator& pos) const override
            {
                get_parent()->next(pos);
            }
        };

        /**Used to iterate over keys situated on the same trie level*/
        template <class Trie, class String>
        struct SiblingRangeAdapter : public TrieRangeAdapter<Trie>
        {
            using super_t = TrieRangeAdapter<Trie>;

            SiblingRangeAdapter(std::shared_ptr<const trie_t> parent, iterator_factory_f f_begin)
                : super_t(std::move(parent), std::move(f_begin), [this]() {return this->get_parent()->end(); })
            {
            }

            void next(iterator& pos) const override
            {
                get_parent()->next_sibling(pos);
            }
        };
    } //ns:trie
}//ns:OP
