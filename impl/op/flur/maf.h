#pragma once
#ifndef _OP_FLUR_MAF__H_
#define _OP_FLUR_MAF__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/ftraits.h>
#include <op/flur/Filter.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /**
    * Mapping and filter by one opertion 
    * \tparam Src - source sequnce to convert
    * \tparam F functor with the signature `bool(typename Src::element_t, <desired-mapped-type> &result)`
    *       Note that implementation assumes that <desired-mapped-type> is default constructible
    */
    template <class Base, class Src, class F, bool keep_order_mapping_c>
    struct MaF : public Base
    {
        using base_t = Base;

        using traits_t = OP::utils::function_traits<F>;
        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using mapped_t = std::decay_t<typename traits_t::template arg_i<1>>;

        using element_t = typename base_t::element_t;//?mapped_t
        static_assert(std::is_same_v<mapped_t, std::decay_t<element_t>>, 
            "compile time de-sync of element types, F must produce compatible type");

        template <class U>
        constexpr MaF(Src&& src, U f) noexcept
            : _src(std::move(src))
            , _predicate(std::move(f))
            , _end (true)
        {
        }
        
        bool is_sequence_ordered() const override
        {
            return keep_order_mapping_c 
                && details::get_reference(_src).is_sequence_ordered();
        }

        void start() override
        {
            auto& source = details::get_reference(_src);
            source.start();
            _end = !source.in_range();
            seek();
        }

        bool in_range() const override
        {
            return details::get_reference(_src).in_range();
        }

        const mapped_t& current() const override
        {
            return _entry;
        }

        void next() override
        {
            details::get_reference(_src).next();
            seek();
        }

    private:
        void seek()
        {
            auto& source = details::get_reference(_src);
            for (; !_end && !(_end = !source.in_range()); source.next())
            {
                if (_predicate(source.current(), _entry))
                    return;
            }
        }

        bool _end;
        Src _src;
        F _predicate;
        mutable mapped_t _entry;
    };

    /**
    *
    * \tparam keep_order - true if function keeps order. NOTE! keep_order does not mean 'ordered', it just state 
    *   if source ordered then result just keep order.
    */
    template < class F, bool keep_order_c = false >
    struct MapAndFilterFactory : FactoryBase
    {
        using traits_t = OP::utils::function_traits<F>;
        static_assert(std::is_convertible_v<typename traits_t::result_t, bool>,
            "MapAndFilter functor must evaluate convertible to bool result type");

        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using mapped_t = std::decay_t<typename traits_t::template arg_i<1>>;

        constexpr MapAndFilterFactory(F f) noexcept
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

            using target_sequence_base_t = std::conditional_t<keep_order_c,
                OrderedSequence<const mapped_t&>/*Ordered sequence does not guarantee is_sequence_ordered() is true*/,
                Sequence<const mapped_t&>
            >;

            return MaF<target_sequence_base_t, input_t, F, keep_order_c>(
                std::move(src), 
                _applicator);
        }
        F _applicator;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_MAPPING__H_
