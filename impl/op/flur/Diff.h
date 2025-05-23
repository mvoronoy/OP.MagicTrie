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
#include <op/flur/SequenceState.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    template <class Src, class TSubtrahendSequence, class TComp>
    struct OrderedOrderedPolicy
    {
        using comparison_t = typename TComp::comparison_t;

        explicit constexpr OrderedOrderedPolicy(TSubtrahendSequence&& sub, TComp cmp = TComp{}) noexcept
            : _subtrahend(std::move(sub))
            , _cmp(cmp.compare_factory())
        {
            //only ordered sequences are allowed in this policy
            assert(details::get_reference(_subtrahend).is_sequence_ordered());
        }

        bool operator()(SequenceState &attrs, const Src& seq) const
        {
            auto& seq_ref = details::get_reference(seq);
            auto& sub_ref = details::get_reference(_subtrahend);
            if( attrs.step() == 0)
            {// first entry to sequence
                assert(seq_ref.is_sequence_ordered()); //policy applicable for both sorted only
                sub_ref.start();
            }
            if( !sub_ref.in_range() )
                return true; //when subtrahend is empty the rest of Seq is valid
            //no need to check !seq.in_range()
            decltype(auto) outer_item = seq_ref.current(); //may return a reference
            int compare_res = _cmp(sub_ref.current(), outer_item);
            if(compare_res < 0)
            {//need catch up _subtrahend
                opt_next(outer_item); 
                if( !sub_ref.in_range() )
                    return true;
                compare_res = _cmp(sub_ref.current(), outer_item);
            }
            if (!compare_res) //exact equals
            {
                sub_ref.next();
                return false;
            }
            return true;
        }

   private:
        constexpr static bool is_join_optimized_c = 
            details::has_lower_bound<TSubtrahendSequence>::value;

        template <class V>
        void opt_next(const V& other_key) const
        {
            auto& sub_ref = details::get_reference(_subtrahend);
            if constexpr (is_join_optimized_c)
            {
                if( _cmp(sub_ref.current(), other_key) < 0 )
                {
                    sub_ref.lower_bound(other_key);
                    return;
                }
                //else just follow regular next
            } 
            //emulate lover_bound by sequential iteration
            do
            {
                sub_ref.next();
            } while (sub_ref.in_range()
                && (_cmp(sub_ref.current(), other_key) < 0));
        }

        mutable TSubtrahendSequence _subtrahend;
        comparison_t _cmp;
    };

    template <class Src, class TSubtrahendSequence, class TComp >
    struct UnorderedDiffPolicy 
    {
        static auto create(TSubtrahendSequence&& sub, TComp comp = TComp{})
        {
            return UnorderedDiffPolicy(
                std::async(std::launch::async, 
                    drain, std::move(sub), std::move(comp)));
        }
        
        bool operator()(const SequenceState& attrs, const Src& seq) const
        {
            if (attrs.step() == 0)
            {// first entry to sequence
                if (attrs.generation() == 0)
                {//not used yet at all
                    _subtrahend = std::move(_subtrahend_future.get());
                }
            }
            auto found = _subtrahend.find(details::get_reference(seq).current());
            if (found == _subtrahend.end())
                return true;
            //use generation index to evaluate if duplication is allowed
            return found->second.deduction(attrs.generation().current());
        }

    private:
        struct DupCount
        {
            size_t _max = 0;
            size_t _current;
            /** increase current number of allowed duplicates.
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
            details::dereference_t<TSubtrahendSequence>>;
        using sub_element_t = std::decay_t<typename sub_conatiner_t::element_t>;
        constexpr static auto hash_factory(TComp& comp)
        {
            if constexpr (has_hash_factory<TComp>::value)
                return comp.hash_factory();
            else
                return std::hash<sub_element_t>{};
        }

        using hash_t = std::decay_t<
            decltype(hash_factory(std::declval<TComp&>())) >;
        using eq_t = typename TComp::equals_t;

        using presence_map_t = std::unordered_map<sub_element_t, DupCount,
            hash_t, eq_t>;
        static auto drain(TSubtrahendSequence&& sub, TComp comp)
        {
            presence_map_t result(1 << 6,
                hash_factory(comp), comp.equals_factory());

            using pair_t = typename presence_map_t::value_type;
            auto&& seq = OP::flur::details::get_reference(sub);
            for (seq.start(); seq.in_range(); seq.next())
            {
                auto point = result.emplace(seq.current(), DupCount{}).first;
                ++point->second._max;
            }
            return result;
        }

        explicit UnorderedDiffPolicy(std::future<presence_map_t> diff_set) noexcept
            : _subtrahend_future(std::move(diff_set))
        {
        }

        mutable std::future<presence_map_t> _subtrahend_future;
        mutable presence_map_t _subtrahend;
    };

    enum class DiffAlgorithm
    {
        ordered,
        unordered,
        automatic
    };

    template <DiffAlgorithm algorithm, class TSubtrahend, class TComparator = CompareTraits>
    class DiffFactory : FactoryBase
    {
        template <DiffAlgorithm custom_alg_c, class TMinuend, class TSub, class TCmp>
        auto static make_sequence(TMinuend&& minuend, TSub&& subtrahend, TCmp comparator)
        {
            using element_t = details::sequence_element_type_t<TMinuend>;

            // It is not a mistake, Distinct uses policy that implements Diff

            if constexpr (custom_alg_c == DiffAlgorithm::ordered)
            {
                using policy_t = 
                    OrderedOrderedPolicy<TMinuend, TSub, TCmp>;

                return DistinctSequence<element_t, policy_t, TMinuend>(
                    std::move(minuend), //always move!
                    policy_t(std::forward<TSub>(subtrahend), std::move(comparator))
                );
            }
            else if constexpr (custom_alg_c == DiffAlgorithm::unordered)
            {
                using policy_t = UnorderedDiffPolicy<TMinuend, TSub, TCmp>;

                return DistinctSequence<element_t, policy_t, TMinuend>(
                    std::move(minuend), //always move!
                    policy_t::create(std::forward<TSub>(subtrahend), std::move(comparator)));
            }
            else //automatic
            {
                using unordered_t = decltype(make_sequence<DiffAlgorithm::unordered>(
                    std::declval<TMinuend>(), std::declval<TSub>(), std::declval<TCmp>()));
                using ordered_t = decltype(make_sequence<DiffAlgorithm::ordered>(
                    std::declval<TMinuend>(), std::declval<TSub>(), std::declval<TCmp>()));
                using proxy_t = SequenceProxy<unordered_t, ordered_t>;

                if(details::get_reference(minuend).is_sequence_ordered() && 
                     details::get_reference(subtrahend).is_sequence_ordered() )
                {
                    return proxy_t{ make_sequence<DiffAlgorithm::ordered>(
                           std::move(minuend), std::move(subtrahend), std::move(comparator)) };
                }
                else
                {
                    return proxy_t{ make_sequence<DiffAlgorithm::unordered>(
                           std::move(minuend), std::move(subtrahend), std::move(comparator)) };
                }
            }
        }

    public:
        using comparator_t = std::decay_t<TComparator>;

        template <class TSub>
        explicit constexpr DiffFactory(TSub&& sub, comparator_t cmp = comparator_t{}) noexcept
            : _sub(std::forward<TSub>(sub))
            , _compare_traits(std::move(cmp))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            return make_sequence<algorithm>(
                std::move(src), 
                details::get_reference(_sub).compound(), 
                _compare_traits);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            return make_sequence<algorithm>(
                std::move(src), 
                std::move(details::get_reference(_sub)).compound(), 
                std::move(_compare_traits));
        }

    private:
        std::decay_t<TSubtrahend> _sub;
        std::decay_t<TComparator> _compare_traits;

    };

    
} //ns: OP::flur

#endif //_OP_FLUR_DIFF__H_
