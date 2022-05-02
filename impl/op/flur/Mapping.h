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
            , _applicator(f)
        {
        }
        
        bool is_sequence_ordered() const override
        {
            return keep_mapping_c 
                && details::get_reference(_src).is_sequence_ordered();
        }

        virtual void start()
        {
            details::get_reference(_src).start();
        }
        virtual bool in_range() const
        {
            return details::get_reference(_src).in_range();
        }
        virtual element_t current() const
        {
            return _applicator(details::get_reference(_src).current());
        }
        virtual void next()
        {
            details::get_reference(_src).next();
        }

        Src _src;
        F _applicator;
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
                OrderedSequence<result_t>/*Ordered sequenc is no garantee is_sequence_ordered() is true*/,
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
