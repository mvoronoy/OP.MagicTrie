#pragma once
#ifndef _OP_FLUR_FLATMAPPING__H_
#define _OP_FLUR_FLATMAPPING__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/StackAlloc.h>
#include <op/common/ftraits.h>


namespace OP
{
    /** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
    namespace flur
    {
        template <class F, class InputSequence>
        struct FlatMapTraits
        {
            using applicator_t = std::decay_t<F>;
            using input_sequence_t = InputSequence;

            using deref_input_sequence_t = details::dereference_t<input_sequence_t>;

            static decltype(auto) invoke(
                applicator_t& applicator,
                const deref_input_sequence_t& sequence,
                SequenceState& state)
            {
                if constexpr (std::is_invocable_v<applicator_t>)
                {// flat map functor can be empty
                    return applicator();
                }
                else if constexpr (std::is_invocable_v<applicator_t, 
                    decltype(details::get_reference(sequence).current())>)
                { //the same as default fallback, but allows in first order check `flat_mapping([](auto&...)`
                    return applicator(details::get_reference(sequence).current());
                }
                else if constexpr (std::is_invocable_v<applicator_t, decltype(state)>)
                {// SequenceState only consumer
                    return applicator(state);
                }
                else if constexpr (std::is_invocable_v<applicator_t, 
                    decltype(details::get_reference(sequence).current()), decltype(state)>)
                {
                    return applicator(details::get_reference(sequence).current(), state);
                }
                else if constexpr (std::is_invocable_v<applicator_t, decltype(state), 
                    decltype(details::get_reference(sequence).current())>)
                {
                    return applicator(state, details::get_reference(sequence).current());
                }
                else //default fallback for consuming current element of input sequence
                {
                    return applicator(details::get_reference(sequence).current());
                }
            }

            /** result type of applicator function */
            using applicator_result_t = decltype(
                invoke(std::declval<F&>(),
                    std::declval<const deref_input_sequence_t&>(),
                    std::declval<SequenceState&>()) );
                //Works:
                //decltype(
                //    std::declval<F>()(
                //        details::get_reference(std::declval<std::decay_t<input_sequence_t>&>().current()))
                //    )
                ;

            using applicator_result_sequence_t =
                std::decay_t<details::unpack_t<applicator_result_t>>;

            /** Holder for the value returned from functor to make it flat */
            using sequence_holder_t = OP::MemBuf<applicator_result_sequence_t>;

            using unref_applicator_result_sequence_t = details::dereference_t<
                applicator_result_sequence_t >;
            /** Element type produced by result FlatMappingSequence sequence */
            using element_t = typename unref_applicator_result_sequence_t::element_t;

            template <class T, class V>
            static void assign_new(T& holder, V&& from)
            {
                holder.construct(std::forward<V>(from));
            }

            template <class T>
            static bool is_empty(const std::shared_ptr<T>& holder)
            {
                return !holder;
            }

            template <typename Dest, typename U, class... Args>
            static auto invoke_compound(Dest& dest, U&& u, Args&& ...args) {
                // case when std::optional used, `std::forward<U>(u)` - is important to avoid copy constructor
                dest.construct(std::forward<U>(u).compound(std::forward<Args>(args)...));
            }

            template <typename Dest, typename U, class... Args>
            static auto invoke_compound(Dest& dest, std::shared_ptr<U> u, Args&& ...args) {
                dest.construct(u->compound(std::forward<Args>(args)...));
            }
        };

        template <class TFlatMapTraits, bool keep_order_c>
        struct FlatMappingSequence : public Sequence<typename TFlatMapTraits::element_t>
        {
            using traits_t = TFlatMapTraits;
            using base_t = Sequence<typename traits_t::element_t>;
            using element_t = typename base_t::element_t;
            using input_sequence_t = std::decay_t<typename traits_t::input_sequence_t>;
            using applicator_t = typename traits_t::applicator_t;

            constexpr FlatMappingSequence(input_sequence_t&& src, applicator_t f) noexcept
                : _src(std::move(src))
                , _applicator(std::move(f))
            {
            }

            bool is_sequence_ordered() const noexcept override
            {
                return keep_order_c &&
                    details::get_reference(_src).is_sequence_ordered();
            }

            virtual void start() override
            {
                details::get_reference(_src).start();
                _state.start();
                seek();
            }

            virtual bool in_range() const override
            {
                return _deferred.has_value() && deferred().in_range();
            }

            virtual element_t current() const override
            {
                return deferred().current();
            }

            virtual void next() override
            {
                auto& def_ref = deferred();
                auto& src_ref = details::get_reference(_src);
                def_ref.next();
                if (!def_ref.in_range() && src_ref.in_range())
                {
                    src_ref.next();
                    _state.next();
                    seek();
                }
            }

        private:

            using sequence_holder_t = typename traits_t::sequence_holder_t;

            void seek()
            {
                auto& src_ref = details::get_reference(_src);
                for (; src_ref.in_range(); src_ref.next(), _state.next())
                {
                    _deferred.construct(details::unpack(
                        traits_t::invoke(_applicator, src_ref, _state)));
                    deferred().start();
                    if (deferred().in_range())
                    { //new sequence is not empty, so we can proceed
                        return;
                    }
                }
                //reach end of source sequence, no data
                _deferred.destroy();
            }

            auto& deferred()
            {
                return details::get_reference(*_deferred);
            }

            const auto& deferred() const
            {
                return details::get_reference(*_deferred);
            }

            applicator_t _applicator;
            sequence_holder_t _deferred;
            input_sequence_t _src;
            SequenceState _state;
        };

        /**
        *
        * \tparam options_c - extra options to customize sequence behavior. Implementation recognizes:
        *       - Intrinsic::keep_order - to allow keep source sequence order indicator;
        */
        template < class F, auto ...options_c >
        struct FlatMappingFactory : FactoryBase
        {
            constexpr static inline bool keep_order_c = OP::utils::any_of<options_c...>(Intrinsic::keep_order);
            using applicator_t = F;//std::decay_t<F>;

            template <class FLike>
            constexpr FlatMappingFactory(int, FLike&& f) noexcept
                : _applicator(std::forward<FLike>(f))
            {
            }

            template <class Src>
            constexpr auto compound(Src&& src) const& noexcept
            {
                using traits_t = FlatMapTraits<applicator_t, Src>;
                return FlatMappingSequence<traits_t, keep_order_c>(
                    std::move(src), _applicator);
            }

            template <class Src>
            constexpr auto compound(Src&& src) && noexcept
            {
                using traits_t = FlatMapTraits<applicator_t, Src>;
                return FlatMappingSequence<traits_t, keep_order_c>(
                    std::move(src), std::move(_applicator));
            }
            applicator_t _applicator;
        };


    } //ns:flur
} //ns:OP

#endif //_OP_FLUR_FLATMAPPING__H_
