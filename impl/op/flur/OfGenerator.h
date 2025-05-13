#pragma once
#ifndef _OP_FLUR_OFGENERATOR__H_
#define _OP_FLUR_OFGENERATOR__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/common/Currying.h>
#include <op/common/ftraits.h>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/SequenceState.h>

namespace OP::flur
{
    
    /** @brief Define a sequence rendered by a generator functor.
    *
    * @tparam Base The base type for the sequence. The generator may support ordered or unordered sequences.
    *              This class does not provide additional sorting; it relies on the generator function and 
    *              the developer's responsibility.
    * @tparam F The generator functor that produces next value each time it is called. 
    *           F may have the following signatures:
    *           - Zero argument function: `std::optional<T> f()` take next value or stop if result is empty. 
    *           - One argument of sequence state : 
    *               `std::optional<T> f(OP::flur::SequenceState& state)`
    *               Typical use cases for `state` argument:
    *               - Use `state.step() == 0` to check the current step or the beginning of the sequence.
    *               - Use `state.generation() == 0` to check how many times the sequence has been restarted.
    */
    template <class Base, class F>
    struct Generator : Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using value_t = std::decay_t<element_t>;

        explicit constexpr Generator(F f) noexcept
            : _generator(std::move(f))
            , _sstate{}
        {
        }

        /** Start iteration from the beginning. If iteration was already in progress it resets.  */
        virtual void start() override
        {
            _sstate.start();
            _current = OP::currying::recomb_call(_generator, _sstate);
        }
        
        /** Check if Sequence is in valid position and may call `next` safely */
        virtual bool in_range() const override
        {
            return !(!_current) && !_sstate.is_stopped();
        }
        
        /** Return current item */
        virtual element_t current() const override
        {
            return *_current;
        }

        /** Position iterable to the next step */
        virtual void next() override
        {
            _current = OP::currying::recomb_call(_generator, _sstate);
            _sstate.next();
        }

    private:
        using deref_element_t = std::decay_t<element_t>;
        
        std::optional<deref_element_t> _current{};

        F _generator;
        SequenceState _sstate;
    };

    namespace details
    {
        
        template <class Tuple>
        struct GeneratorDecompose
        {
            struct IsValid
            {
                template <class T>
                static constexpr bool check = 
                    details::is_optional<std::decay_t<T>>::value;
            }; 
            using any_of_t = typename OP::utils::TypeFilter<IsValid, Tuple>::type;

            constexpr static bool is_valid_c = std::tuple_size_v<any_of_t> > 0;

            using value_holder_t = std::conditional_t<
                is_valid_c,
                std::decay_t<std::tuple_element_t<0, any_of_t>>,
                void
            >;
        };
    }

    /**
    *
    * \tparam F - Functor that produces std::optional<?> or raw-pointer. For details see Generator comments on this parameter
    * \tparam ordered - if generator produce ordered sequence
    */
    template <class F, bool ordered>
    class GeneratorFactory : FactoryBase
    {
        F _gen;
    public:
        using ftraits_t = OP::utils::function_traits<F>;
        using value_holder_t = typename ftraits_t::result_t;

        static_assert(
            details::is_optional<value_holder_t>::value,
            "Generator function must match one of the following signature: `std::optional<T> f()` or `std::optional<T> f(SequenceState&)`"
        );

        using element_t = typename value_holder_t::value_type;

        using generator_base_t = std::conditional_t< 
                ordered,
                OrderedSequence<const element_t&>,
                Sequence<const element_t&>
            >;

        using generator_t = Generator<generator_base_t, F>;

        explicit constexpr GeneratorFactory(F&& f) noexcept
            : _gen(std::forward<F>(f))
        {
        }

        constexpr auto compound() const& noexcept
        {
            return generator_t(_gen);
        }

        constexpr auto compound() && noexcept
        {
            return generator_t(std::move(_gen));
        }
    };

} //ns:OP::flur

#endif // _OP_FLUR_OFGENERATOR__H_
