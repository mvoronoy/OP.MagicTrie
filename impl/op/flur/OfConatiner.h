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
        using iterator = typename container_t::const_iterator;
        constexpr OfContainer(V v) noexcept
            :_v(v)
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


} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFCONTAINER__H_
