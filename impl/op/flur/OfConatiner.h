#pragma once
#ifndef _OP_FLUR_OFCONTAINER__H_
#define _OP_FLUR_OFCONTAINER__H_

#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <set>
#include <vector>

#include <op/common/Utils.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{

    /**
    *   Create conatiner from any stl based container 
    * \tparam TContainer - source container (that supports std::begin / std::end pair). This value can be 
    *  additionally wrapp with one of: `std::reference_wrapper`, `std::shared_ptr`, `std::unique_ptr`.
    *
    * \tparam Base - for std::map and std::set allows allows inherit from OrderedSequence. 
    *               All other containers suppose using `Sequence` interface.
    */
    template <class TContainer, class Base>
    struct OfContainerSequence : Base
    {
        using unref_container_t = details::dereference_t<TContainer>;
        constexpr static bool is_wrap =
            !std::is_same_v<
            std::decay_t<TContainer>, std::decay_t<unref_container_t>>;

        using container_t = std::conditional_t<is_wrap,
            std::decay_t<TContainer>, TContainer>; //keep reference only for raw container

        using base_t = Base;
        using element_t = typename base_t::element_t;
        using iterator = decltype(std::begin(std::declval<unref_container_t>()));

        template <class U>
        constexpr OfContainerSequence(int, U&& v) noexcept
            :_v(std::forward<U>(v))
        {
            _i = details::get_reference(_v).end();
        }

        constexpr OfContainerSequence(const OfContainerSequence& other)
            : _v(other._v)
        {
            _i = details::get_reference(_v).end();
        }

        constexpr OfContainerSequence(OfContainerSequence&& other) noexcept
            : _v(std::move(other._v))
        {
            _i = details::get_reference(_v).end();
        }

        virtual void start() override
        {
            _i = details::get_reference(_v).begin();
        }
        
        virtual bool in_range() const override
        {
            return _i != details::get_reference(_v).end();
        }
        
        virtual element_t current() const override
        {
            return *_i;
        }

        virtual void next() override
        {
            ++_i;
        }

    protected:
        TContainer& container()
        {
            return _v;
        }
        iterator& pos() 
        {
            return _i;
        }
    private:
        container_t _v;
        iterator _i;
    };

    template <class Container, class Base>
    using OfUnorderedContainerSequence = OfContainerSequence<Container, Base>; 

    namespace details
    {
        /** Just generic call of container::lower_bound, when supported by STL.
        * Allows customize this in edge cases (like std::array<T, 1>...)
        */
        template <class Container, class T>
        decltype(auto) lower_bound_impl(const Container& container, const T& key) 
        {
            return container.lower_bound(key);
        }

        /** Special case for std::array<T, 1> */
        template <class T>
        decltype(auto) lower_bound_impl(const std::array<T, 1>& container, const T& key) 
        {
            if(!(container[0] < key)) //per spec - `lower_bound` defined as first non-less.
                return container.begin();
            else
                return container.end();
        }

        /** Special case for std::array<T, 0> */
        template <class T>
        decltype(auto) lower_bound_impl(const std::array<T, 0>& container, const T& ) 
        {
            return container.end();
        }
    }

    //template <class Container, class Base>
    //using OfOrderedContainer = OfContainerSequence<Container, 
    //                            OrderedSequence< details::element_of_container_t<Container> > >;

    //for all STL that supports lower_bound
    template <class Container, class Base>
    struct OfLowerBoundContainerSequence : 
        OfContainerSequence<Container, Base>
    {
        using base_t = OfContainerSequence<Container, Base>;
        using element_t = typename base_t::element_t;
        using base_t::base_t;
        
        template <class Item>
        const auto& extract_key(const Item& it)
        {
            return it;
        }

        template <class A, class B>
        const auto& extract_key(const std::pair<A, B>& it)
        {
            return it.first;
        }

        void lower_bound(const element_t& other) override
        {
            base_t::pos() = details::lower_bound_impl(
                    details::get_reference(base_t::container()),
                    extract_key(other) 
            );
        }
    };

    namespace details
    {

        template <class Container>
        using iterator_reference_type_t = decltype(*std::begin(
            std::declval<details::dereference_t<Container>>()));

        /** \brief detect type of STL container's contained element
        * To get element type reference operator `*` is applied to result of std::begin of `Container`
        */ 
        template <class Container, bool force_by_value >
        using element_of_container_t = std::conditional_t<force_by_value,
            std::decay_t<iterator_reference_type_t<Container>>, iterator_reference_type_t<Container>>;

        template <class Container, bool force_by_value>
        using detect_sequence_t = 
            std::conditional_t< ::OP::flur::details::is_ordered_v<details::dereference_t<std::decay_t<Container>> > 
            , OfLowerBoundContainerSequence< Container, 
                    OrderedSequenceOptimizedJoin<details::element_of_container_t<Container, force_by_value>> >
            , OfUnorderedContainerSequence< Container, 
                    Sequence<details::element_of_container_t<Container, force_by_value>> >
            >;
    } //ns:details

    /**
    * Adapter for STL containers or user defined that support std::begin<TContainer> and std::end<TContainer>
    * Also TContainer may be wrapped with: `std::ref` / `std::cref` or `std::shared_ptr`, `std::unique_ptr`.
    */
    template <class TContainer, auto ... options_c>
    struct OfContainerFactory : FactoryBase
    {
        using holder_t = std::decay_t< TContainer >;

        constexpr static inline bool force_by_value_c = OP::utils::any_of<options_c...>(Intrinsic::result_by_value);

        template <class U>
        constexpr OfContainerFactory(int, U&& u) noexcept
            :_v(std::forward<U>(u)) {}

        /** \brief create iterable sequence with const reference to specified container.
        *   When this factory is defined as l-reference then result sequence uses const-reference to
        *   the source container. That means life-time of factory must be bigger than result sequence.
        * Correct example: \code
        *   auto factory = OP::flur::src::of_container(std::vector{1, 2, 3});
        *   for(int x: factory) //implicitly call compound() for const reference, `factory` lives longer than sequence.
        *       std::cout << x <<'\n';   
        * \endcode
        *
        * Wrong example: \code
        *  auto wrong_sequence()
        *  {
        *       auto factory = OP::flur::src::of_container(std::vector{1, 2, 3});
        *       return factory.compound(); //return sequence, but factory is destroyed at exit. To fix use move semantic instead.     
        *       //Better: return std::move(factory).compound();
        *  }
        * \endcode
        */
        constexpr auto compound() const& noexcept
        {
            /** Detects if container can support OrderedSequence or Sequence */
            using sequence_t = details::detect_sequence_t<const holder_t&, force_by_value_c>;
            return sequence_t(0, _v);
        }
        /** \brief create iterable sequence that owns specified container (using move semantic).
        */
        constexpr auto compound() && noexcept
        {
            using sequence_t = details::detect_sequence_t<holder_t, force_by_value_c>;
            return sequence_t(0, std::move(_v));
        }
        holder_t _v;
    };
    // explicit deduction guide (not needed as of C++20)
    template<class T> OfContainerFactory(int, T) -> OfContainerFactory<T>;

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFCONTAINER__H_
