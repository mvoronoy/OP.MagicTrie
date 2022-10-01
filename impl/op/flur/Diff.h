#pragma once
#ifndef _OP_FLUR_DIFF__H_
#define _OP_FLUR_DIFF__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Comparison.h>
#include <op/flur/Distinct.h>
#include <op/flur/Ingredients.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    template <class Src, class TSubtrahend, class Comp = full_compare_t>
    struct OrderedOrderedPolicyFactory
    {
        constexpr OrderedOrderedPolicyFactory(TSubtrahend sub, Comp cmp = Comp{}) noexcept
            : _subtrahend(std::move(sub))
            , _cmp(std::move(cmp))
        {
            //only ordered sequences allowed in this policy
            assert(_subtrahend.is_sequence_ordered());
        }

        bool operator()(PipelineAttrs &attrs, const Src& seq) const
        {
            if( attrs._step.current() == 0)
            {// first entry to sequence
                assert(seq.is_sequence_ordered()) ; //policy applicable for both sorted only
                _subtrahend.start();    
            }
            if( !_subtrahend.in_range() )
                return true; //when subtrahend is empty the rest of Seq is valid
            //no need to check !seq.in_range()
            decltype(auto) outer_item = seq.current(); //may be a reference
            int compare_res = _cmp(_subtrahend.current(), outer_item);
            if(compare_res < 0)
            {//need catch up _subtrahend
                opt_next(outer_item); 
                if( !_subtrahend.in_range() )
                    return true;
                compare_res = _cmp(_subtrahend.current(), outer_item);
            }
            else
                _subtrahend.next();

            if( !compare_res )//exact equals
                return false;
            return true;
        }
   private:     
        constexpr static bool is_join_optimized_c = 
            details::has_lower_bound<TSubtrahend>::value;

        template <class V>
        void opt_next(const V& other_key) const
        {
            if constexpr (is_join_optimized_c)
            {
                if( _cmp(_subtrahend.current(), other_key) < 0 )
                {
                    _subtrahend.lower_bound(other_key);
                    return;
                }
                //else just follow regulatr next
            } 
            //emulate lover_bound by sequential iteration
            do
            {
                _subtrahend.next();
            } while (_subtrahend.in_range() 
                && (_cmp(_subtrahend.current(), other_key) < 0));
        }

        mutable TSubtrahend _subtrahend;
        Comp _cmp;
    };

    template <class TSubtrahend, class Comp = CompareTraits>
    struct DiffFactory : FactoryBase
    {
        using decayed_sub_t = std::decay_t<TSubtrahend>;
        using sub_sequence_t = details::sequence_type_t<details::dereference_t <decayed_sub_t>>;
        using comparator_t = std::decay_t <Comp>;

        constexpr DiffFactory(decayed_sub_t sub, comparator_t cmp = comparator_t{}) noexcept
            : _sub(std::move(sub))
            , _compare_traits(std::move(cmp))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_conatiner_t = details::sequence_type_t<details::dereference_t<Src>>;
            using base_t = Sequence<typename src_conatiner_t::element_t>;

            using three_way_compare_t = typename comparator_t::comparison_t;
            using effective_policy_t = OrderedOrderedPolicyFactory<
                Src,
                sub_sequence_t,
                three_way_compare_t>;

            effective_policy_t policy(
                std::move(_sub.compound()), 
                _compare_traits.compare_factory());
            return Distinct<effective_policy_t, Src>(
                std::forward<Src>(src), 
                std::move(policy)
                );
        }

        decayed_sub_t _sub;
        comparator_t _compare_traits;
    };

} //ns: OP::flur

#endif //_OP_FLUR_DIFF__H_
