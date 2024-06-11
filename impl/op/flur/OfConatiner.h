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
    namespace details
    {
        template <class Container>
        using element_of_container_t = decltype(*std::begin(
            std::declval<details::dereference_t<Container>>()));
    }
    /**
    *   Create conatiner from any stl based container 
    * \tparam V - source container (that supports std::begin / std::end pair) 
    * \tparam Base - for std::map and std::set allows allows inherit from OrderedSequence. 
    *               All other suppose Sequence
    */
    template <class V, class Base>
    struct OfContainerSequence : Base
    {
        using unref_container_t = details::dereference_t<V>;
        constexpr static bool is_wrap =
            !std::is_same_v<
            std::decay_t<V>, std::decay_t<unref_container_t>>;

        using container_t = std::conditional_t<is_wrap,
            std::decay_t<V>, V>; //keep reference only for raw container

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
        V& container()
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
            base_t::pos() = details::get_reference(
                    base_t::container()).lower_bound( extract_key(other) );
        }
    };

    namespace details
    {

        template <class Container>
        using detect_sequence_t = 
            std::conditional_t< ::OP::flur::details::is_ordered_v<details::dereference_t<std::decay_t<Container>> > 
            , OfLowerBoundContainerSequence< Container, 
                    OrderedSequenceOptimizedJoin<details::element_of_container_t<Container>> >
            , OfUnorderedContainerSequence< Container, 
                    Sequence<details::element_of_container_t<Container>> >
            >;
    } //ns:details

    /**
    * Adapter for STL containers or user defined that support std::begin<V> and std::end<V>
    * Also V may be wrapped with: std::ref / std::cref
    */
    template <class V>
    struct OfContainerFactory : FactoryBase
    {
        using holder_t = std::decay_t< V >;

        template <class U>
        constexpr OfContainerFactory(int, U&& u) noexcept
            :_v(std::forward<U>(u)) {}

        constexpr auto compound() const& noexcept
        {
            /** Detects if container can support OrderedSequence or Sequence */
            using sequence_t = details::detect_sequence_t<const holder_t&>;
            return sequence_t(0, _v);
        }
        constexpr auto compound() && noexcept
        {
            using sequence_t = details::detect_sequence_t<holder_t>;
            return sequence_t(0, std::move(_v));
        }
        holder_t _v;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFCONTAINER__H_
