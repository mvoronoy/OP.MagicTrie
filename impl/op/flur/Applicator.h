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

    /** Trait test if template parameter T is ApplicatorBase */
    template <class T>
    constexpr static inline bool is_applicator_c = std::is_base_of_v<ApplicatorBase, T>;

    namespace details
    {    
        /** Define compile-time check `has_on_start` if class has `on_start` method */
        OP_DECLARE_CLASS_HAS_MEMBER(on_start);
        /** Define compile-time check `has_on_consume` if class has `on_consume` method */
        OP_DECLARE_CLASS_HAS_MEMBER(on_consume);
        /** Define compile-time check `has_on_complete` if class has `on_complete` method */
        OP_DECLARE_CLASS_HAS_MEMBER(on_complete);
        namespace extra
        {
            OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(on_start);
            OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(on_consume);
        }
    }//ns:details


    template <class Lr, class TAction, std::enable_if_t<is_applicator_c<TAction>, int> = 0 >
    decltype(auto) collect_result(Lr&& factory, TAction& action)
    {
        auto seq = std::forward<Lr>(factory).compound();
        auto& rseq = details::get_reference(seq);
        using sequence_t = std::decay_t<decltype(rseq)>;
        decltype(auto) collector = action.collect_for(rseq);
        using collector_t = std::decay_t<decltype(collector)>;
        
        rseq.start();
        if constexpr(
            details::has_on_start<collector_t>::value
            || details::extra::has_on_start<collector_t, sequence_t>::value)
        {
            collector.on_start(rseq);
        }

        for(; rseq.in_range(); rseq.next())
        {
            if constexpr(
                details::has_on_consume<collector_t>::value
                || details::extra::has_on_consume<collector_t, sequence_t>::value)
            {
                collector.on_consume(rseq);
            }
        }        
        
        if constexpr(details::has_on_complete<collector_t>::value)
        {
            return collector.on_complete();
        }
    }


    template <class OutputIterator>
    struct Drain : ApplicatorBase
    {
        Drain(OutputIterator out_iter) noexcept
            : _out_iter(std::move(out_iter))
        {
        }

        template <class TSeq>
        auto& collect_for(TSeq& rseq) const noexcept
        {
            return *this;
        }

        template <class TSeq>
        void on_consume(const TSeq& seq)
        {
            *_out_iter++ = seq.current();
        }

    private:
        OutputIterator _out_iter;
    };

    namespace apply
    {
        template <class OutputIterator>
        constexpr auto drain(OutputIterator&& out_iter) noexcept
        {
            return Drain<OutputIterator>(std::forward<OutputIterator>(out_iter));
        }

    }// ns:apply

    /** placeholder to postpone target type detection */
    struct AsSequence{};

    /**
    *
    * \tparam T type of destination result, it is allowed to be `AsSequence` - meaning result is the same as TSequence::element_t.
    */
    template <class T, template <typename> class Op = std::plus >
    class Sum : ApplicatorBase
    {
    public:

        template <class TSum, class TSeq>
        struct Collector
        {
            TSum _dest;
            Op<std::decay_t<TSum>> _action;

            constexpr Collector() noexcept
                : _dest{}
                , _action{}
            {}

            constexpr Collector(TSum& dest, Op<std::decay_t<TSum>> action = {})
                : _dest{ dest }
                , _action(std::move(action))
            {}

            void on_consume(const TSeq& seq)
            {
                details::get_reference(_dest) = _action(
                    details::get_reference(_dest), seq.current());
            }

            decltype(auto) on_complete() noexcept
            {
                return _dest;
            }
        };

        constexpr Sum() = default;

        constexpr Sum(T dest)
            : _dest(dest)
        {
        }

        template <class TSeq>
        auto collect_for(TSeq& rseq) noexcept
        {
            if constexpr(std::is_same_v<T, AsSequence>)
            {
                using element_t = std::decay_t<details::sequence_element_type_t<TSeq>>;
                return Collector<element_t, TSeq>{};
            }
            else
                return Collector<T, TSeq>{_dest};
        }

    private:

        T _dest;
    };

    template <class T>
    struct Count : ApplicatorBase
    {
        constexpr Count(T counter) noexcept
            : _counter(counter)
        {}

        template <class TSeq>
        auto& collect_for(TSeq& ) noexcept
        {
            return *this;
        }

        template <class TSeq>
        void on_consume(TSeq&)
        {
            ++_counter;
        }
        
        decltype(auto) on_complete() noexcept
        {
            return _counter;
        }

    private:
        T _counter;
    };

    template <class T, class TBinaryFunction>
    struct Reduce : ApplicatorBase
    {
        constexpr Reduce(T& dest, TBinaryFunction f) noexcept
            : _dest(dest)
            , _f(std::move(f))
        {
        }

        template <class TSeq>
        auto& collect_for(TSeq& ) noexcept
        {
            return *this;
        }

        template <class TSeq>
        void on_consume(const TSeq& seq) const
        {
            _f(_dest, seq.current());
        }

        decltype(auto) on_complete() noexcept
        {
            return _dest;
        }

    private:
        T& _dest;
        TBinaryFunction _f;
    };

    template <class TUnaryFunction>
    struct ForEach : ApplicatorBase
    {
        constexpr ForEach(TUnaryFunction f) noexcept
            : _f(std::move(f))
        {
        }

        template <class TSeq>
        auto& collect_for(TSeq& ) noexcept
        {
            return *this;
        }

        template <class TSeq>
        void on_consume(const TSeq& seq)
        {
            _f(seq.current());
        }

    private:
        TUnaryFunction _f;
    };

    namespace apply
    {
        template <class TUnaryFunction>
        constexpr auto for_each(TUnaryFunction terminator) noexcept
        {
            return ForEach<TUnaryFunction>(std::move(terminator));
        }

        /** consume all from connected range and each time apply ++ operator to `counter` variable */
        template <class T>
        constexpr auto count(T& counter) noexcept
        {
            return Count<T&>(counter);
        }

        constexpr auto count() noexcept
        {
            return Count<size_t>(0);
        }

        template <class T>
        constexpr auto sum(T& result) noexcept
        {
            return Sum<T&>(result);
        }

        constexpr auto sum() noexcept
        {
            return Sum<AsSequence>{};
        }

        template <template <typename> class Op, class T>
        constexpr auto sum(T& result) noexcept
        {
            return Sum<T&, Op>(result);
        }

        template <class T, class TBinaryConsumer>
        constexpr auto reduce(T& target, TBinaryConsumer consumer) noexcept
        {
            return Reduce<T, TBinaryConsumer>(target, std::move(consumer));
        }


        template <class T >
        [[nodiscard]] auto first(T&& flur_obj)
        {
            auto seq = details::get_reference(flur_obj).compound();
            auto& rseq = details::get_reference(seq);
            rseq.start();
            if (!rseq.in_range())
            {
                throw std::out_of_range("taking `first` of empty lazy range");
            }
            return rseq.current();
        }

        template <class T>
        [[nodiscard]] auto last(T&& flur_obj)
        {
            auto seq = details::get_reference(flur_obj).compound();
            auto& rseq = details::get_reference(seq);
            rseq.start();
            if (!rseq.in_range())
            {
                throw std::out_of_range("taking `last` of empty lazy range");
            }
            details::sequence_element_type_t<T> result;
            for (; rseq.in_range(); rseq.next())
                result = rseq.current();
            return result;
        }

    }// ns:apply

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
