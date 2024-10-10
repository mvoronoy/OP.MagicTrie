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
    enum class MaFOptions
    {
        /** Modify MaF behavior to declare Sequence::current return result by value instead of
        * const reference
        */
        result_by_value
    };

    /**
    * Mapping and filter by one opertion 
    * \tparam Src - source sequnce to convert
    * \tparam F functor with the signature `bool(typename Src::element_t, <desired-mapped-type> &result)`
    *       Note that implementation assumes that <desired-mapped-type> is default constructible
    * \tparam options_c - extra options to customize sequence behavior. Implementation recognizes one or any of:
    *       - Intrinsic::keep_order - to allow keep source sequence order indicator;
    *       - MaFOptions::result_by_value - to declare `current()` return result by value instead of
    *            const reference.
    *
    */
    template <class Base, class Src, class F, auto ... options_c>
    struct MaF : public Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        constexpr static inline bool keep_order_mapping_c = OP::utils::any_of<options_c...>(Intrinsic::keep_order);

        using traits_t = OP::utils::function_traits<F>;
        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using mapped_t = std::decay_t<typename traits_t::template arg_i<1>>;

        static_assert(std::is_same_v<mapped_t, std::decay_t<element_t>>, 
            "compile time de-sync of element types, F must produce compatible type");

        template <class U>
        constexpr MaF(Src&& src, U f) noexcept
            : _src(std::move(src))
            , _predicate(std::move(f))
        {
        }
        
        bool is_sequence_ordered() const noexcept override
        {
            return keep_order_mapping_c 
                && details::get_reference(_src).is_sequence_ordered();
        }

        void start() override
        {
            details::get_reference(_src).start();
            seek();
        }

        bool in_range() const override
        {
            return details::get_reference(_src).in_range();
        }

        element_t current() const override
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
            for (; source.in_range(); source.next())
            {
                if (_predicate(source.current(), _entry))
                    return;
            }
        }

        Src _src;
        F _predicate;
        mutable mapped_t _entry{};
    };

    /**
    *
    * Factory for the MaF sequence
    */
    template < class F, auto ... options_c>
    struct MapAndFilterFactory : FactoryBase
    {
        using traits_t = OP::utils::function_traits<F>;
        static_assert(std::is_convertible_v<typename traits_t::result_t, bool>,
            "MapAndFilter functor must evaluate convertible to bool result type");

        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using mapped_t = std::decay_t<typename traits_t::template arg_i<1>>;

        constexpr static inline bool is_result_by_value_c = OP::utils::any_of<options_c...>(MaFOptions::result_by_value);
        constexpr static inline bool keep_order_c = OP::utils::any_of<options_c...>(Intrinsic::keep_order);

        using sequence_element_t = std::conditional_t<is_result_by_value_c, mapped_t, const mapped_t&>;
        using target_sequence_base_t = std::conditional_t<keep_order_c,
            OrderedSequence<sequence_element_t>/*Ordered sequence does not guarantee is_sequence_ordered() is true*/,
            Sequence<sequence_element_t>
        >;

        template <class TSrc>
        using result_sequence_t = MaF<target_sequence_base_t, TSrc, F, options_c ...>;

        constexpr MapAndFilterFactory(F f) noexcept
            : _applicator(f)
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            return result_sequence_t<std::decay_t<Src>>(
                std::move(src), 
                _applicator);
        }

        F _applicator;
    };

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_MAPPING__H_
