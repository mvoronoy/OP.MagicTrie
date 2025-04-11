#pragma once
#ifndef _OP_FLUR_ORDEFAULT__H_
#define _OP_FLUR_ORDEFAULT__H_

#include <functional>
#include <memory>

#include <op/common/Utils.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

namespace OP
{
    /** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
    namespace flur
    {

        /** Sequence that implements logic to take information from source, but if source empty
        then consume from alternative source
        \tparam TElement - element produced by sequence
        \tparam Src - source for pipeline
        \tparam Alt - alternative source to consume if Src is empty
        */
        template <class TElement, class Src, class Alt>
        struct OrDefaultSequence : Sequence<TElement>
        {
            static_assert(std::is_convertible_v< 
                details::sequence_element_type_t<Src>, 
                details::sequence_element_type_t<Alt> >,
                "Alternative source must produce compatible values for 'OrDefaultSequence'");

            using base_t = Sequence<TElement>;
            using element_t = typename base_t::element_t;

            template <class T>
            constexpr OrDefaultSequence(Src&& src, T&& alt) noexcept
                : _src(std::move(src))
                , _alt(std::forward<Alt>(alt))
                , _use_alt(false)
            {
            }

            bool is_sequence_ordered() const noexcept override
            {
                return details::get_reference(_src).is_sequence_ordered()
                    && details::get_reference(_alt).is_sequence_ordered()
                    ;
            }

            virtual void start() override
            {
                _use_alt = false;
                auto& deref_src = details::get_reference(_src);
                deref_src.start();
                if (!deref_src.in_range())
                {
                    _use_alt = true;
                    details::get_reference(_alt).start();
                }
            }

            virtual bool in_range() const override
            {
                return _use_alt 
                    ? details::get_reference(_alt).in_range() 
                    : details::get_reference(_src).in_range();
            }

            virtual element_t current() const override
            {
                if (_use_alt)
                    return details::get_reference(_alt).current();
                return details::get_reference(_src).current();
            }

            virtual void next() override
            {
                _use_alt 
                    ? details::get_reference(_alt).next() 
                    : details::get_reference(_src).next();
            }
        private:
            bool _use_alt;
            Src _src;
            Alt _alt;
        };
        namespace details
        {
            template<unsigned N>
            struct priority_tag : priority_tag<N - 1> {};
            template<> struct priority_tag<0> {};
        }//ns:details

        /** Factory to create OrDefaultSequence*/
        template <class Alt>
        class OrDefaultFactory : FactoryBase
        {

            /**\brief create OrDefaultSequence.
            * specialization when Alt is generic value to return instead of main sequence
            */
            template <class A, class Src>
            auto static sequence_factory(A&& value, Src&& source_sequence,
                details::priority_tag<0> = {/*lowest priority*/})
            {
                using src_container_t = details::dereference_t<Src>;
                using element_t = typename src_container_t::element_t;
                return OrDefaultSequence<element_t, Src, OfValue<element_t> >(
                    std::move(source_sequence),
                    OfValue<element_t>(std::forward<A>(value))
                );
            }

            /**\brief create OrDefaultSequence. 
            * specialization when Alt is FactoryBase or std::shared_ptr<FactoryBase> 
            */
            template <class A, class Src, 
                std::enable_if_t<is_factory_c<std::decay_t<details::dereference_t<A>> >, int> = 0>
            auto static sequence_factory(A&& alt_factory, Src&& source_sequence,
                details::priority_tag<1> = {/*normal priority*/})
            {
                constexpr bool factory_reference_v = std::is_lvalue_reference_v<A>;
                using base_lr_t = std::decay_t<details::dereference_t<A>>;
                //implement logic of c++20: std::forward_like - make `get_reference` result be similar to `factory` 
                using lazy_range_t = std::conditional_t<factory_reference_v, const base_lr_t&, base_lr_t&&>;
                
                auto alt_seq = std::forward<lazy_range_t>(details::get_reference(alt_factory)).compound();

                using element_t = details::sequence_element_type_t<Src>;
                //need use std::forward<A> to distinguish when move semantic is used
                using alt_sequence_t = decltype(alt_seq);

                return
                    OrDefaultSequence<element_t, Src, alt_sequence_t>(
                        std::move(source_sequence),
                        std::move(alt_seq));
            }

            /**\brief create OrDefaultSequence.
            * specialization when Alt is Sequence<T>
            */
            template <class A, class Src,
                std::enable_if_t<std::is_base_of_v<OP::flur::Sequence<typename Src::element_t>, std::decay_t<details::dereference_t<A>> >, int> = 0
            >
            auto static sequence_factory(A&& alt_sequence, Src&& source_sequence,
                details::priority_tag<1> = {/*normal priority*/})
            {
                using src_container_t = details::dereference_t<Src>;
                using element_t = typename src_container_t::element_t;

                return
                    OrDefaultSequence<element_t, Src, A>(
                        std::move(source_sequence),
                        std::forward<A>(alt_sequence));
            }

        public:
            explicit constexpr OrDefaultFactory(Alt&& alt) noexcept
                : _alt(std::forward<Alt>(alt))
            {
            }

            template <class Src>
            constexpr auto compound(Src&& src) const& noexcept
            {
                return sequence_factory(_alt, std::move(src), details::priority_tag<3>{});
            }

            template <class Src>
            constexpr auto compound(Src&& src) && noexcept
            {
                return sequence_factory(std::move(_alt), std::move(src), details::priority_tag<3>{});
            }

        private:

            Alt _alt;
        };


    } //ns:flur
} //ns:OP

#endif //_OP_FLUR_ORDEFAULT__H_
