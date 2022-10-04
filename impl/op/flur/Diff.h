#pragma once
#ifndef _OP_FLUR_DIFF__H_
#define _OP_FLUR_DIFF__H_

#include <functional>
#include <memory>
#include <future>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Comparison.h>
#include <op/flur/Distinct.h>
#include <op/flur/Ingredients.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    template <class Src, class TSubtrahend, class TComp>
    struct OrderedOrderedPolicy
    {
        using comparison_t = typename TComp::comparison_t;

        constexpr OrderedOrderedPolicy(TSubtrahend&& sub, TComp cmp = TComp{}) noexcept
            : _subtrahend(std::move(sub))
            , _cmp(cmp.compare_factory())
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
                //else just follow regular next
            } 
            //emulate lover_bound by sequential iteration
            do
            {
                _subtrahend.next();
            } while (_subtrahend.in_range() 
                && (_cmp(_subtrahend.current(), other_key) < 0));
        }

        mutable TSubtrahend _subtrahend;
        comparison_t _cmp;
    };

    template <class Src, class TSubtrahend, class TComp >
    struct UnorderedDiffPolicy 
    {
        static auto create(TSubtrahend&& sub, TComp comp = TComp{})
        {
            return UnorderedDiffPolicy(
                std::async(std::launch::async, 
                    drain, std::move(sub), std::move(comp)));
        }
        
        bool operator()(PipelineAttrs& attrs, const Src& seq) const
        {
            if (attrs._step.current() == 0)
            {// first entry to sequence
                if (attrs._generation.current() == 0)
                {//not used yet at all
                    _subtrahend = std::move(_subtrahend_future.get());
                }
            }
            auto found = _subtrahend.find(seq.current());
            if (found == _subtrahend.end())
                return true;
            //use generation index to evaliuate if duplication is allowed
            return found->second.deduction(attrs._generation.current());
        }

    private:
        struct DupCount
        {
            size_t _max = 0;
            size_t _current;
            /** increase current allowed number of duplicates.
            * @return true if duplicates is allowed, false to skip
            */
            bool deduction(size_t current_gen)
            {
                assert(_max > 0);
                if (_current < (_max * (current_gen + 1)))
                {
                    if (_current < _max * current_gen)
                    {
                        _current = _max * current_gen;
                    }
                    ++_current;
                    return false;
                }
                return true;
            }
        };
        using sub_conatiner_t = details::sequence_type_t<
            details::dereference_t<TSubtrahend>>;
        using sub_element_t = std::decay_t<typename sub_conatiner_t::element_t>;
        using hash_t = std::hash<sub_element_t>;
        using eq_t = typename TComp::equals_t;

        using presence_map_t = std::unordered_map<sub_element_t, DupCount,
            hash_t, eq_t>;

        static auto drain(TSubtrahend&& sub, TComp comp)
        {
            presence_map_t result(1 << 6,
                hash_t{}, comp.equals_factory());
            using pair_t = typename presence_map_t::value_type;
            auto&& seq = OP::flur::details::get_reference(sub);
            for (seq.start(); seq.in_range(); seq.next())
            {
                auto point = result.emplace(seq.current(), DupCount{}).first;
                ++point->second._max;
            }
            return result;
        }

        UnorderedDiffPolicy(std::future<presence_map_t> diff_set)
            : _subtrahend_future(std::move(diff_set))
        {
        }

        mutable std::future<presence_map_t> _subtrahend_future;
        mutable presence_map_t _subtrahend;
    };

    template <bool is_ordered_c, class TSubtrahend, class TComp = CompareTraits>
    struct PolicyFactory
    {
        using comparator_t = std::decay_t <TComp>;

        using sub_sequence_t = details::sequence_type_t<details::dereference_t<TSubtrahend>>;;

        template <class Src>
        using policy_t = std::conditional_t<is_ordered_c,
            OrderedOrderedPolicy<Src, sub_sequence_t, comparator_t>,
            UnorderedDiffPolicy<Src, sub_sequence_t, comparator_t>
            >;

        constexpr PolicyFactory(
            TSubtrahend&& sub, comparator_t cmp = comparator_t{}
        ) noexcept
            : _sub(std::move(sub))
            , _compare_traits(std::move(cmp))
        {
        }

        template <class Src>
        constexpr auto construct() const noexcept
        {
            if constexpr(is_ordered_c)
            {
                return policy_t<std::decay_t<Src>>(
                    _sub.compound(),
                    _compare_traits);
            }
            else
            {
                return policy_t<std::decay_t<Src>>
                    :: create(_sub.compound(), _compare_traits);
            }
        }

    private:
        TSubtrahend _sub;
        comparator_t _compare_traits;
    };

    template <class TPolicyFactory>
    struct DiffFactory : FactoryBase
    {
        constexpr DiffFactory(TPolicyFactory policy_factory) noexcept
            : _policy_factory(std::move(policy_factory))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using decayed_src_t = std::decay_t<Src>;
            auto policy = _policy_factory.template construct<decayed_src_t>();
            return Distinct<decltype(policy), decayed_src_t>(
                std::forward<Src>(src),
                std::move(policy)
                );
                
        }

        TPolicyFactory _policy_factory;
    };

    
} //ns: OP::flur

#endif //_OP_FLUR_DIFF__H_
