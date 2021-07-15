#pragma once
#ifndef _OP_FLUR_OFCONTAINER__H_
#define _OP_FLUR_OFCONTAINER__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/common/Utils.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/SimpleFactory.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /**
    *   Create conatiner from any stl based container 
    * \tparam V - source container (that supports std::begin / std::end pair) 
    * \tparam Base - for std::map and std::set allows allows inherit from OrderedSequence. 
    *               All other suppose Sequence
    */
    template <class V, class Base>
    struct OfContainer : Base
    {
        using container_t = details::container_type_t<V>;
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using iterator = decltype(std::begin(std::declval<container_t>()));//typename container_t::const_iterator;
        constexpr OfContainer(V v) noexcept
            :_v(std::move(v))
            , _i(std::end(details::get_reference(_v)))
        {}
        
        virtual void start()
        {
            _i = std::begin(details::get_reference(_v));
        }
        virtual bool in_range() const
        {
            return _i != std::end(details::get_reference(_v));
        }
        virtual element_t current() const
        {
            return *_i;
        }
        virtual void next()
        {
            ++_i;
        }
    private:
        V _v;
        iterator _i;
    };

    /**
    * Adapter for STL containers or user defined that support std::begin<V> and std::end<V>
    * Also V may be wrapped with: std::ref / std::cref
    */
    template <class V>
    struct OfContainerFactory : FactoryBase
    {
        using holder_t = std::decay_t < V >;
        using container_t = details::container_type_t<V>;

        constexpr OfContainerFactory(holder_t v) noexcept
            :_v(std::move(v)) {}

        constexpr auto compound() const noexcept
        {
            using element_t = decltype(*std::begin(std::declval<const container_t&>()));
            using base_t = std::conditional_t<
                OP::utils::is_generic<container_t, std::map>::value ||
                OP::utils::is_generic<container_t, std::set>::value
                , OrderedSequence< element_t >
                , Sequence< element_t >
            >;
                
            using result_t = OfContainer<holder_t, base_t>;
            return result_t(_v);
        }
        holder_t _v;
    };
    /** The way of create OfContainer without factory and moving owning to OfContainer. 
    * Useful when need create some intermedia step with fixed items set.
    * For example as result of flat_mapping:\code
    * src::of_iota(1, 5)
    *   >>  then::flat_mapping([](auto i) {
    *           return rref_container( std::vector<std::string>{
    *               "a" + std::to_string(i),
    *               "b" + std::to_string(i),
    *               "c" + std::to_string(i)});
    *       })
    *   ;
    * \endcode
    * Create sequence of strings:[ "a1", "b1", "c1", "a2", "b2", "c2", "a3", "b3", "c3", "a4", "b4", "c4"]
    */
    template <class V>
    auto rref_container(V &&v)
    {
        using container_t = details::container_type_t<V>;
        using element_t = decltype(*std::begin(std::declval<const container_t&>()));
        using base_t = Sequence< element_t >;
            
        using result_t = OfContainer<V, base_t>;
        return result_t(std::move(v));
    }

    template <class V>
    auto cref_container(const V &v)
    {
        using container_t = details::container_type_t<V>;
        using element_t = decltype(*std::begin(std::declval<const container_t&>()));
        using base_t = Sequence< element_t >;
            
        using result_t = OfContainer<decltype(std::cref(v)), base_t>;
        return result_t(std::cref(v));
    }
} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFCONTAINER__H_
