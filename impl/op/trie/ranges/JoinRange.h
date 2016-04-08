#ifndef _OP_TRIE_RANGES_PREFIX_RANGE__H_
#define _OP_TRIE_RANGES_PREFIX_RANGE__H_
#include <iterator>
#include <op/trie/ranges/SuffixRange.h>

namespace OP
{
    namespace trie
    {
        template <class Iterator, class OwnerRange>
        struct JoinRangeIterator :
            public std::iterator<
            std::forward_iterator_tag,
            typename Iterator::value_type
            >
        {
            typedef Iterator iterator;
            typedef JoinRangeIterator<Iterator, OwnerRange> this_t;
            typedef typename Iterator::value_type value_type;
            friend OwnerRange;
            JoinRangeIterator(const OwnerRange& owner_range, iterator left, iterator right)
                : _owner_range(owner_range)
                , _left(left)
                , _right(right)
            {}
            this_t& operator ++()
            {
                _owner_range.next(*this);
                return *this;
            }
            this_t operator ++(int)
            {
                this_t result = *this;
                _owner_range.next(*this);
                return result;
            }
            value_type& operator* ()
            {
                return *_left;
            }
            const value_type& operator* () const
            {
                return *_left;
            }
        private:
            const iterator& left() const
            {
                return _left;
            }
            const iterator& right() const
            {
                return _right;
            }

            const OwnerRange& _owner_range;
            iterator _left, _right;
        };
        template <class SourceRange1, class SourceRange2>
        struct JoinRange : public SuffixRange< 
                JoinRangeIterator<typename SourceRange1::iterator, JoinRange<SourceRange1, SourceRange2> > >
        {
            typedef JoinRange<SourceRange1, SourceRange2> this_t;
            typedef JoinRangeIterator<typename SourceRange1::iterator, typename this_t> iterator;
            /**
            * @param iterator_comparator - binary predicate `bool(const iterator&, const iterator&)` that implements 'less' compare of current iterator positions
            */
            template <class BinaryPredicate>
            JoinRange(const SourceRange1 & r1, const SourceRange2 & r2, BinaryPredicate iterator_comparator)
                : _left(r1)
                , _right(r2)
                , _iterator_comparator(iterator_comparator)
            {
            }
            
            iterator begin() const override
            {
                iterator result(*this, _left.begin(), _right.begin());
                seek(result);
                return result;
            }
            bool in_range(const iterator& check) const override
            {
                return _left.in_range(check.left()) && _right.in_range(check.right());
            }
            void next(iterator& pos) const override
            {
                seek(pos);
            }
        private:
            void seek(iterator &pos) const
            {
                while (in_range(pos))
                {
                    if (_iterator_comparator(pos._left, pos._right)) {
                        ++pos._left;
                    }
                    else {
                        if (!_iterator_comparator(pos._right, pos._left)) {
                            return;
                        }
                        ++pos._right;
                    }
                }

            }
            const SourceRange1& _left;
            const SourceRange2& _right;
            typedef typename SourceRange1::iterator i1;
            typedef typename SourceRange2::iterator i2;
            const std::function<bool(const i1&, const i2&)> _iterator_comparator;
        };
    } //ns: trie
} //ns: OP
#endif //_OP_TRIE_RANGES_PREFIX_RANGE__H_
