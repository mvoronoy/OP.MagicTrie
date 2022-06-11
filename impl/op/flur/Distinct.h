#pragma once
#ifndef _OP_FLUR_DISTINCT__H_
#define _OP_FLUR_DISTINCT__H_

#include <functional>
#include <memory>
#include <optional>     
#include <unordered_set>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Ingredients.h>
namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{

    /**
    *   Implement functor to apply Distinct for ordered sequences. It has minimal 
    *   complexity and memory footprint (in compare with unordered implementation
    */
    template <class Seq, class KeyEqual = std::equal_to<typename Seq::element_t>>
    struct SkipOrdered
    {
        using decay_element_t = std::decay_t<typename Seq::element_t>;
        SkipOrdered(KeyEqual cmp = KeyEqual{})
            : _cmp(std::move(cmp)){}
        
        bool operator()(PipelineAttrs &attrs, const Seq& seq, const typename Seq::element_t& cur) const
        {
            if( attrs._step.current() == 0)
            {// first entry to sequence
                _bypass.reset();    
                if( !seq.is_sequence_ordered() )
                    throw std::runtime_error("unordered input sequence");
            }
            if(! _bypass.has_value() || !_cmp(*_bypass, cur))
            {
                _bypass.emplace(cur);
                return true;
            }
            return false;
        }
        mutable std::optional<decay_element_t> _bypass;
        KeyEqual _cmp;
    };


    /**
    *   Implement functor to apply Distinct for unordered sequences using std::unordered_set.  
    *   It brings memory footprint propertional to sequence length
    */
    template <class Seq, 
        class Hash = std::hash<std::decay_t<typename Seq::element_t>>,
        class KeyEqual = std::equal_to<std::decay_t<typename Seq::element_t>>
        >
    struct SkipUnordered
    {
        SkipUnordered(Hash hash = Hash{}, KeyEqual cmp = KeyEqual{})
            : _bypass(16, std::move(hash), std::move(cmp))
        {
        }

        bool operator()(PipelineAttrs &attrs, const typename Seq::element_t& cur) const
        {
            if( attrs._step.current() == 0)
            {
                _bypass.clear();    
            }
            return _bypass.emplace(cur).second;
        }
        using check_set_t = std::unordered_set<std::decay_t<typename Seq::element_t>, Hash, KeyEqual>;
        mutable check_set_t _bypass;
    };

    /**
    *   Sequence to implement Distinct stream
    */
    template <class Policy, class Src>
    struct Distinct : public Sequence<typename Src::element_t>
    {
        using base_t = Sequence<typename Src::element_t>;
        using element_t = typename base_t::element_t;
        
        constexpr Distinct(Src&& src, Policy f) noexcept
            : _attrs(std::move(src), PipelineAttrs{})
            , _policy(std::move(f))
        {
        }

        /** Ordering is the same as previous sequence in the pipeline */
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const
        {
            return rsrc().is_sequence_ordered();
        }

        virtual void start()
        {
            auto& rs = rsrc();
            auto& pline = rsrc<PipelineAttrs>();
            rs.start();
            pline._step.start();
            while(rs.in_range() && !invoke(_policy, _attrs))
            {
                rs.next();
                pline._step.next();
            } 
        }
        virtual bool in_range() const
        {
            return rsrc().in_range();
        }
        virtual element_t current() const
        {
            return rsrc().current();
        }
        virtual void next()
        {
            auto& rs = rsrc();
            auto& pline = rsrc<PipelineAttrs>();
            rs.next();
            pline._step.next();
            while(rs.in_range() && !invoke(_policy, _attrs))
            {
                rs.next();
                pline._step.next();
            } 
        }
    private:
        template <class T = Src>
        const auto& rsrc() const
        {
            return details::get_reference(std::get<T>(_attrs));
        }
        template <class T = Src>
        auto& rsrc() 
        {
            return details::get_reference(std::get<T>(_attrs));
        }
        template <typename F, typename Attrs>
        static bool invoke(F& func, Attrs& attrs) 
        {
            using traits_t = OP::utils::function_traits<F>;
            using result_t = typename traits_t::result_t;

            return do_method(func, attrs, std::make_index_sequence<traits_t::arity_c>());
        }

        template <
            typename F,
            typename Attrs,
            size_t... I,
            typename traits_t = OP::utils::function_traits<F>,
            typename result_t = typename traits_t::result_t>
        static result_t do_method(
                F& func, Attrs& attrs, std::index_sequence<I...>)
        {
            return func(
                inject_argument<typename traits_t::template arg_i<I>>(attrs)...
                );
        }
        /** Method finds best candidate to argument of invocation some handler method of Context::cmd
         *
         * @return reference 
         */
        template <typename arg_t, typename Attrs>
        static constexpr auto inject_argument(Attrs& attrs)
        {
            using t_t = std::decay_t<arg_t>;
            if constexpr(std::is_same_v< std::decay_t<typename Src::element_t>, t_t>)
                return details::get_reference(std::get<Src>(attrs)).current();
            else
                return std::ref(std::get<t_t>(attrs));
        }

        std::tuple<Src, PipelineAttrs> _attrs;
        Policy _policy;
    };

    /** Policy configures DistinctFactory to use default std::equal_to comparator on ordered sequnces */
    struct OrderedDistinctPolicy
    {
        template <class Seq>
        using policy_t = SkipOrdered<Seq>;

        template <class Seq>
        auto construct() const
        {
            return SkipOrdered<Seq>{};
        }
    }; 

    /** Policy configures DistinctFactory to use custome comparator on ordered sequnces */
    template <class Eq>
    struct OrderedDistinctPolicyWithCustomComparator
    {
        template <class Seq>
        using policy_t = SkipOrdered<Seq, Eq>;

        constexpr OrderedDistinctPolicyWithCustomComparator(Eq eq)
            : _eq(std::move(eq))
            {}
        
        template <class Seq>
        constexpr auto construct() const
        {
            return SkipOrdered<Seq, Eq>(_eq);
        }
        Eq _eq;
    }; 
    
    /** Policy configures DistinctFactory to use default std::equal_to and std::hash<> on un-ordered sequnces */
    struct UnorderedHashDistinctPolicy
    {
        template <class Seq>
        constexpr auto construct() const
        {
            return SkipUnordered<Seq>{};
        }
    }; 

    /** Policy configures DistinctFactory to use custome comparator and standart std::hash<> on un-ordered sequnces */
    template <class Eq>
    struct UnorderedDistinctPolicyWithCustomComparator
    {
        template <class Seq>
        using policy_t = SkipOrdered<Seq, Eq>;

        constexpr UnorderedDistinctPolicyWithCustomComparator(Eq eq)
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
    *   Factory that creates Distinct sequence.
    * Implementation allows support ordered and unordered sequences, depending on policy provided.
    * \tparam Policy one of predefined or custom policies. Known policies are:
    *   - OrderedDistinctPolicy
    *   - OrderedDistinctPolicyWithCustomComparator
    *   - UnorderedHashDistinctPolicy
    *   - UnorderedDistinctPolicyWithCustomComparator
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
    *   - PipelineAttrs &attrs - to control current step number;
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
            using base_t = Sequence<typename src_conatiner_t::element_t>;
            using effective_policy_t = decltype(_policy.template construct<Src>());
            return Distinct<effective_policy_t, std::decay_t<Src>>(
                std::forward<Src>(src), _policy.template construct<Src>());
        }
        Policy _policy;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_DISTINCT__H_
