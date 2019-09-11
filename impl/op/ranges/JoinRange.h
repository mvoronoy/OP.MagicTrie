#ifndef _OP_RANGES_JOIN_RANGE__H_
#define _OP_RANGES_JOIN_RANGE__H_
#include <iterator>
#include <op/ranges/PrefixRange.h>

#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

namespace OP
{
    namespace ranges
    {
        template <class OwnerRange>
        struct JoinRangeIterator :
            public std::iterator<
            std::forward_iterator_tag,
            typename OwnerRange::left_iterator::value_type
            >
        {
            typedef JoinRangeIterator<OwnerRange> this_t;
            typedef typename OwnerRange::left_iterator::value_type value_type;
            typedef typename OwnerRange::left_iterator::key_type key_type;
            typedef decltype(std::declval<OwnerRange::left_iterator>().key()) application_key_t;
            //typedef typename OwnerRange::left_iterator::prefix_string_t prefix_string_t;
            
            friend OwnerRange;
            JoinRangeIterator(std::shared_ptr<const OwnerRange> owner_range, 
                typename OwnerRange::left_iterator && left, 
                typename OwnerRange::right_iterator && right) noexcept
                : _owner_range(owner_range)
                , _left(std::move(left))
                , _right(std::move(right))
                , _optimize_right_forward(false)
            {}
            this_t& operator ++()
            {
                _owner_range->next(*this);
                return *this;
            }
            this_t operator ++(int)
            {
                this_t result = *this;
                _owner_range->next(*this);
                return result;
            }
            value_type operator* () const
            {
                return *left();
            }
            application_key_t key() const
            {
                return _left.key();
            }

        private:
            const typename OwnerRange::left_iterator& left() const
            {
                return _left;
            }
            const typename OwnerRange::right_iterator& right() const
            {
                return _right;
            }
            std::shared_ptr<const OwnerRange> _owner_range;
            typename OwnerRange::left_iterator _left;
            typename OwnerRange::right_iterator _right;
            /**Very special case when right == left, then ::next must be called for both iterators (not only for left)*/
            bool _optimize_right_forward;
        };
        
        template <class SourceRange1, class SourceRange2>
        struct JoinRange : public OrderedRange<
                JoinRangeIterator< JoinRange<SourceRange1, SourceRange2> > >
        {
            typedef JoinRange<SourceRange1, SourceRange2> this_t;
            static_assert(SourceRange1::is_ordered_c, "Source range(1) must support ordering");
            static_assert(SourceRange1::is_ordered_c, "Source range(2) must support ordering");

            typedef typename SourceRange1::iterator left_iterator;
            typedef typename SourceRange2::iterator right_iterator;
            typedef std::function<int(const left_iterator&, const right_iterator&)> iterator_comparator_t;
            typedef JoinRangeIterator<this_t> iterator;
            /**
            * @param iterator_comparator - binary predicate `int(const iterator&, const iterator&)` that implements 'less' compare of current iterator positions
            */
            
            JoinRange(std::shared_ptr<const SourceRange1> r1, std::shared_ptr<const SourceRange2> r2, iterator_comparator_t && iterator_comparator) noexcept
                : _left(r1)
                , _right(r2)
                , _iterator_comparator(std::forward<iterator_comparator_t> (iterator_comparator))
            {
            }
            JoinRange() = delete;
            iterator begin() const override
            {
                iterator result(std::static_pointer_cast<const this_t>(shared_from_this()), _left->begin(), _right->begin());
                seek(result);
                return result;
            }
            bool in_range(const iterator& check) const override
            {
                return _left->in_range(check.left()) && _right->in_range(check.right());
            }
            void next(iterator& pos) const override
            {
                _left->next(pos._left);
                if (pos._optimize_right_forward)
                {
                    _right->next(pos._right);
                }
                seek(pos);
            }
            iterator lower_bound(const typename iterator::key_type& key) const override
            {
                iterator result(std::static_pointer_cast<const this_t>(shared_from_this()), 
                    _left->lower_bound(key), _right->lower_bound(key));
                seek(result);
                return result;
            }

        private:
            void seek(iterator &pos) const
            {
                pos._optimize_right_forward = false;
                while (in_range(pos))
                {
                    auto diff = _iterator_comparator(pos._left, pos._right);
                    if (diff < 0) 
                    {
                        _left->next(pos._left);
                    }
                    else {
                        if (diff == 0) 
                        {
                            pos._optimize_right_forward = true;
                            return;
                        }
                        _right->next(pos._right);
                    }
                }

            }
            std::shared_ptr<const SourceRange1> _left;
            std::shared_ptr<const SourceRange2> _right;
            const iterator_comparator_t _iterator_comparator;
        };
        
    } //ns: ranges
} //ns: OP
#endif //_OP_RANGES_JOIN_RANGE__H_
