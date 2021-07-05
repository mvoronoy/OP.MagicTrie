#pragma once
#ifndef _OP_FLUR_TYPEDEFS__H_
#define _OP_FLUR_TYPEDEFS__H_

#include <functional>
#include <memory>

#include <op/common/Utils.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    namespace details
    {
        template <typename U>
        U& get_reference(std::reference_wrapper<U> u) {
            return (U&)u.get();
        }
        template <typename U>
        const U& get_reference(std::reference_wrapper<U const> u) {
            return (U&)u.get();
        }

        template <typename U>
        constexpr U& get_reference(U& u) noexcept{
            return u;
        }
        template <typename U>
        constexpr const U& get_reference(const U& u) noexcept {
            return u;
        }

        template <typename U, typename ... Ux>
        auto get_reference(std::unique_ptr<U, Ux ...>& u) -> U& {
            return *u.get();
        }
        template <typename U, typename ... Ux>
        const U& get_reference(const std::unique_ptr< U, Ux ...>& u) {
            return *u.get();
        }

        template <typename U>
        auto get_reference(std::shared_ptr<U> u) -> U& {
            return *u;
        }

        template <class Value>
        using container_type_t = std::decay_t <
            decltype(details::get_reference(std::declval< Value >())) >;

        /** Defines introspection type for all with less (<) operator defined */
        template < typename T, typename U >
        using less_comparable_t = decltype(std::declval<T&>() < std::declval<U&>());

        template < typename T, typename U, typename = std::void_t<> >
        struct is_less_comparable : std::false_type
        {};

        template < typename T, typename U >
        struct is_less_comparable < T, U, std::void_t< less_comparable_t<T, U> > >
            : std::is_same < less_comparable_t<T, U>, bool >
        {};
        
        template <class SomeContainer>
        constexpr auto unpack(SomeContainer&& co) noexcept
        {
            if constexpr (std::is_base_of_v<FactoryBase, SomeContainer>)
                return unpack(std::move(co.compound()));
            else
                return std::move(co);
        }
        template <class SomeContainer>
        constexpr auto unpack(const SomeContainer& co) noexcept
        {
            if constexpr (std::is_base_of_v<FactoryBase, SomeContainer>)
                return unpack(std::move(co.compound()));
            else
                return co;//return (const SomeContainer&)co;
        }
        template <class SomeContainer>
        using unpack_t = decltype(unpack(std::declval< SomeContainer>()));

        
        //Compound togethere all factories to single construction
        template <class Tuple, std::size_t I = std::tuple_size<Tuple>::value >
        constexpr decltype(auto) compound_impl(Tuple& t) noexcept
        {
            if constexpr (I == 1)
            {
                return unpack(std::get<0>(t));
            }
            else
                return unpack(std::move(std::get<I - 1>(t).compound(std::move(compound_impl<Tuple, I - 1>(t)))));
        }


    }//ns::details

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_TYPEDEFS__H_