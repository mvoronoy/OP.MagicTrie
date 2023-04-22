#pragma once
#ifndef _OP_FLUR_APPLICATOR__H_
#define _OP_FLUR_APPLICATOR__H_
#include <tuple>
#include <op/flur/Cartesian.h>
#include <op/common/ftraits.h>

namespace OP::flur
{

struct ApplicatorBase
{

};

template <class OutputIterator>
struct Drain : ApplicatorBase
{
    Drain(OutputIterator out_iter)
        : _out_iter(std::move(out_iter))
    {
    }

    template <class Lr>
    void operator()(const Lr& lr)
    {
        auto seq = lr.compound();
        for(details::get_reference(seq).start();
            details::get_reference(seq).in_range();
            details::get_reference(seq).next())
            *_out_iter++ = details::get_reference(seq).current();
    }

private:
    OutputIterator _out_iter;
};

template <class T>
struct Sum : ApplicatorBase
{
    Sum(T& dest)
        : _dest(dest)
    {
    }

    template <class Lr>
    void operator()(const Lr& lr)
    {
        auto seq = lr.compound();
        for(details::get_reference(seq).start();
            details::get_reference(seq).in_range();
            details::get_reference(seq).next())
            _dest += details::get_reference(seq).current();
    }

private:
    T& _dest;
};

template <class T, class F>
struct SumF : ApplicatorBase
{
    F _f;
    SumF(T& dest, F f)
        : _dest(dest)
        , _f(std::move(f))
    {
    }

    template <class Lr>
    void operator()(const Lr& lr)
    {
        auto seq = lr.compound();
        for(details::get_reference(seq).start();
            details::get_reference(seq).in_range();
            details::get_reference(seq).next())
            _dest += _f(details::get_reference(seq).current());
    }

private:
    T& _dest;
};

//
//template <class ... FApplicators>
//struct MultiConsumer : ApplicatorBase
//{
//    using applicator_set_t = std::tuple<std::decay_t<FApplicators>...>;
//
//    template <class TElement>
//    inline static constexpr auto type_matches_c = 
//            std::conjunction<
//            std::is_convertible< 
//                /*From:*/TElement,
//                /*To:*/typename function_traits< std::decay_t<FApplicators> >::template arg_i<0> 
//                >, ...
//            >;
//
//    constexpr MultiConsumer(FApplicators&& ... applicators) noexcept
//        : _lazy_factory(std::forward<TLazy>(lazy_factory))
//        , _applicators(std::make_tuple(applicators...))
//    {
//    }
//    
//    template <class TApplicator>
//    constexpr auto then_apply(TApplicator&& after) && noexcept
//    {
//        using ext_t = BaseApplicator<TLazy, FApplicators ..., TApplicator>; 
//        return std::apply([&](auto& ... applicator){ 
//            return ext_t{std::move(_lazy_factory), std::move(applicator)..., std::forward<TApplicator>(after)};
//            });
//    }
//
//    template <class TLazy>
//    void run(const TLazy& lazy_factory) const
//    {
//        auto seq = _lazy_factory.compound();
//        for(seq.start(); seq.in_range(); seq.next())
//        {
//            std::apply([](const auto& ... applicator){
//                applicator(seq.current());
//            }); 
//        }
//    }
//
//    /*void run(OP::utils::ThreadPool& tpool)
//    {
//    }
//    */
//private:
//    applicator_set_t _applicators;
//};
//
//
//template <class TLazy, class TConsumer>
//struct Consumer
//{
//    static_assert(
//        TConsumer::type_matches_c<typename sequence_type_t<TLazy>::element_t>,
//        "Applicator must be exactly 1 arg functor of type provided by Lazy Sequence"
//    );
//
//    constexpr Consumer(TLazy&& lazy_sequence, TConsumer&& consumer):
//        : _lazy_sequence(std::forward<TLazy>(lazy_sequence))
//        , consumer(std::forward<TConsumer>(consumer))
//        {
//        }
//
//    void operator()() const
//    {
//        _consumer.run(details::get_reference(_lazy_sequence));
//    }
//
//private:
//    TLazy _lazy_sequence;
//    TConsumer  _consumer;
//};

template <class ... Lx>
struct CartesianApplicator : ApplicatorBase
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
