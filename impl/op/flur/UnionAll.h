#pragma once
#ifndef _OP_FLUR_UNIONALL__H_
#define _OP_FLUR_UNIONALL__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    /**
    *   Implement sequence to unite compile time defined tuple of input sequences.
    *    Sequence doesn't support ordering.
    */
    template <class Elm, class ...Rx>
    struct UnionAllSequence : public Sequence<Elm>
    {
        static_assert(
            std::conjunction_v<
                std::is_convertible< typename details::dereference_t<Rx>::element_t, Elm >...>,
            "union-all must operate on sequences producing same type elements");
        using base_t = Sequence<Elm>;
        using element_t = typename base_t::element_t;

        using all_seq_tuple_t = std::tuple< Rx... >;
        using getter_t = base_t&(*)(all_seq_tuple_t&);
        //allows convert compile-time indexing to runtime at(i)
        using ptr_holder_t = std::array<getter_t, sizeof ... (Rx)>;

        template <class Tupl>
        explicit constexpr UnionAllSequence(Tupl&& rx) noexcept
            : _scope( std::move(rx) )
            , _index(sizeof...(Rx)) //mark EOS status
        {
        }

        void start() override
        {
            _index = 0;
            at(_index).start();
            seek();
        }

        bool in_range() const override
        {
            return ( _index < union_size() )
                && at(_index).in_range();
        }
        element_t current() const override
        {
            return at(_index).current();
        }
        
        void next() override
        {
            at(_index).next();
            seek();
        }

    protected:

        constexpr size_t union_size() const noexcept
        {
            return sizeof ... (Rx);
        }

        base_t& at(size_t i) noexcept
        {
            return details::get_reference((sequence_resolver[i])(_scope));
        }

        const base_t& at(size_t i) const noexcept
        {
            auto& ref = const_cast<all_seq_tuple_t&>(_scope);
            return details::get_reference((sequence_resolver[i])(ref));
        }

    private:
        all_seq_tuple_t _scope;
        size_t _index;
        
        void seek()
        {
            while( (_index < union_size())
                && !at(_index).in_range() )
            {
                ++_index;
                if(_index < union_size())
                    at(_index).start();
            }
        }

        template <size_t I>
        static base_t& at_i(all_seq_tuple_t& tup) noexcept
        {
            return details::get_reference(std::get<I>(tup));
        }

        template <size_t ... I>
        constexpr static ptr_holder_t mk_arr(std::index_sequence<I...>) noexcept
        {
            return ptr_holder_t{ at_i<I> ... };
        }

        constexpr static ptr_holder_t sequence_resolver
            = { mk_arr(std::make_index_sequence<sizeof...(Rx)>{}) };
    };

    template <class ... Tx>
    struct UnionAllSequenceFactory : FactoryBase
    {
        template <class ...Ux>
        explicit constexpr UnionAllSequenceFactory( Ux&& ... rx ) noexcept
            : _right(std::make_tuple(std::forward<Ux>(rx)...)) //make_tuple is important to get rid-off any references 
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return UnionAllSequence<
                        typename Src::element_t,
                        Src, decltype(details::get_reference(rx).compound())... >(
                            std::make_tuple(std::move(src),
                                details::get_reference(rx).compound() ... )
                    );}, _right);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            
            return
                std::apply([&](auto&& ... rx) ->decltype(auto){
                    return UnionAllSequence<
                        typename Src::element_t,
                        Src, decltype(std::move(details::get_reference(rx)).compound())... >(
                            std::make_tuple(std::move(src),
                                //to force movement semantic tell the reference use &&
                                std::move(details::get_reference(rx)).compound() ... )
                    );}, std::move(_right));
        }

        /** This factory can be used as root-level lazy range */
        constexpr auto compound() const& noexcept
        {
            static_assert(
                sizeof...(Tx) > 1,
                "specify 2 or more sources in UnionAllSequenceFactory<Tx...>");

            using all_factories_t = decltype(_right);
            using zero_sequence_t = details::sequence_type_t< std::tuple_element_t<0, all_factories_t> >;
            using element_t = typename details::dereference_t<zero_sequence_t>::element_t;

            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return UnionAllSequence<
                        element_t,
                        decltype(details::get_reference(rx).compound())... >(
                            std::make_tuple( 
                                details::get_reference(rx).compound() ... )
                    );}, _right);
        }

        constexpr auto compound() && noexcept
        {
            static_assert(
                sizeof...(Tx) > 1,
                "specify 2 or more sources in UnionAllSequenceFactory<Tx...>");

            using all_factories_t = decltype(_right);
            using zero_sequence_t = details::sequence_type_t< std::tuple_element_t<0, all_factories_t> >;
            using element_t = typename details::dereference_t<zero_sequence_t>::element_t;

            return
                std::apply([&](auto&& ... rx) ->decltype(auto){
                    return UnionAllSequence<
                        element_t,
                        decltype(std::move(details::get_reference(rx)).compound())... >(
                            std::make_tuple( 
                                //to force movement semantic tell the reference use &&
                                std::move(details::get_reference(rx)).compound() ... )
                    );}, std::move(_right));
        }
    private:
        std::tuple<std::decay_t<Tx> ...> _right;
    };

    template<class... Tx> UnionAllSequenceFactory(Tx...) -> UnionAllSequenceFactory<Tx...>;

} //ns:OP

#endif //_OP_FLUR_UNIONALL__H_
