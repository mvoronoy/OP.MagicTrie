#pragma once
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <memory>
#include <iterator>
#include <string>
#include <op/common/typedefs.h>

namespace OP
{
    namespace trie
    {
        namespace details
        {
            template <class SourceRange>
            struct SectionRangeTraits
            {
                using source_range_t = SourceRange;
                using source_iterator_t = typename source_range_t::iterator;
                using key_type = typename source_iterator_t::key_type;
                using key_t = key_type;
                using atom_string_t = key_type;

                using value_type = typename source_iterator_t::value_type;

            };
        } //ns::details
        template <class SourceRange>
        struct TrieSectionAdapter;
        /**
        *   Iterator over TrieSectionAdapter 
        */
        template <class Traits>
        struct SectionIterator
        {
            using iterator_category = std::forward_iterator_tag;

            using traits_t = Traits;
            using this_t = SectionIterator<traits_t>;
            using key_type = typename traits_t::key_type;
            using key_t = key_type;
            using value_type = typename traits_t::value_type;
            using owner_range_t = TrieSectionAdapter<typename traits_t::source_range_t>;
            using source_iterator_t = typename traits_t::source_iterator_t;
            friend owner_range_t;

            SectionIterator(
                std::shared_ptr< const owner_range_t > owner_range,
                size_t prefix_length,
                source_iterator_t source_iterator
            )
                : _owner_range(owner_range)
                , _prefix_length(prefix_length)
                , _source_iterator(source_iterator)
            {
                update_cache();
            }

            this_t& operator ++()
            {
                _owner_range.next(*this);
                return *this;
            }
            this_t operator ++(int)
            {
                this_t result(*this);
                this->operator++();
                return result;
            }
            value_type operator* () const
            {
                return OP::ranges::key_discovery::value(_source_iterator);
            }
            const key_type& key() const
            {
                return _cashed_str;
            }
            const source_iterator_t& source_iterator() const
            {
                return _source_iterator;
            }
        protected:
            source_iterator_t& source_iterator() 
            {
                return _source_iterator;
            }
            void update_cache()
            {
                if (_owner_range->source_range()->in_range(_source_iterator))
                {
                    _cashed_str = OP::ranges::key_discovery::key(_source_iterator).substr(_prefix_length);
                }
                else {
                    _cashed_str.clear();
                }
            }
        private:
            std::shared_ptr< const owner_range_t > _owner_range;
            source_iterator_t _source_iterator;
            key_t _cashed_str;
            size_t _prefix_length;
        };
        /**
        *   Special kind of OrderedRange that utilize feature of Trie where all entrie below prefix are lexicographicaly ordered.
        * This range provide access to ordered sequence of suffixes. In simplified view you can think about it as cut of trie entry:
        * \code
        *   trie
        *       ->prefixed_range( prefix )
        *       ->map([&](const auto& i){
        *           return key_discovery::key(i).substr( prefix.length() ); //cut string after the prefix
        *       });
        * \endcode
        * In compare with code above this Range returns ordered sequence
        */
        template <class SourceRange>
        struct TrieSectionAdapter : public OP::ranges::OrderedRange< SectionIterator< details::SectionRangeTraits<SourceRange> > >
        {
            static_assert(SourceRange::is_ordered_c, "Source range must support ordering");
            using this_t = TrieSectionAdapter<SourceRange>;
            using traits_t = details::SectionRangeTraits<SourceRange>;
            using source_range_t = typename traits_t::source_range_t;
            using iterator = SectionIterator<traits_t>;
            using source_iterator_t = typename traits_t::source_iterator_t;
            using atom_string_t = typename traits_t::atom_string_t;

            TrieSectionAdapter(
                std::shared_ptr<const source_range_t> source_range, 
                atom_string_t prefix) noexcept
                : _source_range(source_range)
                , _prefix(std::move(prefix))
            {
            }

            iterator begin() const override
            {
                return iterator(
                    std::static_pointer_cast<const this_t>(shared_from_this()), 
                    _prefix.length(), 
                    skip_first_if_empty(_source_range->begin()));
            }
            iterator end() const
            {
                return iterator();
            }
            bool in_range(const iterator& check) const override
            {
                if (_source_range->in_range(check.source_iterator()))
                {//check against matching the prefix
                    return OP::ranges::key_discovery::key(check.source_iterator())
                        .compare(0, _prefix.length(), _prefix) == 0;
                }
                return false;
            }
            void next(iterator& pos) const override
            {
                _source_range->next(pos.source_iterator());
                if (_source_range->in_range(pos.source_iterator()))
                { //cache result
                    pos.update_cache();
                }
            }

            iterator lower_bound(const typename iterator::key_type& key) const override
            {
                return iterator(
                    std::static_pointer_cast<const this_t>(shared_from_this()), 
                    _prefix.length(), 
                    skip_first_if_empty(_source_range->lower_bound(_prefix + key)));
            }
            const std::shared_ptr<const SourceRange>& source_range() const
            {
                return _source_range;
            }
        private:
            /**When prefixed_range starts iteration it may point to terminal string, so to avoid empty string need check and skip*/
            source_iterator_t& skip_first_if_empty(source_iterator_t& from) const
            {
                if (_source_range->in_range(from) )
                {
                    if (OP::ranges::key_discovery::key(from).length() == _prefix.length())
                        _source_range->next(from);
                }
                return from;
            }
            std::shared_ptr<const SourceRange> _source_range;
            atom_string_t _prefix;
        };
    } //ns:trie
}//ns:OP
