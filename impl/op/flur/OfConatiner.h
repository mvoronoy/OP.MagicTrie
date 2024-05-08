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
#include <op/flur/Join.h>

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
    struct OfContainer : Base
    {
        using container_t = details::dereference_t<V>;
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using iterator = decltype(std::begin(std::declval<container_t>()));//typename container_t::const_iterator;

        constexpr OfContainer(V&& v) noexcept
            :_v(std::move(v))
        {
            _i = details::get_reference(_v).end();
        }

        constexpr OfContainer(const V& v) noexcept
            :_v(v)
        {
            _i = details::get_reference(_v).end();
        }
        
        constexpr OfContainer(OfContainer&&) noexcept = default;

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
        V _v;
        iterator _i;
    };

    template <class Container>
    using OfUnorderedContainer = OfContainer<Container, 
                                Sequence< details::element_of_container_t<Container> > >; 

    template <class Container>
    using OfOrderedContainer = OfContainer<Container, 
                                OrderedSequence< details::element_of_container_t<Container> > >;

    //for all STL that supports lower_bound
    template <class Container>
    struct OfLowerBoundContainer : 
        OfContainer<Container, 
            OrderedSequenceOptimizedJoin< details::element_of_container_t<Container> > >
    {
        using base_t = OfContainer<Container, 
            OrderedSequenceOptimizedJoin< details::element_of_container_t<Container> > >;
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
            std::conditional_t< ::OP::flur::details::is_ordered_v<details::dereference_t<Container>> 
            , OfLowerBoundContainer< Container >
            , OfUnorderedContainer< Container >
            >;
    } //ns:details

    /**
    * Adapter for STL containers or user defined that support std::begin<V> and std::end<V>
    * Also V may be wrapped with: std::ref / std::cref
    */
    template <class V>
    struct OfContainerFactory : FactoryBase
    {
        using holder_t = std::decay_t < V >;
        using container_t = details::dereference_t<holder_t>;

        /** Detects if container can support OrderedSequence or Sequence */
        using sequence_t = details::detect_sequence_t<holder_t>;

        constexpr OfContainerFactory(holder_t&& v) noexcept
            :_v(std::move(v)) {}
        constexpr OfContainerFactory(const holder_t& v) noexcept
            :_v(v) {}

        constexpr auto compound() const& noexcept
        {
            return sequence_t(_v);
        }
        constexpr auto compound() && noexcept
        {
            return sequence_t(std::move(_v));
        }
        holder_t _v;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFCONTAINER__H_
