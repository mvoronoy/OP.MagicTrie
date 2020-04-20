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
        */
        template <class OriginIterator, class UnaryFunction>
        struct FunctionResultCachedPolicy
        {
            typedef FunctionResultCachedPolicy<OriginIterator, UnaryFunction> this_t;
            
            using key_t = OP::utils::return_type_t<UnaryFunction>;
            using cached_key_t = const key_t&;
            using applicator_t = std::function<key_t(const OriginIterator&)>;
            using applicator_result_t = typename applicator_t::result_type;

            FunctionResultCachedPolicy(applicator_t transform)
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
            //typedef typename std::result_of<decltype(&this_t::get)(this_t, const OriginIterator&, const UnaryFunction&)>::type
            //    applicator_result_t;
        private:
            bool _dirty = true;
            key_t _cached;
            applicator_t _transform;
        };

        /**Declares the policy what to do if function applied twice to the same origin iterator position.
        * This iterator always re-invoke function to evaluate new result. \see FunctionResultCachedPolicy
        */
        template <class OriginIterator, class UnaryFunction>
        struct FunctionResultNoCachePolicy
        {
            typedef FunctionResultNoCachePolicy<OriginIterator, UnaryFunction> this_t;
            using key_t = OP::utils::return_type_t<UnaryFunction>;
            using cached_key_t = key_t;
            //using applicator_t = std::function<const key_t&(const OriginIterator&)>;
            FunctionResultNoCachePolicy(UnaryFunction transform)
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
            UnaryFunction _transform;
        };

        template <class SourceIterator, class OwnerRange, class KeyEvalPolicy>
        struct FunctionalRangeIterator :
            public OwnerRange::iterator::RangeIteratorImpl
        {
            using base_t = typename OwnerRange::iterator::RangeIteratorImpl;
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

        template <class SourceRange, class UnaryFunction,
            class KeyEvalPolicy = FunctionResultCachedPolicy<typename SourceRange::iterator, UnaryFunction>,
            class Base = RangeBase<typename KeyEvalPolicy::key_t, typename SourceRange::value_t> >
        struct FunctionalRange : public Base
        {
            using this_t = FunctionalRange<SourceRange, UnaryFunction, KeyEvalPolicy, Base >;
            using base_t = Base;
            using iterator = typename base_t::iterator;

            using iterator_impl = FunctionalRangeIterator<
                typename SourceRange::iterator,
                this_t,
                KeyEvalPolicy
            >;

            using key_eval_policy_t = KeyEvalPolicy;
            using key_t = typename key_eval_policy_t::key_t;

            template <typename... Ts>
            FunctionalRange(std::shared_ptr<const SourceRange> source, UnaryFunction transform, Ts&& ...other) noexcept
                : Base(std::forward<Ts>(other)...)
                , _source_range(std::move(source))
                , _key_eval_policy(std::move(transform))
            {
            }

            iterator begin() const override
            {
                auto res = _source_range->begin();
                if (_source_range->in_range(res))
                {
                    auto policy_copy = _key_eval_policy; //clone
                    policy_copy.on_after_change(res); //notify local policy copy that key was changed
                    return iterator(
                        std::const_pointer_cast<range_t const>(shared_from_this()),
                        std::unique_ptr<iterator::RangeIteratorImpl>(
                            new iterator_impl(std::move(res), std::move(policy_copy))));
                }
                return end();
            }
            bool in_range(const iterator& check) const override
            {
                if( !check )
                    return false;
                return _source_range->in_range(check.impl<iterator_impl>().source());
            }
            void next(iterator& pos) const override
            {
                if(!pos)
                    return;
                auto& impl = pos.impl<iterator_impl>();
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
