#pragma once
#ifndef _OP_FLUR_OFCONTAINER__H_
#define _OP_FLUR_OFCONTAINER__H_

#include <functional>
#include <memory>
#include <optional>

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
        using iterator = decltype(std::declval<const container_t&>().begin());//typename container_t::const_iterator;
        constexpr OfContainer(V v) noexcept
            :_v(std::move(v))
        {
            _i = details::get_reference(_v).end();
        }
        constexpr OfContainer(OfContainer&&) noexcept = default;

        virtual void start()
        {
            _i = details::get_reference(_v).begin();
        }
        virtual bool in_range() const
        {
            return _i != details::get_reference(_v).end();
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
    namespace details
    {
        /** Detects if container can support OrderedSequence or Sequence */
        template <class Container >
        using detect_sequence_base_t = 
            std::conditional_t<
                OP::utils::is_generic<Container, std::map>::value ||
                OP::utils::is_generic<Container, std::set>::value
                , OrderedSequence< decltype(*std::begin(std::declval<const Container&>())) >
                , Sequence< decltype(*std::begin(std::declval<const Container&>())) >
            >;
    }
    /**
    * Adapter for STL containers or user defined that support std::begin<V> and std::end<V>
    * Also V may be wrapped with: std::ref / std::cref
    */
    template <class V>
    struct OfContainerFactory : FactoryBase
    {
        using holder_t = std::decay_t < V >;
        using container_t = details::dereference_t<V>;

        constexpr OfContainerFactory(holder_t v) noexcept
            :_v(std::move(v)) {}

        constexpr auto compound() const& noexcept
        {
            using element_t = decltype(*std::declval<const container_t&>().begin());
            using base_t = details::detect_sequence_base_t<container_t>;
            using result_t = OfContainer<holder_t, base_t>;

            return result_t(_v);
        }
        constexpr auto compound() && noexcept
        {
            using element_t = decltype(*std::declval<container_t&&>().begin());
            using base_t = details::detect_sequence_base_t<container_t>;
            using result_t = OfContainer<holder_t, base_t>;

            return result_t(std::move(_v));
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
        using container_t = details::dereference_t<V>;
        using base_t = 
            details::detect_sequence_base_t<container_t>;
            
        using result_t = OfContainer<V, base_t>;
        return result_t(std::move(v));
    }

    template <class V>
    auto cref_container(const V &v)
    {
        using container_t = details::dereference_t<V>;
        using base_t = 
            details::detect_sequence_base_t<container_t>;
            
        using result_t = OfContainer<decltype(std::cref(v)), base_t>;
        return result_t(std::cref(v));
    }
} //ns:flur
} //ns:OP

#endif //_OP_FLUR_OFCONTAINER__H_
