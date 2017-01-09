#ifndef _OP_RANGES_UNION_ALL_RANGE__H_
#define _OP_RANGES_UNION_ALL_RANGE__H_
#include <iterator>
#include <op/ranges/SuffixRange.h>

#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

namespace OP
{
    namespace ranges
    {
        
        template <class OwnerRange>
        struct UnionAllRangeIterator :
            public std::iterator<
            std::forward_iterator_tag,
            typename OwnerRange::left_iterator::value_type
            >

        {
            typedef UnionAllRangeIterator<OwnerRange> this_t;
            typedef typename OwnerRange::left_iterator::value_type value_type;
            typedef typename OwnerRange::left_iterator::key_type key_type;
            typedef decltype(std::declval<OwnerRange::left_iterator>().key()) application_key_t;

            friend OwnerRange;
            UnionAllRangeIterator(std::shared_ptr<const OwnerRange> owner_range,
                typename OwnerRange::left_iterator && left,
                typename OwnerRange::right_iterator && right)
                : _owner_range(owner_range)
                , _left(std::move(left))
                , _right(std::move(right))
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
            value_type operator* () const
            {
                return _left_less ? *_left : *_right;
            }
            application_key_t key() const
            {
                return _left_less ? _left.key() : _right.key();
            }

        private:
            
            std::shared_ptr<const OwnerRange> _owner_range;
            bool _left_less = false;
            typename OwnerRange::left_iterator _left;
            typename OwnerRange::right_iterator _right;
        };

        template <class SourceRange1, class SourceRange2>
        struct UnionAllRange : public SuffixRange<
            UnionAllRangeIterator< UnionAllRange<SourceRange1, SourceRange2> > >
        {
            typedef UnionAllRange<SourceRange1, SourceRange2> this_t;
            typedef typename SourceRange1::iterator left_iterator;
            typedef typename SourceRange2::iterator right_iterator;
            static_assert(
                std::is_convertible<typename right_iterator::key_type, typename left_iterator::key_type>::value
                && std::is_convertible<typename right_iterator::value_type, typename left_iterator::value_type>::value
                , "Right iterator of merge must have convertible key/value type to left iterator");

            typedef std::function<int(const left_iterator&, const right_iterator&)> iterator_comparator_t;
            typedef UnionAllRangeIterator<this_t> iterator;
            /**
            * @param iterator_comparator - binary predicate `int(const iterator&, const iterator&)` that implements 'less' compare of current iterator positions
            */

            UnionAllRange(std::shared_ptr<const SourceRange1> r1, std::shared_ptr<const SourceRange2> r2, iterator_comparator_t && iterator_comparator)
                : _left(r1)
                , _right(r2)
                , _iterator_comparator(std::forward<iterator_comparator_t>(iterator_comparator))
            {
            }
            UnionAllRange() = delete;
            iterator begin() const override
            {
                iterator result(std::static_pointer_cast<const this_t>(shared_from_this()), _left->begin(), _right->begin());
                seek(result);
                return result;
            }
            bool in_range(const iterator& check) const override
            {
                return _left->in_range(check._left) || _right->in_range(check._right);
            }
            void next(iterator& pos) const override
            {
                pos._left_less ? _left->next(pos._left):_right->next(pos._right);
                seek(pos);
            }
        private:
            void seek(iterator &pos) const
            {
                bool good_left = _left->in_range(pos._left), good_right = _right->in_range(pos._right); 
                if(good_right || good_left)
                {
                    auto diff = (good_right && good_left) 
                        ? _iterator_comparator(pos._left, pos._right)
                        : (good_right ? 1 : -1);
                    pos._left_less = diff <= 0;
                }
            }
            std::shared_ptr<const SourceRange1> _left;
            std::shared_ptr<const SourceRange2> _right;
            const iterator_comparator_t _iterator_comparator;
        };

    } //ns: ranges
} //ns: OP
#endif //_OP_RANGES_UNION_ALL_RANGE__H_

