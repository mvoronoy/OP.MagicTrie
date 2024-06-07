#pragma once
#ifndef _OP_FLUR_DISTINCT__H_
#define _OP_FLUR_DISTINCT__H_

#include <functional>
#include <memory>
#include <optional>     
#include <unordered_set>

#include <op/common/Currying.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Ingredients.h>
#include <op/flur/Proxy.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{

    /**
    *   Implement functor to apply Distinct for ordered sequences. It has minimal 
    *   complexity and memory footprint (in compare with unordered implementation
    */
    template <class Seq, class KeyEqual = std::equal_to< details::sequence_element_type_t <Seq> >>
    struct SkipOrdered
    {
        using decay_element_t = std::decay_t<
            details::sequence_element_type_t <Seq> >;
        constexpr SkipOrdered(KeyEqual cmp = KeyEqual{}) noexcept
            : _cmp(std::move(cmp)){}
        
        bool operator()(SequenceState &attrs, const Seq& seq) const
        {
            if( attrs.step() == 0)
            {// first entry to sequence
                _bypass.reset();    
                if( !details::get_reference(seq).is_sequence_ordered() )
                    throw std::runtime_error("unordered input sequence");
            }
            return conditional_emplace(details::get_reference(seq).current());
        }
    private:
        /** @return true if element was not a duplicate 
        * @param element - really needs && since `seq.current()` may return as lref as a value
        */
        template <class T>
        bool conditional_emplace(T&& element) const
        {
            if(!_bypass.has_value() || !_cmp(*_bypass, element))
            {
                _bypass.emplace(element);
                return true;
            }
            return false;
        }
        mutable std::optional<decay_element_t> _bypass;
        KeyEqual _cmp;
    };


    /**
    *   Implement functor to apply Distinct for unordered sequences using std::unordered_set.  
    *   It brings memory footprint proportional to the source sequence length
    */
    template <class Seq, 
        class Hash = std::hash<std::decay_t<details::sequence_element_type_t <Seq>>>,
        class KeyEqual = std::equal_to<std::decay_t<details::sequence_element_type_t <Seq>>>
        >
    struct SkipUnordered
    {
        SkipUnordered(Hash hash = Hash{}, KeyEqual cmp = KeyEqual{})
            : _bypass(16, std::move(hash), std::move(cmp))
        {
        }

        bool operator()(SequenceState &attrs, const Seq& seq) const
        {
            if( attrs.step().current() == 0)
            {
                _bypass.clear();    
            }
            return _bypass.emplace(details::get_reference(seq).current()).second;
        }
        using dec_element_t = std::decay_t<details::sequence_element_type_t <Seq>>;
        using check_set_t = std::unordered_set<dec_element_t, Hash, KeyEqual>;
        mutable check_set_t _bypass;
    };

    /**
    *   Sequence to implement Distinct stream
    */
    template <class T, class Policy, class Src>
    struct DistinctSequence : public Sequence<T>
    {
        using this_t = DistinctSequence<T, Policy, Src>;
        using base_t = Sequence<T>;
        using element_t = typename base_t::element_t;
        using dereference_sequnce_t = details::dereference_t<std::decay_t<Src>>;

        constexpr DistinctSequence(Src&& src, Policy f) noexcept
            : _attrs(std::move(src), SequenceState{})
            , _policy(std::move(f))
        {
        }

        /** Ordering is the same as previous sequence in the pipeline */
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            return rsrc<Src>().is_sequence_ordered();
        }

        void start() override
        {
            auto& rs = rsrc<Src>();
            auto& pline = rsrc<SequenceState>();
            rs.start();
            pline.start();
            seek(rs, pline); 
        }

        bool in_range() const override
        {
            return rsrc<Src>().in_range();
        }
        
        element_t current() const override
        {
            return rsrc<Src>().current();
        }
        
        void next() override
        {
            auto& rs = rsrc<Src>();
            auto& pline = rsrc<SequenceState>();
            rs.next();
            pline.next();
            seek(rs, pline); 
        }

    private:
        
        void seek(dereference_sequnce_t& rs, SequenceState& pline)
        {
            while(rs.in_range() && !distinct_result())
            {
                rs.next();
                pline.next();
            }
        }

        template <class U>
        const auto& rsrc() const
        {
            return details::get_reference(std::get<U>(_attrs));
        }

        template <class U>
        auto& rsrc() 
        {
            return details::get_reference(std::get<U>(_attrs));
        }

        bool distinct_result()
        {
            return _attrs.typed_invoke(_policy);
        }

        OP::currying::CurryingTuple<
            std::decay_t<Src>, SequenceState> _attrs;
        Policy _policy;
    };

    /** Policy configures DistinctFactory to use default std::equal_to comparator on ordered sequences */
    struct OrderedDistinctPolicy
    {
        template <class Seq>
        using policy_t = SkipOrdered<Seq>;

        template <class Seq>
        constexpr auto construct() const noexcept
        {
            return SkipOrdered<Seq>{};
        }
    }; 

    /** Policy configures DistinctFactory to use custome comparator on ordered sequences */
    template <class Eq>
    struct OrderedDistinctPolicyWithCustomComparator
    {
        template <class Seq>
        using policy_t = SkipOrdered<Seq, Eq>;

        constexpr OrderedDistinctPolicyWithCustomComparator(Eq eq) noexcept
            : _eq(std::move(eq))
            {}
        
        template <class Seq>
        constexpr auto construct() const noexcept
        {
            return SkipOrdered<Seq, Eq>(_eq);
        }
        Eq _eq;
    }; 
    
    /** Policy configures DistinctFactory to use default std::equal_to and std::hash<> on un-ordered sequences */
    struct UnorderedHashDistinctPolicy
    {
        template <class Seq>
        constexpr auto construct() const noexcept
        {
            return SkipUnordered<Seq>{};
        }
    }; 

    /** Policy configures DistinctFactory to use custome comparator and standart std::hash<> on un-ordered sequences */
    template <class Eq>
    struct UnorderedDistinctPolicyWithCustomComparator
    {
        template <class Seq>
        using policy_t = SkipOrdered<Seq, Eq>;

        constexpr UnorderedDistinctPolicyWithCustomComparator(Eq eq) noexcept
            : _eq(std::move(eq))
            {}
        
        template <class Seq>
        constexpr auto construct() const
        {
            using hash_t = std::hash<std::decay_t<typename Seq::element_t>>;

            return SkipUnordered<Seq, hash_t, Eq>(hash_t{}, _eq);
        }
        Eq _eq;
    }; 

    /**
    *   Factory that creates DistinctSequence.
    * Implementation allows support ordered and unordered sequences, depending on policy provided.
    * \tparam Policy one of predefined or custom policies. Known policies are:
    *   - OrderedDistinctPolicy - sequence is ordered, to compare elements used `operator <`
    *   - OrderedDistinctPolicyWithCustomComparator - sequence is ordered but custom comparator is used
    *   - UnorderedHashDistinctPolicy - sequence is unordered and default std::equals_t/std::hash are used
    *   - UnorderedDistinctPolicyWithCustomComparator - sequence is unordered but comparator/hash are used
    * You can create your own policy for example to use more memory efficient storages than std::unordered_set.
    * Just declare functor and policy method `construct` to create functor:\code
    *   
    *   template <class Seq>
    *   constexpr auto construct() const
    *   {   // Must return instance of functor 
    *       return SkipUnordered<Seq, hash_t, Eq>(hash_t{}, _eq);
    *   }
    * \endcode
    *  Functor is allowed to recive following arguments in any combination or order:
    *   - SequenceState &attrs - to control current step number;
    *   - const Seq& - previous sequence in pipeline;
    *   - const typename Seq::element_t&  - current processed element
    */
    template <class Policy>
    struct DistinctFactory : FactoryBase
    {

        constexpr DistinctFactory(Policy policy = Policy{}) noexcept
            : _policy(std::move(policy))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_conatiner_t = details::sequence_type_t<details::dereference_t<Src>>;
            using element_t = typename src_conatiner_t::element_t;
            using base_t = Sequence<element_t>;
            using effective_policy_t = decltype(_policy.template construct<Src>());
            return DistinctSequence<element_t, effective_policy_t, std::decay_t<Src>>(
                std::forward<Src>(src), _policy.template construct<Src>());
        }
        Policy _policy;
    };


    /** Same as DistinctFactory, but allows pick between ordered and unordered algorithms at runtime
    * depending if source sequence supports ordering
    */
    template <class D1, class D2>
    struct SmartDistinctFactory : FactoryBase
    {

        constexpr SmartDistinctFactory(D1&& d1, D2&& d2) noexcept
            : _ordered_distinct_factory(std::forward<D1>(d1))
            , _unordered_distinct_factory(std::forward<D2>(d2))
        {
        }
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using seq_t = std::decay_t<Src>;
            using d1_seq_t = std::decay_t<decltype(_ordered_distinct_factory.compound(std::move(src)))>;
            using d2_seq_t = std::decay_t<decltype(_unordered_distinct_factory.compound(std::move(src)))>;
            using proxy_sequence_t = SequenceProxy< d1_seq_t, d2_seq_t >;
            if(details::get_reference(src).is_sequence_ordered() )
            {
                return proxy_sequence_t( _ordered_distinct_factory.compound(std::move(src)) );
            }
            else
            {
                return proxy_sequence_t( _unordered_distinct_factory.compound(std::move(src)) );
            }
        }

        D1 _ordered_distinct_factory;
        D2 _unordered_distinct_factory;
    };
}  //ns:OP::flur

#endif //_OP_FLUR_DISTINCT__H_
