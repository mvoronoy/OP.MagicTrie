#pragma once

#ifndef _OP_FLUR__PROXY_H_
#define _OP_FLUR__PROXY_H_

#include <functional>
#include <memory>
#include <variant>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP::flur
{
    namespace details
    {
        //Check all classes produces similar elements and detects the type of element
        template <class ... TSequence>
        class ProxySequenceTraits
        {
            template <class ...Tx>
            using pick0_t = std::tuple_element_t<0, std::tuple<Tx...>>;

            template <class ...Tx>
            constexpr static bool is_valid() noexcept
            {
                using zero_t = sequence_element_type_t <dereference_t<pick0_t<Tx...>>>;
                if constexpr (sizeof...(Tx) > 1)
                    return (std::is_convertible_v<sequence_element_type_t<dereference_t<Tx>>, zero_t> && ...);
                else // only single T
                    return true;
            }
        public:
            static_assert(is_valid<TSequence...>(), "All parameters of proxy sequences must have the same element type");
            using element_t = sequence_element_type_t< pick0_t<TSequence...> >;
        };
    } //ns:details

    template <class ... TSequence>
    struct SequenceProxy : Sequence < typename details::ProxySequenceTraits< TSequence... >::element_t >
    {
        using this_t = SequenceProxy<TSequence...>;
        using base_t = Sequence < typename details::ProxySequenceTraits< TSequence... >::element_t >;
        using element_t = typename base_t::element_t;

        template <class T>
        constexpr SequenceProxy(T&& t) noexcept
            : _instance(std::forward<T>(t))
        {}

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const noexcept override
        {
            return std::visit(
                [](auto& inst) { 
                    return details::get_reference(inst).is_sequence_ordered(); },
                _instance);
        }

        void start() override
        {
            std::visit(
                [](auto& inst) { 
                    details::get_reference(inst).start(); },
                _instance);
        }

        bool in_range() const override
        {
            return std::visit(
                [](auto& inst) { 
                    return details::get_reference(inst).in_range(); },
                _instance);
        }

        element_t current() const override
        {
            return std::visit(
                [](auto& inst) ->element_t { 
                    return details::get_reference(inst).current(); },
                _instance);
        }

        void next() override
        {
            std::visit(
                [](auto& inst) { 
                    details::get_reference(inst).next(); },
                _instance);
        }

    private:
        using possible_sequence_impl_t = std::variant<TSequence...>;
        possible_sequence_impl_t _instance;
    };

    template <class ...TFactory>
    struct ProxyFactory : FactoryBase
    {
        using target_sequence_t = SequenceProxy<
            details::sequence_type_t<TFactory>... >;

        template <class TInstance>
        constexpr ProxyFactory(TInstance instance) noexcept
            : _factory_instance(std::move(instance))
        {
        }

        constexpr auto compound() const& noexcept
        {
            return std::visit([](const auto& factory){
                return target_sequence_t{ details::get_reference(factory).compound() };
                }, _factory_instance);
        }

        constexpr auto compound() && noexcept
        {
            return std::visit([](auto&& factory){
                return target_sequence_t{ details::get_reference(factory).compound() };
                }, std::move(_factory_instance));
        }

        template <class Src>
        constexpr auto compound(Src&& src) const& noexcept
        {
            return std::visit([&](const auto& factory){
                return target_sequence_t{ details::get_reference(factory).compound(std::move(src)) };
                }, _factory_instance);
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            return std::visit([&](const auto& factory){
                return target_sequence_t{ details::get_reference(factory).compound(std::move(src)) };
                }, std::move(_factory_instance));
        }
    private:
        std::variant<TFactory...> _factory_instance;
    };

}//ns:OP::flur

#endif //_OP_FLUR__PROXY_H_
