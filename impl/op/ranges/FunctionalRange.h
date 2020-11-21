#ifndef _OP_RANGES_FUNCTIONAL_RANGE__H_
#define _OP_RANGES_FUNCTIONAL_RANGE__H_
#include <iterator>
#include <op/ranges/PrefixRange.h>
namespace OP
{
    namespace ranges
    {

        /**Declares the policy what to do if function applied twice to the same origin iterator position.
        * This iterator caches result in internal storage. \see FunctionResultNoCachePolicy
        * \tparam Rk - result target kay after apply mapping function
        */
        template <class OriginIterator, class Rk>
        struct FunctionResultCachedPolicy
        {
            typedef FunctionResultCachedPolicy<OriginIterator, Rk> this_t;
            
            using key_t = Rk;
            using cached_key_t = const key_t&;
            using applicator_t = std::function<key_t(const OriginIterator&)>;

            FunctionResultCachedPolicy(applicator_t&& transform)
                :_transform(std::move(transform))
                {}

            void on_after_change(const OriginIterator& pos)
            {
                _dirty = true;
            }
            const key_t& get(const OriginIterator& pos) 
            {
                if(_dirty)
                {
                    _cached = std::move(_transform(pos));
                    _dirty = false;
                }
                return _cached;
            }
        private:
            bool _dirty = true;
            key_t _cached;
            applicator_t _transform;
        };

        /**Declares the policy what to do if function applied twice to the same origin iterator position.
        * This iterator always re-invoke function to evaluate new result. \see FunctionResultCachedPolicy
        * \tparam Rk - result target kay after apply mapping function
        */
        template <class OriginIterator, class Rk>
        struct FunctionResultNoCachePolicy
        {
            typedef FunctionResultNoCachePolicy<OriginIterator, Rk> this_t;
            using key_t = Rk;
            using cached_key_t = key_t;
            using applicator_t = std::function<const key_t&(const OriginIterator&)>;

            FunctionResultNoCachePolicy(applicator_t&& transform)
                :_transform(std::move(transform))
                {}

            void on_after_change(const OriginIterator& pos)
            {
                //do nothing
            }
            cached_key_t&& get(const OriginIterator& pos)
            {
                return std::move(_transform(pos));
            }
        private:
            applicator_t _transform;
        };

        template <class SourceIterator, class OwnerRange, class KeyEvalPolicy>
        struct FunctionalRangeIterator :
            public RangeIterator<typename KeyEvalPolicy::key_t, typename SourceIterator::value_t>::RangeIteratorImpl
        {
            using base_t = typename RangeIterator<typename KeyEvalPolicy::key_t, typename SourceIterator::value_t>::RangeIteratorImpl;
            using source_iterator_t = SourceIterator;
            using key_eval_policy_t = KeyEvalPolicy;
            using key_t = typename KeyEvalPolicy::key_t;
            using value_t = typename SourceIterator::value_t;
            using cached_key_t = typename key_eval_policy_t::cached_key_t;

            using this_t = FunctionalRangeIterator<SourceIterator, OwnerRange, KeyEvalPolicy>;

            friend OwnerRange;
            
            FunctionalRangeIterator(source_iterator_t source, key_eval_policy_t && key_eval) noexcept
                : _source(source)
                , _key_eval_policy(std::move(key_eval))
            {}

            const value_t& value() const
            {
                return _source.value();
            }
            std::unique_ptr<base_t> clone() const
            {
                auto policy_copy = _key_eval_policy;
                return std::unique_ptr<base_t>(
                    new this_t(_source, std::move(policy_copy))
                    );
            }

            const key_t& key() const
            {
                return _key_eval_policy.get(_source);
            }
            source_iterator_t& source()
            {
                return _source;
            }
            const source_iterator_t& source() const
            {
                return _source;
            }
            key_eval_policy_t& key_eval_policy() const
            {
                return _key_eval_policy;
            }
        private:
            source_iterator_t _source;
            mutable key_eval_policy_t _key_eval_policy; //need mutable since policy::get cannot be const
        };

        template <class SourceRange, class Rk,
            class KeyEvalPolicy = FunctionResultCachedPolicy<typename SourceRange::iterator, Rk>,
            class Base = RangeBase<Rk, typename SourceRange::value_t> >
        struct FunctionalRange : public Base
        {
            using this_t = FunctionalRange<SourceRange, Rk, KeyEvalPolicy, Base >;
            using base_t = Base;
            using iterator = typename base_t::iterator;
            using range_t = typename base_t::range_t;
            using applicator_t = typename KeyEvalPolicy::applicator_t;

            using iterator_impl = FunctionalRangeIterator<
                typename SourceRange::iterator,
                this_t,
                KeyEvalPolicy
            >;

            using key_eval_policy_t = KeyEvalPolicy;
            using key_t = Rk;

            template <typename... Ts>
            FunctionalRange(std::shared_ptr<const SourceRange> source, applicator_t transform, Ts&& ...other) noexcept
                : Base(std::forward<Ts>(other)...)
                , _source_range(std::move(source))
                , _key_eval_policy(std::move(transform))
            {
            }

            iterator begin() const override
            {
                if(!_source_range)
                    return this->end();
                auto res = _source_range->begin();
                if (_source_range->in_range(res))
                {
                    auto policy_copy = _key_eval_policy; //clone
                    policy_copy.on_after_change(res); //notify local policy copy that key was changed
                    return iterator(
                        std::const_pointer_cast<range_t const>(this->shared_from_this()),
                        std::unique_ptr<typename iterator::impl_t>(
                            new iterator_impl(std::move(res), std::move(policy_copy))));
                }
                return this->end();
            }
            bool in_range(const iterator& check) const override
            {
                if( !check || !_source_range )
                    return false;
                return _source_range->in_range(check.OP_TEMPL_METH(impl)<iterator_impl>().source());
            }
            void next(iterator& pos) const override
            {
                if(!pos)
                    return;
                auto& impl = pos.OP_TEMPL_METH(impl)<iterator_impl>();
                _source_range->next(impl.source());

                if (_source_range->in_range(impl.source()))
                {//notify policy that key was changed
                    impl.key_eval_policy().on_after_change(impl.source());
                }
            }
        protected:
            const std::shared_ptr<SourceRange const>& source_range() const
            {
                return _source_range;
            }
            const key_eval_policy_t& key_eval_policy() const
            {
                return _key_eval_policy;
            }
        private:
            std::shared_ptr<SourceRange const> _source_range;
            key_eval_policy_t _key_eval_policy;
        };
    } //ns: ranges
} //ns: OP


#endif //_OP_RANGES_FUNCTIONAL_RANGE__H_
