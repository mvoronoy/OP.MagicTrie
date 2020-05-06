#ifndef _OP_RANGES_JOIN_RANGE__H_
#define _OP_RANGES_JOIN_RANGE__H_
#include <iterator>
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>

#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

namespace OP
{
    namespace ranges
    {

        template <class SourceRange1, class SourceRange2>
        struct JoinRange : 
            public OrderedRange< typename SourceRange1::key_t, typename SourceRange1::value_t >
        {
            using this_t = JoinRange<SourceRange1, SourceRange2> ;
            using base_t = OrderedRange< typename SourceRange1::key_t, typename SourceRange1::value_t >;
            using key_comparator_t = typename base_t::key_comparator_t;
            using iterator = typename base_t::iterator;
            using key_t = typename base_t::key_t;
            using value_t = typename base_t::value_t;
            using optimized_order_t = OrderedRangeOptimizedJoin<key_t, value_t>;

            using left_iterator = typename SourceRange1::iterator ;
            using right_iterator = typename SourceRange2::iterator ;

            /**
            * @param comparator - binary predicate `int(const iterator&, const iterator&)` that implements tripple compare of current 
            *    iterator positions (< 0, == 0, > 0)
            */
            JoinRange(
                std::shared_ptr<SourceRange1 const> r1, 
                std::shared_ptr<SourceRange2 const> r2,
                key_comparator_t comparator) noexcept
                : base_t(std::move(comparator))
                , _left(std::move(r1))
                , _right(std::move(r2))
                , _left_optimized( dynamic_cast<const optimized_order_t*>(_left.get()) )
                , _right_optimized( dynamic_cast<const optimized_order_t*>(_right.get()) )
                , _seek_left( _left_optimized ? &this_t::left_next_optimized : &this_t::left_next)
                , _seek_right( _right_optimized ? &this_t::right_next_optimized : &this_t::right_next)
            {
            }

            JoinRange() = delete;

            iterator begin() const override
            {
                auto left_i = _left->begin();
                auto right_i = _left->in_range(left_i) ? _right->lower_bound(left_i.key()) : _right->end();
                auto result = std::make_unique<IteratorPayload>(
                    std::move(left_i),
                    std::move(right_i)
                );
                seek(*result);
                return iterator(shared_from_this(), std::move(result));
            }
            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                const auto& pload = check.impl<IteratorPayload>();

                return _left->in_range(pload._left) && _right->in_range(pload._right);
            }
            void next(iterator& pos) const override
            {
                if(!pos)
                    return;
                auto& pload = pos.impl<IteratorPayload>();
                _left->next(pload._left);
                if (pload._optimize_right_forward)
                {
                    _right->next(pload._right);
                }
                seek(pload);
            }
            iterator lower_bound(const typename base_t::key_t& key) const override
            {
                auto left_i = _left->lower_bound(key);
                auto right_i = _left->in_range(left_i) ? _right->lower_bound(left_i.key()) : _right->end();
                auto result = std::make_unique<IteratorPayload>(
                    std::move(left_i),
                    std::move(right_i)
                );
                seek(*result);
                return iterator(shared_from_this(), std::move(result));
            }

        private:
            struct IteratorPayload : public iterator::impl_t
            {
                IteratorPayload(
                  left_iterator && left,
                  right_iterator && right)
                    :  _left(left)
                    , _right(right)
                    , _optimize_right_forward(false)
                {}
                const key_t& key() const override
                {
                    return _left.key();
                }
                const value_t& value() const override
                {
                    return _left.value();
                }
                std::unique_ptr<typename iterator::impl_t> clone() const override
                {
                    return std::unique_ptr<RangeIteratorImpl>{ new IteratorPayload(*this) };
                }

                left_iterator _left;
                right_iterator _right;
                /**Very special case when right == left, then ::next must be called for both iterators (not only for left)*/
                bool _optimize_right_forward;
            };
            /** If optimization possible (`_left` inherited from OrderedRangeOptimizedJoin) this method applied in
            * `seek` instead of regular next for `left` iterator
            */
            void left_next_optimized(left_iterator& left, right_iterator& right) const
            {
                _left_optimized->next_lower_bound_of(left, right.key());
            }
            /** If optimization possible (`_right` inherited from OrderedRangeOptimizedJoin) this method applied in
            * `seek` instead of regular next for `right` iterator
            */
            void right_next_optimized(left_iterator& left, right_iterator& right) const
            {
                _right_optimized->next_lower_bound_of(right, left.key());
            }
            /** Method is dummy when join-optimization is not possible (`_left` is not a OrderedRangeOptimizedJoin).
            */
            void left_next(left_iterator& left, right_iterator& ) const
            {
                _left->next(left);
            }
            /** Method is dummy when join-optimization is not possible (`_right` is not a OrderedRangeOptimizedJoin).
            */
            void right_next(left_iterator& , right_iterator& right) const
            {
                _right->next(right);
            }
            /** Seek next position when left & right iterators satisfy citeria `key_comp()(...) == 0`
            */ 
            virtual void seek(IteratorPayload &pload) const
            {
                pload._optimize_right_forward = false;
                bool left_succeed = _left->in_range(pload._left);
                bool right_succeed = _right->in_range(pload._right);
                while (left_succeed && right_succeed)
                {
                    auto diff = key_comp()(pload._left.key(), pload._right.key());
                    if (diff < 0) 
                    {
                        (this->*_seek_left)(pload._left, pload._right);
                        left_succeed = _left->in_range(pload._left);
                    }
                    else {
                        if (diff == 0) 
                        {
                            pload._optimize_right_forward = true;
                            return;
                        }
                        (this->*_seek_right)(pload._left, pload._right);
                        right_succeed = _right->in_range(pload._right);
                    }
                }
            }
            std::shared_ptr<SourceRange1 const> _left;
            const OrderedRangeOptimizedJoin<key_t, value_t>* _left_optimized;

            std::shared_ptr<SourceRange2 const> _right;
            const OrderedRangeOptimizedJoin<key_t, value_t>* _right_optimized;

            void(this_t::* _seek_left)(left_iterator&, right_iterator&) const;
            void(this_t::* _seek_right)(left_iterator&, right_iterator&) const;

        };

    } //ns: ranges
} //ns: OP
#endif //_OP_RANGES_JOIN_RANGE__H_
