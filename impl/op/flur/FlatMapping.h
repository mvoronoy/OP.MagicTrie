#pragma once
#ifndef _OP_FLUR_FLATMAPPING__H_
#define _OP_FLUR_FLATMAPPING__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

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

            using applicator_result_t = 
                std::decay_t<decltype(std::declval<applicator_t>()(details::get_reference(std::declval<InputSequence>()).current()))>;
            
            using unref_applicator_result_t = details::dereference_t< applicator_result_t >;
            
            /** `true` when applicator produces factory (not a sequence) */
            constexpr static bool applicator_makes_factory_c =
                std::is_base_of_v < FactoryBase, unref_applicator_result_t >;
            static constexpr bool applicator_makes_sequence_c = details::is_sequence_v<applicator_result_t>;
            
            using applicator_result_sequence_t = 
                std::decay_t<details::unpack_t<unref_applicator_result_t>>;

            /** Holder for the value returned from functor to make it flat */
            using sequence_holder_t = std::conditional_t<
                    //is factory
                    applicator_makes_factory_c,
                    std::conditional_t<
                        std::disjunction_v<
                            details::is_shared_ptr<applicator_result_sequence_t >,
                            details::is_optional<applicator_result_sequence_t > >,
                        applicator_result_sequence_t,
                        std::optional<applicator_result_sequence_t >
                    >,
                    //is sequence
                    std::conditional_t<
                        std::disjunction_v<
                            details::is_shared_ptr<applicator_result_t>,
                            details::is_optional<applicator_result_t> >,
                        applicator_result_t,
                        std::optional<applicator_result_t>
                    >
                >;

            using unref_applicator_result_sequence_t = details::dereference_t< 
                applicator_result_sequence_t >;
            /** Element type produced by result FlatMapping sequence */
            using element_t = typename unref_applicator_result_sequence_t::element_t;


            template <class V, class T>
            static void assign_new(V&& from, std::optional<T>& holder)
            {
                holder.emplace(std::forward<V>(from));
            }
            /** Makes holder empty */
            template <class T>
            static void reset(std::optional<T>& holder)
            {
                holder.reset();
            }
            template <class T>
            static bool is_empty(const std::optional<T>& holder)
            {
                return !holder;
            }

            template <class V, class T>
            static void assign_new(std::shared_ptr<V>&& from, std::shared_ptr<T>& holder)
            {
                holder = std::forward< std::shared_ptr<V>>(from);
            }
            /** Makes holder empty */
            template <class T>
            static void reset(std::shared_ptr<T>& holder)
            {
                holder.reset();
            }
            template <class T>
            static bool is_empty(const std::shared_ptr<T>& holder)
            {
                return !holder;
            }
            template <typename Dest, typename U, class... Args>
            static auto invoke_compound(Dest& dest, U&& u, Args&& ...args) {
                // case when std::optional used, `std::forward<U>(u)` - is important to avoid copy constructor
                dest.emplace(std::forward<U>(u).compound(std::forward<Args>(args)...));
            }

            template <typename Dest, typename U, class... Args>
            static auto invoke_compound(Dest& d, std::shared_ptr<U>&& u, Args&& ...args) {
                d = std::move(u->compound(std::forward<Args>(args)...));
            }
        };

        template <class TFlatMapTraits, bool keep_order_c>
        struct FlatMapping : public Sequence<typename TFlatMapTraits::element_t>
        {
            using traits_t = TFlatMapTraits;
            using base_t = Sequence<typename traits_t::element_t>;
            using element_t = typename base_t::element_t;
            using input_sequence_t = typename traits_t::input_sequence_t;
            using applicator_t = typename traits_t::applicator_t;

            constexpr FlatMapping(input_sequence_t&& src, applicator_t f) noexcept
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
                seek();
            }
            
            virtual bool in_range() const override
            {
                return !traits_t::is_empty(_defered) && defered().in_range();
            }
            
            virtual element_t current() const override
            {
                return defered().current();
            }
            
            virtual void next() override
            {
                defered().next();
                if (!defered().in_range() && details::get_reference(_src).in_range())
                {
                    details::get_reference(_src).next();
                    seek();
                }
            }

        private:
            using sequence_holder_t = typename traits_t::sequence_holder_t;

            void seek()
            {
                for (auto& rrc = details::get_reference(_src); rrc.in_range(); rrc.next())
                {   
                    if constexpr (std::is_base_of_v<FactoryBase, typename traits_t::unref_applicator_result_t>)
                    {
                        traits_t::invoke_compound(
                            _defered,
                            std::move(
                                _applicator(
                                    std::move(rrc.current())))
                        );
                    }
                    else //assume inheritance from Sequence
                    {
                        traits_t::assign_new(
                            std::move(_applicator(std::move(rrc.current()))),
                            _defered);
                    }
                    //
                    // Following block works, but invokes more copying constructors
                    // 
                    //traits_t::assign_new(
                    //    std::move(
                    //        details::unpack(_applicator(details::get_reference(rrc.current())))
                    //    ),
                    //    _defered
                    //);
                    defered().start();
                    if (defered().in_range())
                    { //new sequence is not empty, so we can proceed
                        return;
                    }
                }
                //reach end of source sequence, no data
                traits_t::reset(_defered);
            }

            auto& defered()
            {
                return details::get_reference(*_defered);
            }

            const auto& defered() const
            {
                return details::get_reference(*_defered);
            }

            applicator_t _applicator;

            sequence_holder_t _defered;
            input_sequence_t _src;
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
            using applicator_t = std::decay_t<F>;

            constexpr FlatMappingFactory(F&& f) noexcept
                : _applicator(std::forward<F>(f))
            {
            }

            template <class Src>
            constexpr auto compound(Src&& src) const& noexcept
            {
                using traits_t = FlatMapTraits<applicator_t, Src>;
                return FlatMapping<traits_t, keep_order_c>(
                    std::move(src), _applicator);
            }

            template <class Src>
            constexpr auto compound(Src&& src) && noexcept
            {
                using traits_t = FlatMapTraits<applicator_t, Src>;
                return FlatMapping<traits_t, keep_order_c>(
                    std::move(src), std::move(_applicator));
            }
            applicator_t _applicator;
        };


    } //ns:flur
} //ns:OP

#endif //_OP_FLUR_FLATMAPPING__H_
