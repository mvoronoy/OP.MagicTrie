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
    template <class Base, class Src, class F>
    struct Mapping : public Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        template <class U>
        constexpr Mapping(Src&& src, U f) noexcept
            : _src(std::move(src))
            , _applicator(f)
        {
        }
        virtual void start()
        {
            _src.start();
        }
        virtual bool in_range() const
        {
            return _src.in_range();
        }
        virtual element_t current() const
        {
            return _applicator(_src.current());
        }
        virtual void next()
        {
            _src.next();
        }

        Src _src;
        F _applicator;
    };

    /**
    *
    * \tparam keep_order - true if function keeps order. NOTE! keep_order does not mean 'ordered', it just state 
    *   if source ordered then result just keep order.
    */
    template < class F, bool keep_order = false >
    struct MappingFactory : FactoryBase
    {
        using ftraits_t = OP::utils::function_traits< F >;

        constexpr MappingFactory(F f) noexcept
            : _applicator(f)
        {
        }
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_container_t = std::decay_t<details::unpack_t<Src>>;
            using result_t = decltype( _applicator(std::declval< std::decay_t <src_container_t> >().current()) );

            using target_sequence_base_t = std::conditional_t<keep_order && src_container_t::ordered_c,
                Sequence<result_t>,
                OrderedSequence<result_t>
            >;

            return Mapping<target_sequence_base_t, src_container_t, F>(
                std::move(details::unpack(std::move(src))), 
                _applicator);
        }
        F _applicator;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_MAPPING__H_
