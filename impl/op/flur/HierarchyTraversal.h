#pragma once
#ifndef _OP_FLUR_HIERARCHYTRAVERSAL__H
#define _OP_FLUR_HIERARCHYTRAVERSAL__H

#include <functional>
#include <memory>
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

    /** Helper to provide meta-information about trabersal hierarchy. */
    struct HierarchyAttrs : PipelineAttrs
    {
        using level_t = typename PipelineAttrs::Step;
        level_t _level;
    };

    /** Implement functor to navigate over hierachy (tree- or graph- like) in deep-first way.
    *  \tparam T - element of hierarchy.
    *  \tparam FChildrenResolve - functor to resolve children from hierarchy element. 
    */
    template <class T, class FChildrenResolve>
    struct DeepFirstFunctor
    {
        using this_t = DeepFirstFunctor<T, FChildrenResolve>;
        using raw_applicator_result_t = decltype(std::declval<FChildrenResolve>()(std::declval<const T&>()));
        using applicator_result_t = std::decay_t<raw_applicator_result_t>;
        using applicator_element_t = typename details::sequence_type_t<applicator_result_t>::element_t;
        //static_assert(
        //    std::is_same_v< T, applicator_element_t>,
        //    "must operate on sequences producing same type elements");
        using poly_result_t = AbstractPolymorphFactory<const T&>;
        using result_t = std::shared_ptr<poly_result_t>;

        constexpr DeepFirstFunctor(FChildrenResolve f, int lev = 3) noexcept
            : _applicator(std::move(f))
            , _lev(lev)
        {}

        
        /*std::shared_ptr< Sequence<const T&> >*/result_t operator ()(const std::string& elem) const
        {
            std::cout << "\n" << std::setw(4 - _lev) << ' ';
            if (_lev)
            {
                auto val = src::of_value(elem);
                auto fl = src::of_value(std::move(elem))
                    >> then::flat_mapping(_applicator)
                    >> then::flat_mapping(this_t(_applicator, _lev - 1));
                auto cf = val
                    >> then::union_all(std::move(fl))
                    ;
                return OP::flur::make_shared(std::move(cf));
            }
            else
                return OP::flur::make_shared(src::of_cref_value(std::move(elem)));
        }
    private:
        FChildrenResolve _applicator;
    };

    /**Lazy factory to create linear sequence of hierarchy traversal without stack recursion.
    * This can be used to iterate elements of tree- or graph- structures. In case of graphs your 
    * functor of children resolution is responsible to prevent dead loops (for example use 
    * std::unorder_set to track visited vertices).
    *
    * \tparam FChildrenResolve - functor to resolve children. It may use several argument signatures,
    *   but must return some flur LazyFactory to enumerate children entites. This method can use any
    *   combination of the following input arguments:
    *   - `const HierarchyAttrs&` - to control _generation (number times the sequence was started), _step (
    *       current step from the sequence beginning), _level (current level of processed hierarchy).
    *   - parent element (type depends on source sequence).
    * \tparam HierarchyTraversal traversal_alg_c algorithm how to deal with traversal order. By default 
    *   `HierarchyTraversal::deep_first` is used as fast-most.
    */
    template <class FChildrenResolve, HierarchyTraversal traversal_alg_c = HierarchyTraversal::deep_first>
    struct HierarchyTraversalFactory : FactoryBase
    {
        using applicator_traits_t = OP::utils::function_traits<FChildrenResolve>;
        using applicator_element_t = typename details::sequence_type_t<applicator_result_t>::element_t;

        constexpr HierarchyTraversalFactory(FChildrenResolve applicator) noexcept
            : _applicator(std::move(applicator))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& seq) const
        {
            static_assert(
                std::is_same_v< typename OP::flur::details::dereference_t<Src>::element_t, applicator_element_t>,
                "must operate on sequences producing same type elements");
            if constexpr (traversal_alg_c == HierarchyTraversal::deep_first)
            {   //just reuse FlatMap sequence for this case
                using f_t = DeepFirstFunctor<applicator_element_t, FChildrenResolve>;
                return FlatMappingFactory< f_t >(f_t(_applicator)).compound(std::move(seq));
            }
            else //traversal_alg_c == HierarchyTraversal::breadth_first
            {}
            return ProxySequence<Src, F>(std::move(seq), _applicator);
        }

        FChildrenResolve _applicator;
    };
}  //ns:OP::flur

#endif //_OP_FLUR_HIERARCHYTRAVERSAL__H
