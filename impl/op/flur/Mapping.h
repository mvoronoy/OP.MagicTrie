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
        template <class F>
        struct WrapMapApplicator
        {
            F _applicator;

            constexpr explicit WrapMapApplicator(F f) noexcept
                : _applicator(std::move(f))
            {
            }

            template <class R>
            inline decltype(auto) invoke_impl(R&& a, const SequenceState& state)
            {
                return OP::currying::recomb_call(
                    _applicator, std::forward<R>(a), state);
            }
        };

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

        constexpr MappingSequence(Src&& src, details::WrapMapApplicator<F> holder) noexcept
            : _src(std::move(src))
            , _state{}
            , _holder(std::move(holder))
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
            return _holder.invoke_impl(details::get_reference(_src).current(), _state);
        }

        void next() override
        {
            details::get_reference(_src).next();
            _state.next();
        }

    private:

        Src _src;
        SequenceState _state;
        mutable details::WrapMapApplicator<F> _holder;
    };
    
    /** Helper class allows to reduce allocations number when mapping result
    * is a heap consuming entity. For example, following code allocates memory for 
    * `std::string` several times:
    *  \code
    *     src::of_iota(5, 7)
    *     >> then::keep_order_mapping([](auto n)->std::string{ return std::to_string(n); }
    *  \endcode
    *  To optimize it use state-full functor: \code
    *     src::of_iota(5, 7)
    *     >> then::keep_order_mapping(
    *         ReusableMapBuffer([](auto n, std::string& already_existing) -> void
    *         { 
    *             std::format_to(
    *                 std::back_inserter(already_existing), "{}", n); 
    *         })
    *     );
    * \endcode
    * 
    */
    template <class F, bool result_by_value_c = false>
    struct ReusableMapBuffer
    {
        using traits_t = OP::utils::function_traits<F>;
        using from_t = std::decay_t<typename traits_t::template arg_i<0>>;
        using to_t = std::decay_t<typename traits_t::template arg_i<1>>;
        using result_t = std::conditional_t< result_by_value_c, to_t, const to_t&>;

        constexpr explicit ReusableMapBuffer(F f) noexcept
            : _f(std::move(f)) 
            , _entry{}
        {
        }

        constexpr ReusableMapBuffer(to_t init_buffer, F f) noexcept
            : _f(std::move(f)) 
            , _entry{std::move(init_buffer)}
        {
        }

        result_t operator()(const from_t& from) const
        {
            _f(from, _entry);
            return _entry;
        }

    private:
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
        constexpr auto compound(Src&& src) const& noexcept
        {
            using src_container_t = details::sequence_type_t< details::dereference_t<Src> >;

            details::WrapMapApplicator holder{ _applicator };

            using result_t = decltype( holder.invoke_impl(
                details::get_reference(src).current(),
                std::declval<SequenceState>()
                )
            );

            return MappingSequence<result_t, Src, applicator_t, keep_order_c>(
                std::move(src), 
                std::move(holder)
            );
        }

        template <class Src>
        constexpr auto compound(Src&& src) && noexcept
        {
            using src_container_t = details::sequence_type_t< details::dereference_t<Src> >;

            details::WrapMapApplicator holder{ std::move(_applicator) };

            using result_t = decltype(
                holder.invoke_impl(
                details::get_reference(src).current(),
                std::declval<SequenceState>()
                )
            );

            return MappingSequence<result_t, Src, applicator_t, keep_order_c>(
                std::move(src), 
                std::move(holder));
        }
    private:
        applicator_t _applicator;
    };
} //ns:OP::flur

#endif //_OP_FLUR_MAPPING__H_
