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
            //sometimes on_start can have template definitions
            OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(on_start);
            //sometimes on_consume can have template definitions
            OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(on_consume);
        }

       
    }//ns:details


    template <class Lr, class TAction, std::enable_if_t<is_applicator_c<TAction>, int> = 0 >
    decltype(auto) collect_result(Lr&& factory, TAction& action)
    {
        constexpr bool factory_reference_v = std::is_lvalue_reference_v<Lr>;
        using base_lr_t = std::decay_t<details::dereference_t<Lr>>;
        //implement logic of c++20: std::forward_like - make `get_reference` result be similar to `factory` 
        using lazy_range_t = std::conditional_t<factory_reference_v, const base_lr_t&, base_lr_t&&>;

        auto seq = std::forward<lazy_range_t>(details::get_reference(factory)).compound();
        auto& rseq = details::get_reference(seq);

        using sequence_t = std::decay_t<decltype(rseq)>;
        decltype(auto) collector = action.collect_for(rseq);
        using collector_t = std::decay_t<decltype(collector)>;
        
        if constexpr(
            details::has_on_start<collector_t>::value
            || details::extra::has_on_start<collector_t, sequence_t>::value)
        {
            //delegate start of sequence to implementation
            collector.on_start(rseq);
        }
        else //default start implementation
            rseq.start();

        if constexpr (
            details::has_on_consume<collector_t>::value
            || details::extra::has_on_consume<collector_t, sequence_t>::value)
        {
            for (; rseq.in_range(); rseq.next())
            {
                collector.on_consume(rseq);
            }
        }

        if constexpr(details::has_on_complete<collector_t>::value)
        {
            return collector.on_complete();
        }
    }

    template <class TFactory, class TApplicator, 
        std::enable_if_t<is_factory_c<TFactory> && is_applicator_c<TApplicator>, int> = 0>
    decltype(auto) operator >>= (std::shared_ptr<TFactory> factory, TApplicator&& applicator)
    {
        return collect_result(*factory, std::forward<TApplicator>(applicator));
    }

    template <class TFactory, class TApplicator, 
        std::enable_if_t<is_factory_c<TFactory> && is_applicator_c<TApplicator>, int> = 0>
    decltype(auto) operator >>= (std::shared_ptr<TFactory> factory, const TApplicator& applicator)
    {
        return collect_result(*factory, applicator);
    }

    template <class OutputIterator>
    struct Drain : ApplicatorBase
    {
        explicit Drain(OutputIterator out_iter) noexcept
            : _out_iter(std::move(out_iter))
        {
        }

        template <class TSeq>
        auto& collect_for(TSeq& rseq) noexcept
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


    /** placeholder to postpone target type detection */
    struct AsSequence{};

    /** @brief Namespace providing generic and specialized implementations of 
    *        sum and multiplication algorithms.
    *
    * This namespace defines algorithms that follow the sum accumulation pattern, 
    * supporting both addition and multiplication operations for generic types.
    * Specializations for floating-point and arithmetic types are included to 
    * reduce numerical error accumulation and improve precision in computation-heavy contexts.
    */
    namespace collectors 
    {
       /**
        * @brief Provides a default collector suitable for most use cases.
        *
        * This collector performs basic accumulation using the provided operation.
        * For floating-point arithmetic, consider using alternatives (\sa PairwiseSumCollector, 
        *   KahanSumCollector, NeumaierSumCollector).
        * to reduce the risk of numerical error accumulation.
        *
        * @tparam TSum  The result type of the accumulation.
        * @tparam TSeq  The source sequence type to aggregate.
        * @tparam Op    The accumulation functor. Defaults to `std::plus<>`.
        *               You may substitute any functor implementing the binary operator:
        *               `TSeq::element_t operator()(TSeq::element_t, TSeq::element_t)`, for example `std::multiplies`.
        */
        template <class TSum, class TSeq, template <typename> class Op>
        struct DefaultSumCollector
        {
            TSum _dest;
            Op<std::decay_t<TSum>> _action;

            explicit constexpr DefaultSumCollector(TSum dest, Op<std::decay_t<TSum>> action = {}) noexcept
                : _dest{ dest }
                , _action(std::move(action))
            {}

            explicit constexpr DefaultSumCollector(Op<std::decay_t<TSum>> action = {}) noexcept
                : _dest{}
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

        template <template <typename> class Op>
        struct ArgReducer
        {
            template <class T, class TSeq>
            using default_sum_collector_with_op_t = collectors::DefaultSumCollector<T, TSeq, Op>;
        };

        /**
         * @brief Summation collector uses pairwise summation to improve numerical stability.
         *
         * This collector divides the input sequence and sums values in pairs,
         * reducing rounding error by maintaining better balance between large and small terms.
         * It is particularly effective for large datasets or where precision loss due to ordering
         * is a concern.
         *
         * Compared to naive accumulation, it significantly reduces numerical drift,
         * but may still be less accurate than Kahan or Neumaier methods for extreme cases.
         */
        template <class TSum, class TSeq, 
            std::enable_if_t<std::is_floating_point_v<std::decay_t<TSum>>, int> = 0>
        struct PairwiseSumCollector
        {
            using deref_sum_t = std::decay_t<TSum>;
            
            TSum _dest;
            std::array<deref_sum_t, 2> _accumulator{};
            deref_sum_t _odd_sum{};
            size_t _offset = 0, _gen = 0;

            explicit constexpr PairwiseSumCollector(TSum dest) noexcept
                : _dest{ dest }
            {
            }

            constexpr PairwiseSumCollector() noexcept
                : _dest{}
            {
            }

            void on_consume(const TSeq& seq)
            {
                _accumulator[_offset++] = seq.current(); //accumulate pairs
                if(_offset == _accumulator.size())
                {
                    //need suppress optimization on pair sum
                    volatile deref_sum_t pair_sum = (_accumulator[0] + _accumulator[1]);
                    *((_gen ++ & 1) ? &_dest : &_odd_sum) += pair_sum;
                    _offset = 0;
                }
            }

            decltype(auto) on_complete() noexcept
            {
                if(_offset == 1) //handle odd number
                    _dest += _accumulator[0];
                _dest += _odd_sum;
                return _dest;
            }
        };

        /**
         * @brief Summation collector uses the Kahan summation algorithm.
         *
         * This collector applies compensation for lost low-order bits during summation
         * by tracking a running error term. It adds a correction factor on each iteration,
         * significantly improving accuracy in floating-point addition.
         *
         * Especially effective when many small numbers are added to a much larger one.
         * More accurate than pairwise summation, but incurs slightly more computational cost.
         */
        template <class TSum, class TSeq,
            std::enable_if_t<std::is_floating_point_v<TSum>, int> = 0>
        struct KahanSumCollector
        {
            using deref_sum_t = std::decay_t<TSum>;
            TSum _dest;
            deref_sum_t _compensation{};

            explicit constexpr KahanSumCollector(TSum dest) noexcept
                : _dest{ dest }
            {
            }

            explicit constexpr KahanSumCollector() noexcept
                : _dest{}
            {
            }

            void on_consume(const TSeq& seq)
            {
                deref_sum_t delta = seq.current() - _compensation;
                deref_sum_t result = _dest + delta;
                _compensation = (result - _dest) - delta;
                _dest = result;
            }

            decltype(auto) on_complete() noexcept
            {
                return _dest;
            }
        };

        /**
         * @brief Accumulator using the Neumaier summation algorithm.
         *
         * A refined version of Kahan's method, Neumaier's algorithm compensates rounding errors
         * even when the next term is larger in magnitude than the current sum.
         * It maintains both the total sum and a separate compensation term.
         *
         * Offers higher precision but slower than Kahan in cases with large magnitude variation,
         * and is generally more robust in real-world scenarios where cancellation is common.
         */
        template <class TSum, class TSeq,
            std::enable_if_t<std::is_floating_point_v<TSum>, int> = 0>
        struct NeumaierSumCollector
        {
            using deref_sum_t = std::decay_t<TSum>;
            TSum _dest;
            deref_sum_t _compensation{};

            explicit constexpr NeumaierSumCollector(TSum dest) noexcept
                : _dest{ dest }
            {
            }

            explicit constexpr NeumaierSumCollector() noexcept
                : _dest{}
            {
            }

            void on_consume(const TSeq& seq)
            {
                deref_sum_t item = seq.current();
                deref_sum_t result = _dest + item;
                
                if(std::abs(_dest) >= std::abs(item))
                    _compensation += (_dest - result) + item;
                else
                    _compensation += (item - result) + _dest;

                _dest = result;
            }

            decltype(auto) on_complete() noexcept
            {
                _dest += _compensation; //apply compensation to the result
                return _dest;
            }
        };
    }//ns:collectors

    /**
    *
    * \tparam T type of destination result, it is allowed to be `AsSequence` - meaning result is the same as TSequence::element_t.
    */
    template <class T, template <typename, typename> class TCollector>
    class Sum : ApplicatorBase
    {
        using pointer_t = std::add_pointer_t<T>;
        using reference_t = std::add_lvalue_reference_t<T>;
    public:

        constexpr Sum() noexcept = default;

        explicit constexpr Sum(reference_t dest) noexcept
            : _dest(&dest)
        {
        }

        template <class TSeq>
        auto collect_for(TSeq& rseq) noexcept
        {
            if constexpr(std::is_same_v<T, AsSequence>)
            {
                using element_t = std::decay_t<details::sequence_element_type_t<TSeq>>;
                return TCollector<element_t, TSeq>{};
            }
            else
                return TCollector<reference_t, TSeq>{*_dest};
        }

    private:

        pointer_t _dest;
    };

    template <class T>
    struct Count : ApplicatorBase
    {
        explicit constexpr Count(T counter) noexcept
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
        explicit constexpr ForEach(TUnaryFunction f) noexcept
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

    struct FirstImpl : ApplicatorBase
    {
        template <class TSeq>
        struct Collect
        {
            explicit Collect(TSeq& origin) noexcept
                : _origin(origin) //keep reference
            {
            }

            decltype(auto) on_complete() const
            {
                if(!_origin.in_range())
                    throw std::out_of_range("taking `first` of empty lazy range");
                return _origin.current();
            }
            TSeq& _origin;
        };

        template <class TSeq>
        auto collect_for(TSeq& rseq) const noexcept
        {
            return Collect<TSeq>(rseq);
        }
        
        /** mimic to regular function */
        template <class T>
        [[nodiscard]] decltype(auto) operator()(T&& flur_obj) const
        {
            return collect_result(std::forward<T>(flur_obj), *this);
        }
    };

    struct LastImpl : ApplicatorBase
    {
        template <class TSeq>
        struct Collect
        {
            using last_t = std::decay_t< details::sequence_element_type_t<TSeq> >;
            Collect() = default;
            
            void on_consume(const TSeq& seq)
            {
                _last = seq.current();
            }

            //Implementation uses `auto` (instead of `decltype(auto)`) and 
            // copy/move semantic to return result to avoid dangling references
            auto on_complete()
            {
                if(!_last.has_value())
                    throw std::out_of_range("taking `last` of empty lazy range");
                return *std::move(_last);
            }
            
            std::optional<last_t> _last;
        };

        template <class TSeq>
        auto collect_for(TSeq& rseq) const noexcept
        {
            return Collect<TSeq>{};
        }

        template <class T>
        [[nodiscard]] decltype(auto) operator()(T&& flur_obj) const
        {
            return collect_result(std::forward<T>(flur_obj), *this);
        }
    };

    namespace apply
    {
        /**
        * \brief Consume all from sequence using output iterator.
        *
        *   \tparam OutputIterator - some destination that supports `*` and (postfix)`++`. For reference example see
        *       std::insert_iterator, std::back_inserter, std::front_inserter
        */
        template <class OutputIterator>
        constexpr auto drain(OutputIterator&& out_iter) noexcept
        {
            return Drain<OutputIterator>(std::forward<OutputIterator>(out_iter));
        }

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
            
            return Sum<T&, 
                collectors::ArgReducer<std::plus>::template default_sum_collector_with_op_t>(result);
        }

        constexpr auto sum() noexcept
        {
            return Sum<AsSequence, 
                collectors::ArgReducer<std::plus>::template default_sum_collector_with_op_t>{};
        }

        template <template <typename> class Op, class T>
        constexpr auto sum(T& result) noexcept
        {
            return Sum<T, 
                collectors::ArgReducer<Op>::template default_sum_collector_with_op_t>(result);
        }

        template <class TSum, class TSeq>
        using sum_pairwise_t = collectors::PairwiseSumCollector<TSum, TSeq>;

        template <class TSum, class TSeq>
        using sum_kahan_t = collectors::KahanSumCollector<TSum, TSeq>;

        template <class TSum, class TSeq>
        using sum_neumaier_t = collectors::NeumaierSumCollector<TSum, TSeq>;
        /**
        * @brief Create applicator to summate floating point sequence with improved precision.
        * 
        * When you apply naive `apply::sum` to sequence of floating points (including `float`, `double` and `long double`)
        *   this implementation can accumulate rounding errors that amplifies as sequence size grows. To reduce
        *   errors special algorithms can be applied. Following table allows you select correct algorithm:
        * 
        * Collector                        | Accuracy | Cost  | Best for
        * ---------------------------------|----------|-------|---------------------------
        * naive (if you use `sum()`)       |Very Low  | Low   | Non floating point types.
        * collectors::PairwiseSumCollector | Medium   | Low   | Balanced summation with mild error risk
        * (alias: sum_pairwise_t)          |          |       |
        * collectors::KahanSumCollector    |High      |Medium | Sequences with many additions. Loses precision when adding a small number 
        * (alias: sum_kahan_t)             |          |       | to a much larger sum.
        * collectors::NeumaierSumCollector |Very High |Above medium | Datasets with large magnitude variation.
        * (alias: sum_neumaier_t)          |          |        |
        *
        * @param result destination result, has one of the floating point types: `float`, `double` or `long double`.
        * @tparam TCollector summation algorithm, one of the: `sum_pairwise_t`, `sum_kahan_t` or `sum_neumaier_t`.
        */ 
        template <template <typename, typename> class TCollector, class T>
        constexpr decltype(auto) fsum(T& result) noexcept
        {
            return Sum<T, TCollector>{result};
        }

        /** @brief Create applicator to summate floating point sequence with improved precision.
        * 
        * The same as `fsum(T&)`, but evaluate result type by collected input sequence. 
        */ 
        template <template <typename, typename> class TCollector = sum_pairwise_t>
        constexpr auto fsum() noexcept
        {
            return Sum<AsSequence, TCollector>();
        }

        template <class T, class TBinaryConsumer>
        constexpr auto reduce(T& target, TBinaryConsumer consumer) noexcept
        {
            return Reduce<T, TBinaryConsumer>(target, std::move(consumer));
        }

        /** \brief takes the first element of non-empty LazyRange.
        *   Takes only the first element of LazyRange and raises exception when source is empty. 
        *   Function can be used as regular call or as part of applicator syntax using operator `>>=`. For example:\code
        *   using namespace OP::flur;
        *   apply::first(src::of_value(57)); //returns 57
        *   \endcode
        *   The same with consuming over operator `>>=`: \code
        *   using namespace OP::flur;
        *   src::of_value(57) >>= apply::first; //returns 57
        *   \endcode
        *
        *   \throws std::out_of4_range when source LazyRange produces empty sequence.
        */
        constexpr static inline FirstImpl first{};

        constexpr static inline LastImpl last{};

        //template <class T >
        //[[nodiscard]] auto first(T&& flur_obj)
        //{
        //    auto seq = details::get_reference(flur_obj).compound();
        //    auto& rseq = details::get_reference(seq);
        //    rseq.start();
        //    if (!rseq.in_range())
        //    {
        //        throw std::out_of_range("taking `first` of empty lazy range");
        //    }
        //    return rseq.current();
        //}

        //template <class T>
        //[[nodiscard]] auto last(T&& flur_obj)
        //{
        //    auto seq = std::forward<T>(details::get_reference(flur_obj)).compound();
        //    auto& rseq = details::get_reference(seq);
        //    rseq.start();
        //    if (!rseq.in_range())
        //    {
        //        throw std::out_of_range("taking `last` of empty lazy range");
        //    }
        //    details::sequence_element_type_t<T> result;
        //    for (; rseq.in_range(); rseq.next())
        //        result = rseq.current();
        //    return result;
        //}

    }// ns:apply



    template <class TCartesianFactory>
    struct CartesianApplicator : ApplicatorBase
    {
        explicit constexpr CartesianApplicator(TCartesianFactory&& crtf) noexcept
            : _factory(std::move(crtf))
        {
        }
        

        template <class TSeq>
        auto collect_for(TSeq& origin) noexcept
        {
            auto crtseq = _factory.compound(std::ref(origin));
            return Collector<decltype(crtseq)>{ std::move(crtseq) };
        }

    private:

        template <class TCartesianSeq>
        struct Collector
        {
            TCartesianSeq _cartesian_sequence;

            Collector(TCartesianSeq&& seq)
                : _cartesian_sequence(std::move(seq))
            {
            }

            template <class T>
            void on_start(T&)
            {
                _cartesian_sequence.start();
            }

            void on_complete()
            {
                for(;_cartesian_sequence.in_range(); _cartesian_sequence.next())
                    static_cast<void>(_cartesian_sequence.current());//consume
            }
        };
        
        TCartesianFactory _factory;
    };

} //ns: OP::flur

#endif //_OP_FLUR_APPLICATOR__H_
