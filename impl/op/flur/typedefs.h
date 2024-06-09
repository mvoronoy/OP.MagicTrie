#pragma once
#ifndef _OP_FLUR_TYPEDEFS__H_
#define _OP_FLUR_TYPEDEFS__H_

#include <functional>
#include <memory>
#include <optional>
#include <array>
#include <map>
#include <set>

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
    U& get_reference(std::reference_wrapper<U> u) noexcept 
    {
        return u.get();
    }

    template <typename U>
    U const& get_reference(std::reference_wrapper<U const> u) noexcept 
    {
        return u.get();
    }

    template <typename U>
    constexpr U& get_reference(U& u) noexcept 
    {
        return u;
    }
    
    template <typename U>
    constexpr const U& get_reference(const U& u) noexcept 
    {
        return u;
    }
    
    template <typename U>
    constexpr U&& get_reference(U&& u) noexcept 
    {
        return std::forward<U>(u);
    }

    template <typename U, typename ... Ux>
    U& get_reference(std::unique_ptr<U, Ux ...>& u) noexcept 
    {
        return *u;
    }

    template <typename U, typename ... Ux>
    const U& get_reference(const std::unique_ptr< U, Ux ...>& u) noexcept 
    {
        return *u;
    }

    template <typename U>
    decltype(auto) get_reference(const std::shared_ptr<U>& u) noexcept 
    {
        return *u.get();
    }

    template <typename U>
    decltype(auto) get_reference(std::shared_ptr<U>& u) 
    {
        return *u.get();
    }

    template <typename U>
    U& get_reference(std::shared_ptr<U>&& u) noexcept 
    {
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
        if constexpr (std::is_base_of_v<FactoryBase, std::decay_t<SomeContainer>>)
            return std::forward<SomeContainer>(co).compound();
        else
            return std::forward<SomeContainer>(co);
    }

    template <class SomeContainer>
    constexpr decltype(auto) unpack(std::shared_ptr<SomeContainer> co) noexcept
    {
        if constexpr (std::is_base_of_v<FactoryBase, std::decay_t<SomeContainer>>)
            return co->compound();
        else
            return co;
    }

    template <class SomeContainer>
    using unpack_t = decltype(unpack(std::declval<SomeContainer>()));

    //Compound together all factories to single construction
    template <class FactoriesTuple, std::size_t I = std::tuple_size<FactoriesTuple>::value >
    constexpr decltype(auto) compound_impl(const FactoriesTuple& t) noexcept
    {   //scan tuple in reverse order
        static_assert(std::tuple_size_v<FactoriesTuple> > 0, "invalid factory set to compound");
        if constexpr (I == 1)
        { //at zero level must place no-arg `compound` implementation
            return get_reference(std::get<0>(t)).compound();
        }
        else // non-zero level invokes recursively `compound` with previous layer result
        {
            return
                get_reference(std::get<I - 1>(t)).compound(
                    compound_impl<FactoriesTuple, I - 1>(t));
        }
    }

    template <class FactoriesTuple, std::size_t I = std::tuple_size<FactoriesTuple>::value >
    constexpr decltype(auto) compound_impl(FactoriesTuple&& t) noexcept
    {   //scan tuple in reverse order

        // In both following branches `std::move` used only as cast to T&& 
        if constexpr (I == 1)
        { //at zero level must reside no-arg `compound` implementation
            return std::move(get_reference(std::get<0>(t))).compound();
        }
        else // non-zero level invokes recursively `compound` with previous layer result
        {
            return std::move(get_reference(std::get<I - 1>(t)))
                .compound(compound_impl<FactoriesTuple, I - 1>(std::move(t)));
        }
    }

    /** \brief Detect type of Sequence with respect to rules of sequence creation.
    *
    *   \tparam T can be either FactoryBase, FactoryBase wrapped with all standards
    *   wrappers (like std::shared_ptr, std:reference_wrapper...) or even Sequence itself.
    *
    *   For some types (for example `OfContainerFactory`) this definition can provide different
    *   types depending on (T& or T&&) has been used.
    */
    template <class T>
    using sequence_type_t = std::decay_t< unpack_t<T> >;

    /** Detect type of element produced by Sequence iteration */
    template <class Value>
    using sequence_element_type_t = typename dereference_t< sequence_type_t<Value> >::element_t;

    /** Generic check if arbitrary container (mostly about from std:: namespace) is ordered.
    * By default it false.
    * Make specification or overload if you need more specific behavior for your purpose
    */
    template <class TArbitrary>
    constexpr std::false_type is_ordered(const TArbitrary&) noexcept
    {
        return std::false_type{};
    }

    /** Indicates that generic std::map is ordered */
    template <class ...Tx>
    constexpr std::true_type is_ordered(const std::map<Tx...>&) noexcept
    {
        return std::true_type{};
    }

    /** Indicates that generic std::multimap is ordered */
    template <class ...Tx>
    constexpr std::true_type is_ordered(const std::multimap<Tx...>&) noexcept
    {
        return std::true_type{};
    }

    /** Indicates that generic std::set is ordered */
    template <class ...Tx>
    constexpr std::true_type is_ordered(const std::set<Tx...>&) noexcept
    {
        return std::true_type{};
    }

    /** Indicates that generic std::multiset is ordered */
    template <class ...Tx>
    constexpr std::true_type is_ordered(const std::multiset<Tx...>&) noexcept
    {
        return std::true_type{};
    }

    /** Indicates that 1 element array is always ordered */
    template <class T>
    constexpr std::true_type is_ordered(const std::array<T, 1>&) noexcept
    {
        return std::true_type{};
    }

    /** Check if container `TContainer` is ordered. Definition just wraps reference to a
    *   custom function `constexpr std::<true/false>_type is_ordered(const TContainer&) noexcept`
    */
    template <class TContainer>
    constexpr inline bool is_ordered_v = decltype(
        is_ordered(std::declval<const std::decay_t<TContainer>&>()))::value;

} //ns:OP::flur::details

#endif //_OP_FLUR_TYPEDEFS__H_