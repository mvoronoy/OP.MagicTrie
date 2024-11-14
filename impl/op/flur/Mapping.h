#pragma once
#ifndef _OP_FLUR_MAPPING__H_
#define _OP_FLUR_MAPPING__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/ftraits.h>

namespace OP::flur
{
    namespace details
    {
        template <class F, class A, class ...Ax>
        inline decltype(auto) map_invoke_impl(F& applicator, A&& a, Ax&& ...ax)
        {
            if constexpr (std::is_invocable_v<F, A>)
            {
                return applicator(std::forward<A>(a));
            }
            else if constexpr (std::is_invocable_v<F, A, Ax...>)
            {
                return applicator(std::forward<A>(a), std::forward<Ax>(ax)...);
            }
            else if constexpr (std::is_invocable_v<F, Ax..., A>)
            { //it is not real recombination, but for 2 it works
                return applicator(std::forward<Ax>(ax)..., std::forward<A>(a));
            }
            else 
            {
                return map_invoke_impl(applicator, std::forward<Ax>(ax)...);
            }
        }

    } //ns:details
    /**
    * MappingSequence converts one sequence to another by applying functor to a source element.
    * \tparam Src - source sequence to convert
    */
    template <class R, class Src, class F, bool keep_order_c>
    struct MappingSequence : public Sequence<R>
    {
        using base_t = Sequence<R>;
        using element_t = typename base_t::element_t;

        template <class U>
        constexpr MappingSequence(Src&& src, U&& f) noexcept
            : _src(std::move(src))
            , _state{}
            , _applicator(std::forward<U>(f))
        {
        }
        
        bool is_sequence_ordered() const noexcept override
        {
            return keep_order_c 
                && details::get_reference(_src).is_sequence_ordered();
        }

        void start() override
        {
            details::get_reference(_src).start();
            _state.start();
        }

        bool in_range() const override
        {
            return details::get_reference(_src).in_range();
        }
        
        element_t current() const override
        {
            return details::map_invoke_impl(_applicator, details::get_reference(_src).current(), _state);
        }

        void next() override
        {
            details::get_reference(_src).next();
            _state.next();
        }

    private:

        Src _src;
        SequenceState _state;
        F _applicator;
    };
    
    /** Helper class allows to reduce allocations number when mapping result
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
    template <class F, bool result_by_value_c = false>
    struct ReusableMapBuffer
    {
        using traits_t = OP::utils::function_traits<F>;
        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using to_t = std::decay_t<typename traits_t::template arg_i<1>>;
        using result_t = std::conditional_t< result_by_value_c, to_t, const to_t&>;

        ReusableMapBuffer(F f) 
            : _f(std::move(f)) 
        {
        }

        result_t operator()(const from_t& from) const
        {
            _f(from, _entry);
            return _entry;
        }

        F _f;
        mutable to_t _entry;
    };
    

    /**
    *
    * \tparam keep_order - true if function keeps order. NOTE! keep_order does not grant 'ordering', it just state 
    *   if source ordered then result just keep order.
    */
    template < class F, bool keep_order_c = false >
    struct MappingFactory : FactoryBase
    {
        using applicator_t = F;//std::decay_t<F>;

        template <class FLike>
        constexpr MappingFactory(int, FLike&& f) noexcept
            : _applicator(std::forward<FLike>(f))
        {
        }

        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            using src_container_t = details::sequence_type_t< details::dereference_t<Src> >;

            using result_t = decltype( details::map_invoke_impl(
                std::declval<applicator_t&>(),
                std::declval< src_container_t& >().current(), 
                std::declval<SequenceState&>()
                )
            );

            return MappingSequence<result_t, Src, applicator_t, keep_order_c>(
                std::move(src), 
                _applicator);
        }
        applicator_t _applicator;
    };
} //ns:OP::flur

#endif //_OP_FLUR_MAPPING__H_
