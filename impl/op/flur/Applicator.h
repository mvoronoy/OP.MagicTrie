#pragma once
#ifndef _OP_FLUR_APPLICATOR__H_
#define _OP_FLUR_APPLICATOR__H_
#include <tuple>
#include <op/flur/Cartesian.h>
#include <op/common/ftraits.h>

namespace OP::flur
{

struct Applicator
{

};

template <class ... Lx>
struct CartesianApplicator : Applicator
{
    constexpr CartesianApplicator(Lx ...lx) noexcept
        : _sources(std::move(lx)...)
    {
    }

    template <class Lz>
    constexpr auto extend(Lz&& lz) const
    {
        return construct(std::forward<Lz>(lz), std::make_index_sequence<seq_size_c>{});
    }

    template <class L>
    using nth_sequence_t = std::decay_t<decltype(
        details::get_reference(details::get_reference(std::declval<L&>()).compound())
    )>;
    
    template <class F>
    void collect(F applicator) const
    {
        //start all
        auto seq = std::apply(
            [&](const auto& ... seq_factory) {

                using result_t = decltype(
                    applicator(
                        details::get_reference(details::get_reference(seq_factory).compound()).current()...)
                    );

                using sequence_t = CartesianSequence<
                    result_t, F, 
                    std::decay_t<decltype(details::get_reference(seq_factory).compound())>... >;

                return sequence_t(
                    std::move(applicator),
                    details::get_reference(seq_factory).compound()...
                );    

            }, _sources);

        for(seq.start(); seq.in_range(); seq.next())
        {//consume all
            seq.current();
        } 
    }

private:
    static constexpr size_t seq_size_c = sizeof ... (Lx);

    
    template <class Lz, size_t... I>
    constexpr auto construct(Lz&& arg, std::index_sequence<I...>) const
    {
        return CartesianApplicator<Lx..., Lz>( std::get<I>(_sources)..., std::forward<Lz>(arg));
    }
    
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
        auto &seq = std::get<I>(_sources);
        
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

    template <class F, size_t... I>
    void do_call(F& applicator, std::index_sequence<I...>)
    {
        //func(args.template pop< typename traits::template arg_i<I> >()...);
        applicator(std::get<I>(_sources).current()...);
    }
    
    std::tuple<Lx...> _sources;
};

} //ns: OP::flur
#endif //_OP_FLUR_APPLICATOR__H_
