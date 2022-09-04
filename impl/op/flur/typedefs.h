#pragma once
#ifndef _OP_FLUR_TYPEDEFS__H_
#define _OP_FLUR_TYPEDEFS__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/common/Utils.h>
#include <op/common/ftraits.h>
#include <op/flur/FactoryBase.h>

namespace OP::flur::details
{
    template<typename T> struct is_shared_ptr : std::false_type {};
    template<typename T> struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

    template<typename ...T> struct is_unique_ptr : std::false_type {};
    template<typename ...T> struct is_unique_ptr<std::unique_ptr<T...>> : std::true_type {};

    template<typename T> struct is_optional : std::false_type {};
    template<typename T> struct is_optional<std::optional<T>> : std::true_type {};

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
        return std::forward<U>(u);
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
    decltype(auto) get_reference(const std::shared_ptr<U>& u) {
        return *u.get();
    }
    template <typename U>
    decltype(auto) get_reference(std::shared_ptr<U>& u) {
        return *u.get();
    }
    template <typename U>
    U& get_reference(std::shared_ptr<U>&& u) {
        //invoking this code means very-very bad lost control, used only for type-deduction only
        assert(false);
        return *u.get();
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
            return get_reference(co).compound();
        else
            return std::move(co);
    }
    template <class SomeContainer>
    constexpr decltype(auto) unpack(const SomeContainer& co) noexcept
    {
        if constexpr (std::is_base_of_v<FactoryBase, dereference_t<SomeContainer> >)
            return get_reference(co).compound();
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
        { //at zero level must place no-arg `compound` implementation
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
    constexpr decltype(auto) compound_impl(Tuple&& t) noexcept
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


} //ns:OP

#endif //_OP_FLUR_TYPEDEFS__H_