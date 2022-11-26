#pragma once
#ifndef _OP_FLUR_HIERARCHYTRAVERSAL__H
#define _OP_FLUR_HIERARCHYTRAVERSAL__H

#ifdef _MSVC_LANG
// warning C4172: "returning address of local variable or temporary" must be an error
// when trying to return result from function combination
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

    /** Helper to provide meta-information about trabersal hierarchy. */
    struct HierarchyAttrs : PipelineAttrs
    {
        using level_t = typename PipelineAttrs::Step;
        level_t _level;
    };

    template <class Src, class FChildrenResolve,
        class Base = Sequence< typename OP::flur::details::dereference_t<Src>::element_t > >
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
            _origin.start();
            //<-- no drain there since need to consume first elem
        }

        virtual bool in_range() const override
        {
            return !_gen1.empty() || _origin.in_range();
        }

        virtual element_t current() const override
        {
            const Sequence<element_t>* take_from =
                _gen1.empty() ?
                static_cast<const Sequence<element_t>*>(&_origin) :
                static_cast<const Sequence<element_t>*>(&_gen1.back());
            return take_from->current();
        }

        virtual void next() override
        {
            bool is_root = _gen1.empty();
            Sequence<element_t>* take_from =
                is_root ?
                static_cast<Sequence<element_t>*>(&_origin) :
                static_cast<Sequence<element_t>*>(&_gen1.back());

            _gen1.emplace_back(_applicator(take_from->current()).compound());
            take_from->next();
            if (!is_root && !take_from->in_range())
            { //for elements from list check validity, don't allow empty items in stack
                auto erpt = _gen1.end();
                std::advance(erpt, -2);
                _gen1.erase(erpt);
            }
            auto& at = _gen1.back();
            at.start();
            if (!at.in_range())
                _gen1.pop_back();
        }

    private:
        using then_vector_t = std::list<flat_element_t>;

        then_vector_t _gen1;
        FChildrenResolve _applicator;
        Src _origin;
    };//DeepFirstSequence

    //---
    template <class Src, class FChildrenResolve, 
        class Base = Sequence< typename OP::flur::details::dereference_t<Src>::element_t > >
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
            _gen1_idx = 0;
            _gen1.clear(), _gen2.clear();
            _origin.start();
            drain();
        }

        virtual bool in_range() const override
        {
            if (_is_gen0)
            {
                return _origin.in_range();
            }
            else return (_gen1_idx < _gen1.size());
        }

        virtual element_t current() const override
        {
            if (_is_gen0)
            {
                return _origin.current();
            }
            else 
            {
                return 
                    _gen1[_gen1_idx].current();
            }
        }

        virtual void next() override
        {
            if (_is_gen0)
            {
                _origin.next();
            }
            else
            {
                auto& at = _gen1[_gen1_idx];
                assert(at.in_range());
                at.next();
                if( !at.in_range() )
                {
                    ++_gen1_idx;
                    seek_gen1();
                }
            }
            drain();
        }

    private:
        using then_vector_t = std::vector<flat_element_t>;

        /** Drain children sequence from current element to the generation-2, when nothing
        * to drain swaps gen-2 with gen-1 to continue iteration
        */
        void drain()
        {
            if (!in_range())
            {//need start-over generation 2
                _gen1.clear();
                //make gen-2 available over gen-1
                std::swap(_gen1, _gen2);
                _is_gen0 = false;
                _gen1_idx = 0;
                if(!seek_gen1())
                    return; //EOS
            }
            _gen2.emplace_back(_applicator(current()).compound());
        }

        /** Search generation-1 for the first non-empty sequence 
        \return true if current state in_range
        */
        bool seek_gen1()
        {
            for (; _gen1_idx < _gen1.size(); ++_gen1_idx)
            { //find first non-empty sequence for generation-1 leftovers
                auto& at = _gen1[_gen1_idx];
                at.start();
                if (at.in_range())
                {
                    return true;
                }
            }
            //no entries in gen-1, EOS
            _gen1.clear();
            return false;
        }

        then_vector_t _gen1, _gen2;
        bool _is_gen0 = true;
        size_t _gen1_idx = 0;
        FChildrenResolve _applicator;
        Src _origin;
        
    };//BreadthFirstSequence


    /** Lazy factory to create linear sequence of hierarchy traversal without stack recursion.
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
        using applicator_traits_t = OP::utils::function_traits<std::decay_t<FChildrenResolve>>;
        using applicator_result_t = typename applicator_traits_t::result_t;
        using applicator_element_t = typename details::sequence_type_t<applicator_result_t>::element_t;

        constexpr HierarchyTraversalFactory(FChildrenResolve applicator) noexcept
            : _applicator(std::move(applicator))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& seq) const noexcept
        {
            static_assert(
                std::is_convertible_v< typename OP::flur::details::dereference_t<Src>::element_t, applicator_element_t>,
                "must operate on sequences producing same type of elements");
            if constexpr (traversal_alg_c == HierarchyTraversal::deep_first)
            {   //just reuse FlatMap sequence for this case
                //using f_t = DeepFirstFunctor<applicator_element_t, FChildrenResolve>;
                //return FlatMappingFactory< f_t >(f_t(_applicator)).compound(std::move(seq));
                return DeepFirstSequence<Src, FChildrenResolve>(std::move(seq), _applicator);
                
            }
            else //traversal_alg_c == HierarchyTraversal::breadth_first
            {
                return BreadthFirstSequence<Src, FChildrenResolve>(std::move(seq), _applicator);
            }
        }

        FChildrenResolve _applicator;
    };
}  //ns:OP::flur

#endif //_OP_FLUR_HIERARCHYTRAVERSAL__H
