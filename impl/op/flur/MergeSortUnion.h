#pragma once
#ifndef _OP_FLUR_UNIONMERGE__H_
#define _OP_FLUR_UNIONMERGE__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Join.h>

namespace OP::flur
{
    /**
    *   Implement sequence to unite compile time defined tuple of ordered input sequences.
    */
    template <class Elm, class ...Rx>
    struct MergeSortUnionSequence : public OrderedSequence<Elm>
    {
        using base_t = Sequence<Elm>;
        using element_t = typename base_t::element_t;

        using all_seq_tuple_t = std::tuple< Rx... >;
        using getter_t = base_t&(*)(all_seq_tuple_t&);
        //allows convert compile-time indexing to runtime at(i)
        using ptr_holder_t = std::array<getter_t, sizeof ... (Rx)>;

        template <class Tupl>
        constexpr MergeSortUnionSequence(Tupl&& rx) noexcept
            : _scope( std::forward<Tupl>(rx) )
            , _retain_size(0)
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
                            greater_cmp);
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
                greater_cmp);
            if( !current->in_range() )
                --_retain_size;
            if( _retain_size ) // move to [0] sequence with minimal `current`
                std::push_heap(
                _retain_heap.begin(), 
                _retain_heap.begin() + _retain_size,
                greater_cmp);
        }

    protected:
        constexpr size_t union_size() const noexcept
        {
            return sizeof ... (Rx);
        }

    private:
        all_seq_tuple_t _scope;
        //contains pointers to _scope instances
        std::array<base_t*, sizeof...(Rx)> _retain_heap;
        unsigned _retain_size;

        /** since heap supports revert order need implement greater compare */
        static bool greater_cmp(const base_t* left, const base_t* right)
        {
            return left->current() > right->current();
        }
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
            static_assert(
                std::conjunction_v<
                std::is_convertible< typename containers_t::element_t, typename details::sequence_type_t<Tx>::element_t >...>,
                "merge-sort-union must use sequences producing same type elements");

            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return MergeSortUnionSequence<
                        typename containers_t::element_t,
                        containers_t, details::sequence_type_t<Tx>... >(
                            std::make_tuple(std::move(src),
                                details::get_reference(rx).compound() ... )
                        );}, _right);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            using containers_t = details::sequence_type_t<Src>;
            static_assert(
                std::conjunction_v<
                std::is_convertible< typename containers_t::element_t, typename details::sequence_type_t<Tx>::element_t >...>,
                "merge-sort-union must use sequences producing same type elements");

            return
                std::apply([&](auto&& ... rx) ->decltype(auto) {
                return MergeSortUnionSequence<
                    typename containers_t::element_t,
                    containers_t, details::sequence_type_t<Tx>... >(
                            std::make_tuple(std::move(src),
                            details::get_reference(rx).compound() ...)
                    ); }, std::move(_right));
        }

        /** This factory can be used as root-level lazy range */
        constexpr auto compound() const noexcept
        {
            static_assert(
                sizeof...(Tx) > 1,
                "specify 2 or more sources in MergeSortUnionFactory<Cmp, Tx...>");

            using containers_t = details::sequence_type_t<Src>;

            using all_factories_t = decltype(_right);
            using zero_sequence_t = details::sequence_type_t< 
                    std::tuple_element_t<0, all_factories_t> >;
            using element_t = typename details::dereference_t<zero_sequence_t>::element_t;

            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return MergeSortUnionSequence<
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
