#pragma once
#ifndef _OP_FLUR_UNIONALL__H_
#define _OP_FLUR_UNIONALL__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/Join.h>

namespace OP::flur
{
    /**
    *   Implement sequence to unite compile time defined tuple of input sequences.
    * (In compare with UnionAllQueueSequence that allows form input set at runtime)
    */
    template <class Elm, class ...Rx>
    struct UnionAllSequence : public Sequence<Elm>
    {
        using base_t = Sequence<Elm>;
        using element_t = typename base_t::element_t;

        using all_seq_tuple_t = std::tuple< Rx... >;
        using getter_t = base_t&(*)(all_seq_tuple_t&);
        //allows convert compile-time indexing to runtime at(i)
        using ptr_holder_t = std::array<getter_t, sizeof ... (Rx)>;

        template <class Tupl>
        constexpr UnionAllSequence(Tupl&& rx) noexcept
            : _scope( std::forward<Tupl>(rx) )
            , _index(sizeof...(Rx))
        {
        }

        /** Union all doesn't support ordering */
        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return false;
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
        constexpr size_t union_size() const
        {
            return sizeof ... (Rx);
        }
        base_t& at(size_t i)
        {
            return details::get_reference((sequence_resolver[i])(_scope));
        }
        const base_t& at(size_t i) const
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
        static base_t& at_i(all_seq_tuple_t& tup)
        {
            return std::get<I>(tup);
        }

        template <size_t ... I>
        constexpr static ptr_holder_t mk_arr(std::index_sequence<I...>)
        {
            return ptr_holder_t{ at_i<I> ... };
        }
        constexpr static ptr_holder_t sequence_resolver
            = { mk_arr(std::make_index_sequence<sizeof...(Rx)>{}) };
    };

    template <class ... Rx>
    struct UnionAllSequenceFactory : FactoryBase
    {
        constexpr UnionAllSequenceFactory( Rx&& ... rx ) noexcept
            :  _right(std::make_tuple(std::forward<Rx>(rx)...))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {

            using left_container_t = details::sequence_type_t<Src>;

            static_assert(
                std::conjunction_v<
                    std::is_same< typename left_container_t::element_t, typename details::sequence_type_t<Rx>::element_t >...>,
                "union-all must operate on sequences producing same type elements");

            return
                std::apply([&](const auto& ... rx) ->decltype(auto){
                    return UnionAllSequence<
                        typename left_container_t::element_t,
                        left_container_t, details::sequence_type_t<Rx>... >(
                            std::make_tuple(std::move(src),
                                details::get_reference(rx).compound() ... )
                    );}, _right);
        }
    private:
        std::tuple< Rx ...> _right;
    };

} //ns:OP

#endif //_OP_FLUR_UNIONALL__H_
