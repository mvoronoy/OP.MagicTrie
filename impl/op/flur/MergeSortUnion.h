#pragma once
#ifndef _OP_FLUR_UNIONMERGE__H_
#define _OP_FLUR_UNIONMERGE__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    /**
    *   Implement sequence to union-merge  of ordered input sequences.
    */
    template <class Comp, class Elm, class ...Rx>
    struct MergeSortUnionSequence : public OrderedSequence<Elm>
    {
        static_assert(
            std::conjunction_v<
            std::is_convertible< typename Rx::element_t, Elm >...>,
            "union-merge-sort must operate on sequences producing same type elements");

        using base_t = Sequence<Elm>;
        using element_t = typename base_t::element_t;

        using all_seq_tuple_t = std::tuple< Rx... >;

        template <class Tupl>
        constexpr MergeSortUnionSequence(Comp comp, Tupl&& rx) noexcept
            : _scope( std::forward<Tupl>(rx) )
            , _retain_size(0)
            , _comparator(std::move(comp))
        {
        }

        void start() override
        {
            _retain_size = 0;
            auto init_step = [&](auto& seqx){
                    _retain_heap[_retain_size] = &seqx;
                    seqx.start();
                    if (seqx.in_range())
                    {
                        std::push_heap(
                            _retain_heap.begin(),
                            _retain_heap.begin() + ++_retain_size,
                            _comparator);
                    }
                };
            std::apply([&](auto& ...seqx) {
                (init_step(seqx), ...);
                }, _scope);
        }

        bool in_range() const override
        {
            return _retain_size > 0;
        }
        
        element_t current() const override
        {
            return _retain_heap[0]->current();
        }
        
        void next() override
        {
            auto* current = _retain_heap[0];
            current->next();
            //pop will move current to the [_retain_size-1]
            std::pop_heap(
                _retain_heap.begin(), 
                _retain_heap.begin() + _retain_size,
                _comparator);
            if( !current->in_range() )
                --_retain_size;
            if( _retain_size ) // move to [0] sequence with minimal `current`
                std::push_heap(
                    _retain_heap.begin(), 
                    _retain_heap.begin() + _retain_size,
                    _comparator);
        }

    private:
        all_seq_tuple_t _scope;
        //contains pointers to _scope instances
        std::array<base_t*, sizeof...(Rx)> _retain_heap{};
        unsigned _retain_size;
        
        /** heap queue uses reverse order, so this structure 
        * implements std::greater semantic
        */
        struct GreatComparator
        {
            constexpr GreatComparator(Comp&& comp) noexcept
                : _comp(std::move(comp))
            {}
            
            bool operator()(const base_t* left, const base_t* right)
            {
                return _comp(left->current(), right->current()) > 0;
            }

            Comp _comp;
        } _comparator;

    };

    template <class Comp, class ... Tx>
    struct MergeSortUnionFactory : FactoryBase
    {

        constexpr MergeSortUnionFactory(Comp cmp, Tx&& ... rx ) noexcept
            : _comp(std::move(cmp))
            , _right(std::make_tuple(std::forward<Tx>(rx)...)) //make_tuple is imprtant to get rid-off any refernces 
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            using containers_t = details::sequence_type_t<Src>;

            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return MergeSortUnionSequence<
                        Comp, 
                        typename containers_t::element_t,
                        containers_t, decltype(details::get_reference(rx).compound())... >(
                            _comp,
                            std::make_tuple(std::move(src),
                                details::get_reference(rx).compound() ... )
                        );}, _right);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            using containers_t = details::sequence_type_t<Src>;

            return
                std::apply([&](auto&& ... rx) ->decltype(auto) {
                return MergeSortUnionSequence<
                    Comp,
                    typename containers_t::element_t,
                    containers_t, decltype(std::move(details::get_reference(rx)).compound())... >(
                            std::move(_comp),
                            std::make_tuple(std::move(src),
                                //let factory recognize that move semantic used
                            std::move(details::get_reference(rx)).compound() ...)
                    ); }, std::move(_right));
        }

        /** This factory can be used as root-level lazy range */
        constexpr auto compound() const noexcept
        {
            static_assert(
                sizeof...(Tx) > 1,
                "specify 2 or more sources in MergeSortUnionFactory<Cmp, Tx...>");

            using all_factories_t = decltype(_right);
            using zero_sequence_t = details::sequence_type_t< 
                    std::tuple_element_t<0, all_factories_t> >;
            using element_t = typename details::dereference_t<zero_sequence_t>::element_t;

            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return MergeSortUnionSequence<
                        Comp,
                        element_t,
                        details::sequence_type_t<Tx>... >(
                            std::make_tuple( details::get_reference(rx).compound() ... )
                    );}, _right);
        }

    private:
        std::tuple<std::decay_t<Tx> ...> _right;
        Comp _comp;
    };

} //ns:OP

#endif //_OP_FLUR_UNIONMERGE__H_
