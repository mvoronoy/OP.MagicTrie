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

        /** Range provide joining of 2 other ordered ranges. Result may potentially contains dupplicates, so review
        * possibility to apply `OrderedRange::distinct` if needed.
        * Please note: specific of implmentation always takes items only from SourceRange1, so if you join following 2 ranges:
        * \code
        * {{'a', 1}, {'b', 2}}
        * {{'a', 101}, {'c', 301}}
        * \endcode
        * join will produce exactly: {{'a', 1}}
        */
        template <class SourceRange1, class SourceRange2, bool implement_exists_c = false>
        struct JoinRange : 
            public OrderedRange< typename SourceRange1::key_t, typename SourceRange1::value_t >
        {
            static_assert(SourceRange1::is_ordered_c, "Source range 1 must support ordering");
            static_assert(SourceRange2::is_ordered_c, "Source range 2 must support ordering");
            using this_t = JoinRange<SourceRange1, SourceRange2, implement_exists_c> ;
            using base_t = OrderedRange< typename SourceRange1::key_t, typename SourceRange1::value_t >;
            using key_comparator_t = typename base_t::key_comparator_t;
            using iterator = typename base_t::iterator;
            using key_t = typename base_t::key_t;
            using value_t = typename base_t::value_t;
            using optimized_order_t = OrderedRangeOptimizedJoin<key_t, value_t>;

            using left_iterator = typename SourceRange1::iterator ;
            using right_iterator = typename SourceRange2::iterator ;

            /**
            * @param join_comparator - binary functor `int(const iterator&, const iterator&)` that implements tripple compare result  
            *    of a key in the current iterator positions (< 0, == 0, > 0). Don't mix it with OrderedRange::key_comp(). It can be 
            *    the same, but join operation may be used for relaxed comparison. For example:
            *    - Source1: {'aa', 'ab'}
            *    - Source2: {'a'}
            *    Then following join_comparator allows create {'aa', 'ab'} as join result:
            * \code
            * [](const auto& i1, const auto& i2){
            *    //compare 1 letter only
            *    return static_cast<int>(i1[0]) - static_cast<int>(i2[0])
            *  }
            * \encode
            * This trick can be used to produce join by prefix part only of some long tailed set (remember only items from 
            * SourceRange1 are in result).
            */
            JoinRange(
                std::shared_ptr<SourceRange1 const> r1, 
                std::shared_ptr<SourceRange2 const> r2,
                key_comparator_t join_comparator) noexcept
                : base_t(r1->key_comp())
                , _left(std::move(r1))
                , _right(std::move(r2))
                , _left_optimized( dynamic_cast<const optimized_order_t*>(_left.get()) )
                , _right_optimized( dynamic_cast<const optimized_order_t*>(_right.get()) )
                , _seek_left( _left_optimized ? &this_t::left_next_optimized : &this_t::left_next)
                , _seek_right( _right_optimized ? &this_t::right_next_optimized : &this_t::right_next)
                , _join_key_cmp(std::move(join_comparator))
            {
            }

            /** Create join of two ranges with default key-comparison provided by `SourceRange1::key_comp()` */
            JoinRange(
                std::shared_ptr<SourceRange1 const> r1, 
                std::shared_ptr<SourceRange2 const> r2) noexcept
                : this_t(std::move(r1), std::move(r2), r1->key_comp())
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
                /** special case when right == left, then ::next must be called for both iterators (not only for left)*/
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
            /** Seek next position when left & right iterators satisfy citeria `_join_key_comp(...) == 0`
            */ 
            virtual void seek(IteratorPayload &pload) const
            {
                pload._optimize_right_forward = false;
                bool left_succeed = _left->in_range(pload._left);
                bool right_succeed = _right->in_range(pload._right);
                while (left_succeed && right_succeed)
                {
                    auto diff = _join_key_cmp(pload._left.key(), pload._right.key());
                    if (diff < 0) 
                    {
                        (this->*_seek_left)(pload._left, pload._right);
                        left_succeed = _left->in_range(pload._left);
                    }
                    else {
                        if (diff == 0) 
                        {
                            if constexpr (!implement_exists_c) 
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

            key_comparator_t _join_key_cmp;
        };

    } //ns: ranges
} //ns: OP
#endif //_OP_RANGES_JOIN_RANGE__H_
