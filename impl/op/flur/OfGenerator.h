#pragma once
#ifndef _OP_FLUR_OFGENERATOR__H_
#define _OP_FLUR_OFGENERATOR__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/ftraits.h>

namespace OP
{
    /** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
    namespace flur
    {
        /** Sequence formed by generator functor
        *  \tparam Base - generator may support ordered or unordered sequence. Class doesn't provide 
        *       additional sorting instead it relies on generator function and developer responsibility
         * \tparam F - generator functor. F must return some value that supports contextually convertible 
         *              to bool (. casoperator `bool` or (not)) and 
         *              dereferencing `*`. So generator class will work out of box for raw-pointers, 
         *              std::optional, std::unique_ptr, std::shared_ptr and so on. Note, according to
         *          this https://stackoverflow.com/a/26895581/149818 std::optional cannot own reference
         *          so if need reference semantic use smart-pointers .
         *  F may have following signaures:
         *  \li no arg `f()`. 
         *  \li 1 input bool arg `f(bool)`, true mean start of generation and false for 
         *          consequnt calls. 
         *  \li 1 input size_t arg `f(size_t)`, index started from 0 used to 
         *          track order of invocation.
         */
        template <class Base, class F>
        struct Generator : Base
        {
            using base_t = Base;
            using element_t = typename base_t::element_t;
            using value_t = std::decay_t<element_t>;

            constexpr Generator(F f) noexcept
                : _generator(std::move(f)) 
            {
            }

            /** Start iteration from the beginning. If iteration was already in progress it resets.  */
            virtual void start() override
            {
                adaptive_start();
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
                adaptive_next();
            }
        private:
            F _generator;
            size_t _index = 0;
            using ftraits_t = OP::utils::function_traits<F>;
            using current_holder_t = typename ftraits_t::result_t;
            current_holder_t _current;

            void adaptive_start()
            {
                _index = 0;
                if constexpr (ftraits_t::arity_c == 1)
                {
                    constexpr bool is_arg_bool_c =
                            std::is_same_v<bool, typename ftraits_t::template arg_i <0>>;
                    static_assert(is_arg_bool_c/*clang compatibility*/, "Unsupported generator signature neither: f() nor f(bool)");
                    _current = std::move(_generator(true));
                }
                else
                {
                    constexpr bool is_zero_arity_c = ftraits_t::arity_c == 0;
                    static_assert(is_zero_arity_c, "Unsupported generator signature neither: f() nor f(bool)");
                    _current = std::move(_generator());
                }
            }

            void adaptive_next()
            {
                ++_index;
                if constexpr (ftraits_t::arity_c == 1)
                {
                    static_assert(
                        std::is_same_v<bool, typename ftraits_t::template arg_i<0>>,
                        "Unsupported generator signature neither: f() nor f(bool)");
                    _current = std::move(_generator(false));
                }
                else
                {
                    static_assert((ftraits_t::arity_c == 0), "Unsupported generator signature neither: f() nor f(bool)");
                    _current = std::move(_generator());
                }
            }

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
            using generator_base_t = std::conditional_t< ordered,
                OrderedSequence<const element_t&>,
                Sequence<const element_t&> >;
            using generator_t = Generator< generator_base_t, F>;

            constexpr GeneratorFactory(F&& f) noexcept
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
