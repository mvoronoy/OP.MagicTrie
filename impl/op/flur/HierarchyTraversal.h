#pragma once
#ifndef _OP_FLUR_HIERARCHYTRAVERSAL__H
#define _OP_FLUR_HIERARCHYTRAVERSAL__H

#ifdef _MSVC_LANG
// warning C4172: "returning address of local variable or temporary" must be an error
// when trying to return result from function combinating `const T&` and `T` values
#pragma warning( error: 4172)
#endif //_MSVC_LANG
#include <functional>
#include <memory>
#include <list>
#include <optional>     
#include <unordered_set>

#include <op/common/Currying.h>
#include <op/common/ftraits.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/FlatMapping.h>
#include <op/flur/Ingredients.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    /** Tag algorithm used for hierarchy traversal */
    enum class HierarchyTraversal
    {
        /** Explore from top to down, then siblings. */
        deep_first,
        /** Explore top siblings then step down. */
        breadth_first
    };

    template <class Src, class FChildrenResolve, class Base>
    struct DeepFirstSequence : Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using traits_t = FlatMapTraits<FChildrenResolve, Src>;
        using flat_element_t = OP::flur::details::sequence_type_t<
            typename traits_t::applicator_result_t>;

        constexpr DeepFirstSequence(Src&& rref, FChildrenResolve applicator) noexcept
            : _origin(std::move(rref))
            , _applicator(std::move(applicator))
        {
        }

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return false;
        }

        virtual void start()
        {
            _gen1.clear();
            auto& take_from = details::get_reference(_origin); 
            take_from.start();
            while(take_from.in_range() && !step_in() )
            { 
                take_from.next();//step_out() may be used there, but explicit `next` more optimal
            }
        }

        virtual bool in_range() const override
        {
            return details::get_reference(_origin).in_range() 
                || !details::get_reference(_gen1.empty());
        }

        virtual element_t current() const override
        {
            return details::get_reference(_gen1.back()).current();
        }

        virtual void next() override
        {
            while (in_range() && !step_in())
            {
                if(step_out()) 
                    return;
            }
        }

    private:
        using then_conatiner_t = std::list<flat_element_t>;

        bool step_in()
        {
            return _gen1.empty() ? _step_in(_origin) : _step_in(_gen1.back());
        }

        template <class TakeFrom>
        bool _step_in(TakeFrom& take_from)
        {
            auto& rfrom = details::get_reference(take_from);

            _gen1.emplace_back(_applicator(rfrom.current()).compound());
            auto& at = details::get_reference(_gen1.back());
            at.start();
            if (!at.in_range())
            {//keep stack clear of non-productive items
                _gen1.pop_back();
                return false;
            }
            return true;
        }
        /** pop item from stack and may be deposit from origin
        * \return `true` when stack contains correct next item and `false` when step-in 
        * into deposited from origin
        */ 
        bool step_out()
        {
            while (!_gen1.empty())
            {
                auto& rfrom = details::get_reference(_gen1.back());
                rfrom.next();
                if (rfrom.in_range())
                    return true;
                _gen1.pop_back();
            }
            //stack is over, need deposit item from origin source
            auto& rorigin = details::get_reference(_origin);
            if (rorigin.in_range())
            {
                rorigin.next();
            }
            return false;
        }

        then_conatiner_t _gen1;
        FChildrenResolve _applicator;
        Src _origin;
    };//DeepFirstSequence

    //---
    template <class Src, class FChildrenResolve, class Base >
    struct BreadthFirstSequence : Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using traits_t = FlatMapTraits<FChildrenResolve, Src>;
        using flat_element_t = OP::flur::details::sequence_type_t<
            typename traits_t::applicator_result_t>;

        constexpr BreadthFirstSequence(Src&& rref, FChildrenResolve applicator) noexcept
            : _origin(std::move(rref))
            , _applicator(std::move(applicator))
        {
        }

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return false;
        }

        virtual void start()
        {
            _is_gen0 = true;
            _gen1.clear();
            auto& rorigin = details::get_reference(_origin);
            for (rorigin.start(); rorigin.in_range(); rorigin.next())
            {//find first non-empty child
                if (flatten_current<true>(rorigin))
                    return;
            }
        }

        virtual bool in_range() const override
        {
            if (_is_gen0)
            {
                return details::get_reference(_origin).in_range();
            }
            else return (!_gen1.empty());
        }

        virtual element_t current() const override
        {
            return details::get_reference(_gen1.front()).current();
        }

        virtual void next() override
        {
            assert(!_gen1.empty());

            //drain current element before advance next
            auto& top = details::get_reference(_gen1.front());
            flatten_current<false>(top);
            top.next();

            while(!_gen1.empty())
            {
                auto& at = details::get_reference(_gen1.front());
                if (at.in_range())
                    return;
                _gen1.pop_front();
                if (_is_gen0)
                {
                    auto& rorigin = details::get_reference(_origin);
                    for (rorigin.next(); rorigin.in_range(); rorigin.next())
                    {//find next non-empty child of gen0
                        if (flatten_current<true>(rorigin))
                            return;
                    }
                    _is_gen0 = false;
                }
            }

        }

    private:
        using then_vector_t = std::list<flat_element_t>;
        
        template <bool high_priority, class TakeFrom>
        bool flatten_current(TakeFrom& take_from)
        {
            //resolve pair of methods list::emplace_front/pop_front or list::emplace_back/pop_back
            constexpr auto method_pair = list_method_pair <high_priority>;
            auto& at = details::get_reference(
                (_gen1.*method_pair.first)
                (_applicator(take_from.current()).compound()));
            at.start();
            if (at.in_range())
                return true;
            (_gen1.*method_pair.second)(); //remove non-productive item
            return false;
        }
        
        template <bool high_pritority>
        constexpr static inline auto list_method_pair =
            high_pritority 
                ? std::make_pair(&then_vector_t::template emplace_front<flat_element_t&&>, &then_vector_t::pop_front)
                : std::make_pair(&then_vector_t::template emplace_back<flat_element_t&&>, &then_vector_t::pop_back);


        then_vector_t _gen1;
        bool _is_gen0 = true;
        FChildrenResolve _applicator;
        Src _origin;
        
    };//BreadthFirstSequence


    /** Lazy factory to create linear sequence of hierarchy traversal without stack recursion.
    * This can be used to iterate elements of tree- or graph- structures. In case of graphs your 
    * functor of children resolution is responsible to prevent dead loops (for example use 
    * std::unorder_set to track visited vertices).
    *
    * \tparam FChildrenResolve - functor to resolve children. It expects parent element at imput 
    *   but must return some flur LazyFactory to enumerate children entites. 
    * \tparam HierarchyTraversal traversal_alg_c algorithm how to deal with traversal order. By default 
    *   `HierarchyTraversal::deep_first` is used as fast-most.
    */
    template <class FChildrenResolve, HierarchyTraversal traversal_alg_c = HierarchyTraversal::deep_first>
    struct HierarchyTraversalFactory : FactoryBase
    {
        using applicator_traits_t = OP::utils::function_traits<std::decay_t<FChildrenResolve>>;
        using applicator_result_t = typename applicator_traits_t::result_t;
        using applicator_element_t = typename details::sequence_type_t<applicator_result_t>::element_t;

        constexpr HierarchyTraversalFactory(FChildrenResolve applicator) noexcept
            : _applicator(std::move(applicator))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& seq) const& noexcept
        {
            using element_t = typename OP::flur::details::dereference_t<Src>::element_t;
            using base_seq_t = Sequence< element_t >;

            static_assert(
                std::is_convertible_v< typename OP::flur::details::dereference_t<Src>::element_t, applicator_element_t>,
                "must operate on sequences producing same type of elements");
            if constexpr (traversal_alg_c == HierarchyTraversal::deep_first)
            {   //just reuse FlatMap sequence for this case
                //using f_t = DeepFirstFunctor<applicator_element_t, FChildrenResolve>;
                //return FlatMappingFactory< f_t >(f_t(_applicator)).compound(std::move(seq));
                return DeepFirstSequence<Src, FChildrenResolve, base_seq_t>(
                    std::move(seq), _applicator);
                
            }
            else //traversal_alg_c == HierarchyTraversal::breadth_first
            {
                return BreadthFirstSequence<Src, FChildrenResolve, base_seq_t>(
                    std::move(seq), _applicator);
            }
        }

        template <class Src>
        constexpr auto compound(Src&& seq) && noexcept
        {
            using element_t = typename OP::flur::details::dereference_t<Src>::element_t;
            static_assert(
                std::is_convertible_v< element_t, applicator_element_t>,
                "must operate on sequences producing same type of elements");
            using base_seq_t = Sequence< element_t >;
            if constexpr (traversal_alg_c == HierarchyTraversal::deep_first)
            {   //just reuse FlatMap sequence for this case
                //using f_t = DeepFirstFunctor<applicator_element_t, FChildrenResolve>;
                //return FlatMappingFactory< f_t >(f_t(_applicator)).compound(std::move(seq));
                return DeepFirstSequence<Src, FChildrenResolve, base_seq_t>(
                    std::move(seq), std::move(_applicator));
                
            }
            else //traversal_alg_c == HierarchyTraversal::breadth_first
            {
                return BreadthFirstSequence<Src, FChildrenResolve, base_seq_t>(
                    std::move(seq), std::move(_applicator));
            }
        }

        FChildrenResolve _applicator;
    };
}  //ns:OP::flur

#endif //_OP_FLUR_HIERARCHYTRAVERSAL__H
