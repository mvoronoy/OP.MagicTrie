#pragma once
#ifndef _OP_FLUR_TYPEDEFS__H_
#define _OP_FLUR_TYPEDEFS__H_

#include <functional>
#include <memory>

#include <op/common/Utils.h>
#include <op/common/ftraits.h>
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
        template <typename U>
        constexpr U&& get_reference(U&& u) noexcept{
            return std::move(u);
        }

        template <typename U, typename ... Ux>
        U& get_reference(std::unique_ptr<U, Ux ...>& u) {
            return *u;
        }
        template <typename U, typename ... Ux>
        const U& get_reference(const std::unique_ptr< U, Ux ...>& u) {
            return *u;
        }

        template <typename U>
        U& get_reference(std::shared_ptr<U> u) {
            return *u;
        }

        template <class Value>
        using dereference_t = std::decay_t <
            decltype(details::get_reference(std::declval< const Value& >())) >;

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
        constexpr decltype(auto) unpack(SomeContainer&& co) noexcept
        {
            if constexpr (std::is_base_of_v<FactoryBase, dereference_t<SomeContainer> >)
                return std::move(get_reference(co).compound());
            else
                return std::move(co);
        }
        template <class SomeContainer>
        constexpr decltype(auto) unpack(const SomeContainer& co) noexcept
        {
            if constexpr (std::is_base_of_v<FactoryBase, dereference_t<SomeContainer> >)
                return std::move(get_reference(co).compound());
            else
                return co;
        }
        template <class SomeContainer>
        using unpack_t = decltype(unpack(std::declval< SomeContainer>()));

        
        //Compound togethere all factories to single construction
        template <class Tuple, std::size_t I = std::tuple_size<Tuple>::value >
        constexpr decltype(auto) compound_impl(const Tuple& t) noexcept
        {   //scan tuple in reverse order
            if constexpr (I == 1)
            { //at zero level must reside no-arg `compound` implementation
                return get_reference(std::get<0>(t)).compound();
            }
            else // non-zero level invokes recursively `compound` with previous layer result
            {
                return 
                    get_reference(std::get<I - 1>(t)).compound(
                        std::move(compound_impl<Tuple, I - 1>(t)));
            }
        }

        template <class Tuple, std::size_t I = std::tuple_size<Tuple>::value >
        constexpr auto compound_impl(Tuple&& t) noexcept
        {   //scan tuple in reverse order
            if constexpr (I == 1)
            { //at zero level must reside no-arg `compound` implementation
                return get_reference(std::move(std::get<0>(t))).compound();
            }
            else // non-zero level invokes recursively `compound` with previous layer result
            {
                return 
                    get_reference(std::move(std::get<I - 1>(t))).compound( std::move(
                        compound_impl<Tuple, I - 1>(std::move(t))) );
            }
        }
        
        template <class T>
        class sequence_type
        {
            using _cleant_t = dereference_t< T >;
            

            template <typename C> 
            static typename OP::utils::function_traits<decltype(&C::compound)>::result_t test( decltype(& C::compound) ) ;

            template <typename C> 
            static _cleant_t& test(...);
        public:
            //using type = std::decay_t< decltype( test<_cleant_t>(nullptr) )>;
            using type = std::decay_t< unpack_t<T> >;
        };

        template <class Value>
        using sequence_type_t = typename sequence_type<Value>::type;


    }//ns::details

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_TYPEDEFS__H_