#ifndef _OP_RANGES_FUNCTIONAL_RANGE__H_
#define _OP_RANGES_FUNCTIONAL_RANGE__H_
#include <iterator>
#include <op/ranges/PrefixRange.h>
namespace OP
{
    namespace ranges
    {
        template <class Iterator, bool is_ordered>
        struct PrefixRange;

        
        /**Declares the policy what to do if function applied twice to the same origin iterator position.
        * This iterator caches result in internal storage. \see FunctionResultNoCachePolicy
        */
        template <class OriginIterator, class UnaryFunction>
        struct FunctionResultCachedPolicy
        {
            typedef FunctionResultCachedPolicy<OriginIterator, UnaryFunction> this_t;
            typedef typename std::result_of<UnaryFunction(const OriginIterator&)>::type key_t;
            typedef const key_t& applicator_result_t;
            //typedef decltype(UnaryFunction(const OriginIterator&)) applicator_result_t;
            //typedef typename std::decay<applicator_result_t>::type key_t;
            
            void on_after_change(OriginIterator& pos, const UnaryFunction& transform)
            {
                _cached = transform(pos);
            }
            applicator_result_t get(const OriginIterator& /*ignored*/, const UnaryFunction& /*ignored*/) const
            {
                return _cached;
            }
            //typedef typename std::result_of<decltype(&this_t::get)(this_t, const OriginIterator&, const UnaryFunction&)>::type
            //    applicator_result_t;
        private:
            key_t _cached;
        };

        /**Declares the policy what to do if function applied twice to the same origin iterator position.
        * This iterator always re-invoke function to evaluate new result. \see FunctionResultCachedPolicy
        */
        template <class OriginIterator, class UnaryFunction>
        struct FunctionResultNoCachePolicy
        {
            typedef FunctionResultNoCachePolicy<OriginIterator, UnaryFunction> this_t;
            //typedef typename std::result_of<UnaryFunction(const OriginIterator&)>::type key_t;
            typedef typename std::result_of<UnaryFunction(const OriginIterator&)>::type applicator_result_t;

            void on_after_change(OriginIterator& pos, const UnaryFunction& transform)
            {
                //do nothing
            }
            applicator_result_t get(const OriginIterator& pos, const UnaryFunction& transform) const
            {
                return transform(pos);
            }
        };

        template <class SourceIterator, class OwnerRange>
        struct FunctionalRangeIterator :
            public std::iterator<std::forward_iterator_tag, typename SourceIterator::value_type>
        {
            typedef SourceIterator source_iterator_t;
            typedef FunctionalRangeIterator<SourceIterator, OwnerRange> this_t;
            typedef typename OwnerRange::key_eval_policy_t key_eval_policy_t;
            typedef typename key_eval_policy_t::applicator_result_t key_type;
            typedef typename SourceIterator::value_type value_type;

            typedef typename key_eval_policy_t::applicator_result_t applicator_result_t;
            friend OwnerRange;
            FunctionalRangeIterator(const OwnerRange& owner_range, source_iterator_t source, key_eval_policy_t && key_eval) noexcept
                : _owner_range(owner_range)
                , _source(source)
                , _key_eval_policy(std::forward<key_eval_policy_t>(key_eval))
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
                return *_source;
            }
            applicator_result_t key() const
            {
                return _key_eval_policy.get(_source, _owner_range.transform());
            }

        private:
            const OwnerRange& _owner_range;
            source_iterator_t _source;
            key_eval_policy_t _key_eval_policy;
        };

        template <class SourceRange, class UnaryFunction, 
            class KeyEvalPolicy = FunctionResultNoCachePolicy<typename SourceRange::iterator, UnaryFunction>, bool is_ordered = false  >
        struct FunctionalRange : public PrefixRange<
            FunctionalRangeIterator<
                typename SourceRange::iterator, 
                FunctionalRange<SourceRange, UnaryFunction, KeyEvalPolicy>
                >, is_ordered >
        {
            typedef FunctionalRange<SourceRange, UnaryFunction, KeyEvalPolicy, is_ordered> this_t;
            typedef FunctionalRangeIterator<
                typename SourceRange::iterator,
                typename this_t//FunctionalRange<SourceRange, UnaryFunction, KeyEvalPolicy>
            > iterator;
            typedef KeyEvalPolicy key_eval_policy_t;

            FunctionalRange(const SourceRange & source, UnaryFunction && transform) noexcept
                : _source_range(source)
                , _transform(std::forward<UnaryFunction >(transform))
            {
            }

            iterator begin() const override
            {
                iterator res(*this, _source_range.begin(), key_eval_policy_t());
                if (in_range(res))
                {//notify policy that key was changed
                    res._key_eval_policy.on_after_change(res._source, _transform);
                }
                return res;
            }
            bool in_range(const iterator& check) const override
            {
                return _source_range.in_range(check._source);
            }
            void next(iterator& pos) const override
            {
                _source_range.next(pos._source);
                
                if (in_range(pos))
                {//notify policy that key was changed
                    pos._key_eval_policy.on_after_change(pos._source, _transform);
                }
            }
            const UnaryFunction& transform() const
            {
                return _transform;
            }
        private:
            
            const SourceRange& _source_range;
            UnaryFunction _transform;
        };
    } //ns: ranges
} //ns: OP


#endif //_OP_RANGES_FUNCTIONAL_RANGE__H_
