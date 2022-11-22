#pragma once
#ifndef _OP_FLUR_MAPPING__H_
#define _OP_FLUR_MAPPING__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/ftraits.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /**
    * Mapping converts one sequence to another by applying function to source element.
    * \tparam Src - source sequnce to convert
    */
    template <class Base, class Src, class F, bool keep_mapping_c>
    struct Mapping : public Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using base_t::ordered_c;

        template <class U>
        constexpr Mapping(Src&& src, U f) noexcept
            : _src(std::move(src))
            , _applicator(std::move(f))
        {
        }
        
        bool is_sequence_ordered() const override
        {
            return keep_mapping_c 
                && details::get_reference(_src).is_sequence_ordered();
        }

        void start() override
        {
            details::get_reference(_src).start();
        }
        bool in_range() const override
        {
            return details::get_reference(_src).in_range();
        }
        element_t current() const override
        {
            return _applicator(details::get_reference(_src).current());
        }
        void next() override
        {
            details::get_reference(_src).next();
        }

        Src _src;
        F _applicator;
    };
    
    /** Helper class allows to reduce allocattions number when mapping result
    * is a heap consuming entity. For example, following code allocates memory for 
    `std::string` several times:
     \code
        src::of_iota(5, 7)
        >> then::keep_order_mapping([](auto n)->std::string{ return std::to_string(n); }
    \endcode
    To optimize it we can add state-full functor:\code
        src::of_iota(5, 7)
        >> then::keep_order_mapping(
            ReusableMapBuffer([](auto n, std::string& already_existing) -> void
            { 
                std::format_to(
                    std::back_inserter(already_existing), "{}", n); 
            })
        );
    \endcode

    */
    template <class F>
    struct ReusableMapBuffer
    {
        using traits_t = OP::utils::function_traits<F>;
        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using to_t = std::decay_t<typename traits_t::template arg_i<1>>;
        
        ReusableMapBuffer(F f) 
            : _f(std::move(f)) 
        {
        }

        const to_t& operator()(const from_t& from) const
        {
            _f(from, _entry);
            return _entry;
        }

        F _f;
        mutable to_t _entry;
    };
    

    /**
    *
    * \tparam keep_order - true if function keeps order. NOTE! keep_order does not mean 'ordered', it just state 
    *   if source ordered then result just keep order.
    */
    template < class F, bool keep_order_c = false >
    struct MappingFactory : FactoryBase
    {
        constexpr MappingFactory(F f) noexcept
            : _applicator(f)
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using input_t = std::decay_t<details::unpack_t<Src>>;
            using src_container_t = std::decay_t < 
                details::dereference_t<
                    decltype(details::get_reference(std::declval< input_t >()))
                > 
            >;

            using result_t = decltype( 
                _applicator(std::declval< src_container_t >().current()) 
            );

            using target_sequence_base_t = std::conditional_t<keep_order_c,
                OrderedSequence<result_t>/*Ordered sequenc is no guarantee is_sequence_ordered() is true*/,
                Sequence<result_t>
            >;

            return Mapping<target_sequence_base_t, input_t, F, keep_order_c>(
                std::move(src), 
                _applicator);
        }
        F _applicator;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_MAPPING__H_
