#pragma once
#ifndef _OP_FLUR_CARTESIAN__H_
#define _OP_FLUR_CARTESIAN__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    template <class R, class F, class ... Seqx>
    struct CartesianSequence: public Sequence< R >
    {
        using base_t = Sequence< R >;
        using base_t::base_t;
        using element_t = typename base_t::element_t;

        constexpr CartesianSequence(F applicator, Seqx&& ...sx) noexcept
            : _applicator(applicator)
            , _sources(std::move(sx)...)
            , _all_good(false)
        {
        }

        void start() override
        {
            auto ini_seq = [](auto& seq) -> bool 
            {
                auto& ref = details::get_reference(seq);
                ref.start();
                return ref.in_range();
            };
            //start all
            _all_good = std::apply( 
                [&](auto& ...seq) ->bool {  
                    return (ini_seq(seq) && ...);
            }, _sources);
        }

        bool in_range() const override
        {
            return _all_good;
        }

        element_t current() const override
        {
            // this class as an applicator can be used with `void`, so add special case handler
            if constexpr(std::is_same_v<void, element_t>)
            {
                do_call(std::make_index_sequence<seq_size_c>{});
            }
            else
            {
                return do_call(std::make_index_sequence<seq_size_c>{});
            }
        }

        void next() override
        {
            _all_good = do_next(std::make_index_sequence<seq_size_c>{});
        }

    private:
        static constexpr size_t seq_size_c = sizeof ... (Seqx);

        template <size_t... I>
        bool do_next(std::index_sequence<I...>)
        {
            bool sequence_failed = false;
            bool result = (do_step<I>(sequence_failed) || ...);
            return !sequence_failed && result;
        }

        template <size_t I>
        bool do_step(bool& sequence_failed)
        {
            auto &seq = details::get_reference(std::get<I>(_sources));
            
            if(!seq.in_range()) //indication of empty sequence stop all
            {
                sequence_failed = true;
                return true; //don't propagate
            }
            seq.next();    
            if( seq.in_range() )
                return true; //stop other sequences propagation
            seq.start(); //restart 
            if( !seq.in_range() )
            {
                sequence_failed = true;
                return true; //don't propagate
            }
            return false; //propagate sequences enumeration
        }

        template <size_t... I>
        auto do_call(std::index_sequence<I...>) const
        {
            return _applicator(
                details::get_reference(std::get<I>(_sources)).current()...);
        }

        F _applicator;
        std::tuple<Seqx...> _sources;
        bool _all_good;
    };


    template <class F, class ... Lx>
    struct CartesianFactory : FactoryBase
    {
        constexpr CartesianFactory(F f, Lx&& ... alien) noexcept
            : _alien_factory(std::forward<Lx>(alien) ... )
            , _applicator(std::move(f))
        {
        }

        template <class L>
        using nth_sequence_t = std::decay_t<decltype(
            details::get_reference(details::get_reference(std::declval<L&>()).compound())
        )>;
        
        template <class Src>
        constexpr auto compound(Src&& src) const noexcept
        {
            return construct_sequence(
                std::make_index_sequence<sizeof...(Lx)>{}, std::forward<Src>(src)
            );
        }

        // factory can be used as a source (without previous `sequence >> cartesian`)
        constexpr auto compound() const noexcept
        {
            return construct_sequence(
                std::make_index_sequence<sizeof...(Lx)>{} );
        }

    private:
        template <class ... Seqx, size_t ... I>
        constexpr auto construct_sequence(std::index_sequence<I...>, Seqx&& ... sx ) const noexcept
        {
            using result_t = decltype(
                _applicator(
                    details::get_reference(sx).current()...,
                    std::declval<nth_sequence_t<Lx>&>().current()...)
                );
            using sequence_t = CartesianSequence<
                result_t, F, std::decay_t<Seqx>..., nth_sequence_t<Lx>...>;
            return sequence_t{
                _applicator,
                std::move(details::get_reference(sx))...,
                std::move(details::get_reference(std::get<I>(_alien_factory)).compound())...
            };
        }
        std::tuple<Lx...> _alien_factory;
        F _applicator;
    };

} //ns:OP::flur

#endif //_OP_FLUR_CARTESIAN__H_
