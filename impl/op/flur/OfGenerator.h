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
#include <op/flur/Ingredients.h>

namespace OP
{
    /** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
    namespace flur
    {
        /** @brief Define a sequence formed by a generator functor.
        *
        * @tparam Base The base type for the sequence. The generator may support ordered or unordered sequences.
        *              This class does not provide additional sorting; it relies on the generator function and 
        *              the developer's responsibility.
        * @tparam F The generator functor. The functor must return a value that is contextually convertible to 
        *           bool (i.e., supports `operator bool` or an equivalent operator). Additionally, it must 
        *           support dereferencing via `*`. Therefore, the generator class will work out-of-the-box for 
        *           raw pointers, std::optional, std::unique_ptr, std::shared_ptr and so on. Note, according to
        *           this https://stackoverflow.com/a/26895581/149818 std::optional cannot own reference
        *           so if need reference semantic use smart-pointers.
        *           F may have the following input arguments:
        *           - No arguments (`f()`).
        *           - `const SequenceState&` to expose the current state of the sequence. For example:
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
            {
            }

            /** Start iteration from the beginning. If iteration was already in progress it resets.  */
            virtual void start() override
            {
                std::get<SequenceState>(_attrs.arguments()).start();
                _current = _attrs.invoke(_generator);
            }
            
            /** Check if Sequence is in valid position and may call `next` safely */
            virtual bool in_range() const override
            {
                return !(!_current);
            }
            
            /** Return current item */
            virtual element_t current() const override
            {
                return *_current;
            }

            /** Position iterable to the next step */
            virtual void next() override
            {
                std::get<SequenceState>(_attrs.arguments()).next();
                _current = _attrs.invoke(_generator);
            }

        private:
            using ftraits_t = OP::utils::function_traits<F>;
            using current_holder_t = typename ftraits_t::result_t;
            
            current_holder_t _current{};

            F _generator;
            OP::currying::CurryingTuple<SequenceState> _attrs;
        };

        namespace details
        {
            template <typename A>
            class has_deref_operator
            { 
                typedef char YesType[1]; 
                typedef char NoType[2]; 
                template <typename C> static std::enable_if_t< !std::is_same_v<void, decltype(*std::declval<C>())>, YesType&> test( void* = nullptr ) ; 
                template <typename C> static NoType& test(...); 
            public: 
                enum { value = sizeof(test<A>(nullptr)) == sizeof(YesType) }; 
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
            using result_t = typename ftraits_t::result_t; //decltype(decl_r(std::declval<F>()));
            static_assert(
                details::has_deref_operator<result_t>::value && 
                    std::is_constructible_v<bool, result_t>,
                "Generator must be contextually convertible to bool (like std::optional<?>, std::shared_ptr<?>, std::unique_ptr<?> or raw-pointer)");

            using element_t = std::decay_t<decltype(*std::declval<result_t>())>;
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

    } //ns:flur
} //ns:OP

#endif // _OP_FLUR_OFGENERATOR__H_
