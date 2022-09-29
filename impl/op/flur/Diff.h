#pragma once
#ifndef _OP_FLUR_DIFF__H_
#define _OP_FLUR_DIFF__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Join.h>
#include <op/flur/Distinct.h>
#include <op/flur/Ingredients.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    template <class TSubtrahend, class Comp = plain_less_t<TSubtrahend> >
    struct OrderedOrderedPolicyFactory
    {
        constexpr OrderedOrderedPolicyFactory(TSubtrahend sub, Comp cmp = Comp{}) noexcept
            : _subtrahend(std::move(sub))
            , _cmp(std::move(cmp))
        {
            //only ordered sequences allowed in this policy
            assert(_subtrahend.is_sequence_ordered());
        }

        bool operator()(PipelineAttrs &attrs, const TSubtrahend& seq) const
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
                && _cmp(_subtrahend.current(), other_key) < 0);
        }

        mutable TSubtrahend _subtrahend;
        Comp _cmp;
    };

    struct CompareTraits
    {
        template <class TSequence>
        using compare_t = plain_less_t<TSequence>;

        constexpr CompareTraits() noexcept = default;
        constexpr CompareTraits(CompareTraits&&) noexcept = default;

        template <class TSequence>
        constexpr auto comparator() const noexcept
        {
            return compare_t<TSequence>();
        }
    };

    template <class TSubtrahend, class Comp = CompareTraits>
    struct DiffFactory : FactoryBase
    {
        using decayed_sub_t = std::decay_t<TSubtrahend>;
        constexpr DiffFactory(decayed_sub_t sub, Comp cmp = Comp{}) noexcept
            : _sub(std::move(sub))
            , _compare_traits(std::move(cmp))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_conatiner_t = details::sequence_type_t<details::dereference_t<Src>>;
            using base_t = Sequence<typename src_conatiner_t::element_t>;

            using compare_t = typename Comp::template compare_t<src_conatiner_t>;
            using effective_policy_t = OrderedOrderedPolicyFactory<
                Src, 
                compare_t>;

            effective_policy_t policy(
                std::move(_sub.compound()), 
                _compare_traits.template comparator<src_conatiner_t>());
            return Distinct<effective_policy_t, std::decay_t<Src>>(
                std::forward<Src>(src), 
                std::move(policy));
        }

        decayed_sub_t _sub;
        Comp _compare_traits;
    };

} //ns: OP::flur

#endif //_OP_FLUR_DIFF__H_
