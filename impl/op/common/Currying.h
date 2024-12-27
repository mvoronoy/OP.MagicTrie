#ifndef _OP_COMMON_CURRYING__H_
#define _OP_COMMON_CURRYING__H_

#include <tuple>
#include <any>
#include <op/common/ftraits.h>
#include <op/common/Utils.h>

namespace OP::currying
{
    
    /** Marker that allows add extra behaviour for Currying argument evaluation. This is a base
    * class that in implementation must provide some no-arg `operator ()()`;
    */
    struct CurryingArgSpec{};

    /** Functor is invoked each time to evaluate correct argument value for some method invocation */
    template <class TCallable>
    struct Functor : CurryingArgSpec
    {
        using result_t = decltype(std::declval<TCallable>()());

        explicit constexpr Functor(TCallable callable) noexcept
            : _callable(std::move(callable))
        {
        }

        decltype(auto) operator()()
        {
            return _callable();
        }

        template <typename>
        decltype(auto) extract()
        {
            return _callable();
        }

        template <typename U>
        static constexpr bool can_handle() noexcept
        {
            return std::is_same_v<
                std::remove_reference_t<result_t>, std::remove_reference_t<U> >;
        }

    private:
        TCallable _callable;
    };

    template <class M, class TClass, class ... Args>
    struct MemberFunctor : CurryingArgSpec
    {

        constexpr MemberFunctor(TClass inst, M method, Args&&...args) noexcept
            : _inst(inst)
            , _method(method)
            , _args(std::forward<Args>(args)...)
        {
        }

        decltype(auto) operator()()
        {
            return std::apply([this](auto&& ...args) -> decltype(auto){
                return (_inst.*_method)(std::forward<decltype(args)>(args)...);
            }, _args);
        }

        decltype(auto) operator()() const
        {
            return std::apply([this](auto&& ...args) -> decltype(auto){
                return (_inst.*_method)(std::forward<decltype(args)>(args)...);
            }, _args);
        }

        template <typename>
        decltype(auto) extract()
        {
            return operator()();
        }

        template <typename U>
        static constexpr bool can_handle()
        {
            using method_traits_t = OP::utils::function_traits<M>;

            return std::is_same_v<
                std::remove_reference_t<typename method_traits_t::result_t>, 
                std::remove_reference_t<U> >;
        }

    private:
        TClass _inst;
        M _method;
        std::tuple<Args...> _args;
    };

    /** Var allows bind external variable to a function argument */ 
    template <class T>
    struct Var : CurryingArgSpec
    {
        using result_t = T&;

        explicit constexpr Var(T& val) noexcept:
            _val(&val)
        {}

        constexpr Var() noexcept:
            _val(nullptr)
        {}

        template <typename U>
        static constexpr bool can_handle() noexcept
        {
            return std::is_same_v<std::decay_t<T>, std::decay_t<U> >;
        }

        decltype(auto) operator()()
        {
            return *_val;
        }

        template <class U>
        void set(U&& val)
        {
            *_val = std::forward<U>(val);
        }

        operator T& ()
        {
            return *_val;
        }

        template <typename>
        auto& extract()
        {
            return *_val;
        }

        template <typename>
        const auto& extract() const
        {
            return *_val;
        }

    private:
        T* _val;
    };

    /** Similar to Var but deals with types that packed to some wrapper (like std::variant or std::any).
    * Allows to extract expected argument type
    */
    template <class T>
    struct Unpackable : CurryingArgSpec
    {
        template <typename U>
        static constexpr bool can_handle() noexcept;

        template <typename U>
        auto& extract();
    };

    template <>
    struct Unpackable<std::any> : CurryingArgSpec
    {
        explicit constexpr Unpackable(std::any& val) noexcept
            : _val(&val)
        {}

        constexpr Unpackable() noexcept
            : _val(nullptr)
        {}

        template <typename >
        static constexpr bool can_handle() noexcept
        {
            return true;
        }
        
        /**
        *   \throws std::bad_any_cast
        */
        template <typename U>
        auto& extract()
        {
            if constexpr(std::is_same_v<std::decay_t<U>, std::any>)
                return *_val;
            else
            {
                return std::any_cast<std::add_lvalue_reference_t<std::decay_t<U>>>(*_val);
            }
        }

    private:
        std::any* _val;
    };

    template <class T>
    auto of_unpackable(T&& t)
    {
        return Unpackable<std::decay_t<T>>(std::forward<T>(t));
    }

    template <class T>
    auto of_callable(T t)
    {
        return Functor(std::move(t));
    }

    template <class M, class TClass, class ... Args>
    constexpr auto of_member(const TClass& inst, M(TClass::* method)(Args...) const, Args&& ...args)
    {
        return MemberFunctor<decltype(method), const TClass&, Args...>{inst, method, std::forward<Args>(args)...};
    }

    /**
    * Create callable wrapper around member of arbitrary class that can be used 
    * together with CurryingTuple in role of `TCallable` template parameter.
    * Result is lambda with `TClass&` as a first argument.
    */
    template <class R, class TClass, class ... Args>
    constexpr auto of_member(TClass& inst, R(TClass::* method)(Args...), Args&& ...args)
    {
        return MemberFunctor<decltype(method), TClass&, Args...>{inst, method, std::forward<Args>(args)...};
    }

    template <class T>
    auto of_var()
    {
        return Var<T>();
    }

    template <class T>
    auto of_var(T&& t)
    {
        return Var<std::decay_t<T>>(std::forward<T>(t));
    }

    namespace det
    {
        template <class TDest, class TSource>
        struct CanSubstAsArg
        {
            static long test(TDest);
            static char test(...);
            constexpr static inline bool value = sizeof(decltype(test(std::declval<TSource>()))) == sizeof(long);
        };

        template <class TDest, class TSource>
        constexpr static inline bool can_subst_as_arg_v = CanSubstAsArg<TDest, TSource>::value;

        template <class TDest, class TSource, typename Enabler = void>
        struct Injector
        {
            using src_arg_ref = std::add_lvalue_reference_t<TSource>;

            /** compile-time checker if TSource can be injected as an argument to TDest 
            * \param strong used either to weak or strong match for type matching.
            */
            constexpr static bool can_handle(bool strong) noexcept
            {
                return strong 
                    ? std::is_same_v<std::remove_reference_t<TDest>, std::remove_reference_t<TSource>>
                    : can_subst_as_arg_v<TDest, TSource>;
            }
            
            /** Simplest case to inject argument by apply non-conflicting type cast */
            constexpr static TDest inject(src_arg_ref src) noexcept
            {
                return static_cast<TDest>(src);
            }
        };

        template <class TDest, class TSource>
        struct Injector<TDest, std::reference_wrapper<TSource>, void>
        {
            constexpr static bool can_handle(bool /*strong*/) noexcept
            {
                return std::is_same_v<std::remove_reference_t<TDest>, TSource>;
            }

            constexpr static TDest& inject(std::reference_wrapper<TSource>& src)
            {
                return src.get();
            }
        };

        template <class TDest, class TSource>
        struct Injector<TDest, TSource,
            std::enable_if_t<std::is_base_of_v<OP::currying::CurryingArgSpec, std::decay_t<TSource>>>>
        {
            constexpr static decltype(auto) inject(TSource& src)
            {
                return src.template extract<TDest>();
            }

            constexpr static bool can_handle(bool strong) noexcept
            {
                using real_dest_t = std::remove_reference_t< decltype(inject(*(std::add_pointer_t<TSource>)(nullptr))) >;
                return strong
                    //check result type vs expected type
                    ? std::is_same_v<std::remove_reference_t<TDest>, real_dest_t>
                    : std::decay_t<TSource>::template can_handle<TDest>();
            }

        };

        template <class TDest, class Tuple, size_t I = 0>
        constexpr size_t index_of() noexcept
        {
            static_assert(I < std::tuple_size_v<Tuple>, "No of Tuple elements can be used to inject argument of type TDest");
            using element_t = std::tuple_element_t<I, Tuple>;
            if constexpr (Injector<TDest, element_t>::can_handle(false))
                return I;
            else
                return index_of<TDest, Tuple, I + 1>();
        }

        template <class TDest, class Tuple>
        constexpr decltype(auto) as_argument_weak(Tuple& args)
        {
            constexpr size_t I = index_of<TDest, Tuple>();
            return Injector<TDest, std::tuple_element_t< I, Tuple >>::inject(
                std::get< I >(args));
        }

        //template <class TDest>
        //struct WeakCanHandlePredicate
        //{
        //    template <class TSource>
        //    static constexpr bool check = Injector<TDest, TSource>::can_handle(false);
        //};

        template <class TDest>
        struct CanHandlePredicate
        {
            template <class TSource>
            static constexpr bool check = Injector<TDest, TSource>::can_handle(true);
        };

        template <class TDest, class Tuple>
        constexpr decltype(auto) as_argument(Tuple& args)
        {
            //using narrow_t = typename OP::utils::TypeFilter< WeakCanHandlePredicate<TDest>, Tuple>::type;
            using exact_match_t = typename OP::utils::TypeFilter<CanHandlePredicate<TDest>, Tuple>::type;
            if constexpr (std::tuple_size_v<exact_match_t> == 0)
            { //there is no strong match
                return as_argument_weak<TDest>(args);
            }
            else
            {
                using best_match_t = std::tuple_element_t< 0, exact_match_t >;
                return Injector<TDest, best_match_t>::inject(
                    std::get<best_match_t>(args));
            }
        }

    }//ns:det

    template <class ... Tx>
    struct CurryingTuple
    {
        using this_t = CurryingTuple<Tx...>;
        using arguments_t = std::tuple<Tx...>;
        
        constexpr CurryingTuple() = default;

        template <class ...Ux>
        constexpr CurryingTuple(Ux&& ...ax) noexcept
            : _arguments{ std::forward<Ux>(ax)... }
        {}

        explicit constexpr CurryingTuple(std::tuple<Tx ...>&& ax) noexcept
            : _arguments{ std::forward<std::tuple<Tx ...>>(ax) }
        {}

        auto& arguments() noexcept
        {
            return _arguments;
        }

        const auto& arguments() const noexcept
        {
            return _arguments;
        }

        template <class T>
        auto& assign(T&& t)
        {
            std::get<T>(_arguments) = std::forward<T>(t);
            return *this;
        }

        template <class T>
        decltype(auto) eval_arg() 
        {
            return det::as_argument<T>(_arguments);
        }
        
        template <class T>
        decltype(auto) eval_arg() const
        {
            return det::as_argument<T>(_arguments);
        }

        /** Create functor of zero-arguments by substitution args from this tuple to argument function */
        template <class TCallable>
        constexpr decltype(auto) def(TCallable f)& noexcept
        {
            return [f = std::move(f), this]() -> decltype(auto){
                return this->invoke(f);
            };
        }

        /** Create functor of zero-arguments by substitution args from this tuple to argument function */
        template <class TCallable>
        constexpr decltype(auto) def(TCallable func)&& noexcept
        {
            return of_callable([
                func = std::move(func), 
                args = std::move(*this)
                ]() mutable -> decltype(auto){
                    return args.invoke(func);
            });
        }

        /** Invoke functor `func` with positional substitution from this tuple */
        template <typename TCallable, typename ... Ax>
        constexpr decltype(auto) invoke(TCallable func, Ax&&...ax) &
        {
            using traits_t = OP::utils::function_traits<TCallable>;
            if constexpr (sizeof...(Ax))
            {
                return do_invoke(func, std::tuple_cat(
                    _arguments,
                    std::forward_as_tuple(std::forward<Ax>(ax)...)), 
                    std::make_index_sequence<traits_t::arity_c>{});
            }
            else
                return do_invoke(func, _arguments, std::make_index_sequence<traits_t::arity_c>{});
        }

        template <typename TCallable, typename ... Ax>
        constexpr decltype(auto) invoke(TCallable func, Ax&&...ax) &&
        {
            using traits_t = OP::utils::function_traits<TCallable>;
            if constexpr (sizeof...(Ax))
            {
                return do_invoke(func, std::tuple_cat(
                    std::move(_arguments),
                    std::forward_as_tuple(std::forward<Ax>(ax)...)), std::make_index_sequence<traits_t::arity_c>{});
            }
            else
                return do_invoke(func, std::move(_arguments), std::make_index_sequence<traits_t::arity_c>{});
        }

        template <typename TCallable, typename ... Ax>
        decltype(auto) invoke_back(TCallable& func, Ax&&...ax)
        {
            using traits_t = OP::utils::function_traits<TCallable>;
            return do_invoke(func, std::tuple_cat(std::forward_as_tuple(std::forward<Ax>(ax)...), _arguments),
                std::make_index_sequence<traits_t::arity_c>{});
        }

        /**
        * The same way as #typed_def collates existing arguments and bind them to the TCallable 
        * arguments using type matching, but keeps N front arguments unbound to allow specify during a call time.
        * @return functor of N arguments where other expected bind to the current tuple.
        * 
        * *Example*:\code
        * float hypot3d(float x, float y, float z)
        * {
        *     return std::hypot(x, y, z); //use float function `std::hypot` from <cmath>
        * }
        * 
        * auto free_x_y = arguments(2.f). //this value will pass as 3d argument z
        *                 typed_bind_free_front<2>(hypot3d) //reserve 2 free front argument
        * std::cout << "(Should be 7.)hypot = " << free_x_y(
        *   3.f,  //x
        *   6.f   //y 
        * ) << "\n";
        * \endcode
        */
    //private:
        arguments_t _arguments;

        template <typename TCallable, typename Tuple, size_t ... I>
        static decltype(auto) do_invoke(TCallable& func, Tuple&& args, std::index_sequence<I...>)
        {
            using traits_t = OP::utils::function_traits<TCallable>;
            return func(
                    det::as_argument<typename traits_t::template arg_i<I>>(args)
                    ...
            );
        }

        
        //template<typename>
        //struct sfinae_true : std::true_type {};
        //template <class TDest, class TSource> static auto test_deref_exists(int) -> sfinae_true<decltype(
        //    det::deref<TDest>(std::declval<TSource>(), utils::priority_tag<2>{})) >;
        //template <class TDest, class TSource> static auto test_deref_exists(long) -> std::false_type;
        //
        //template <class TDest, class TSource> constexpr static inline bool test_deref_exists_v =
        //    decltype(test_deref_exists<TDest, TSource>(0))::value;

    };

    template <class ...Tx>
    constexpr auto arguments(Tx&& ...tx)
    {
        return CurryingTuple{ std::make_tuple(std::forward<Tx>(tx)...) };
    }

    /** 
    * Create callable wrapper around member of arbitrary class that can be used
    * together with CurryingTuple in role of `TCallable` template parameter.
    * Result is lambda with `const TClass&` as a first argument.
    */ 
    template <class M, class TClass, class ... Args>
    constexpr auto use_member(M(TClass::* method)(Args...) const)
    {
        return [method](const TClass& inst, Args... args) {return (inst.*method)(args...); };
    }

    /**
    * Create callable wrapper around member of arbitrary class that can be used 
    * together with CurryingTuple in role of `TCallable` template parameter.
    * Result is lambda with `TClass&` as a first argument.
    */
    template <class M, class TClass, class ... Args>
    constexpr auto use_member(M(TClass::* method)(Args...))
    {
        return [method](TClass& inst, Args... args) {return (inst.*method)(args...); };
    }

    template<class... Ts> struct ArgInjectors : Ts... { using Ts::operator()...; };
    // explicit deduction guide (not needed as of C++20)
    template<class... Ts> ArgInjectors(Ts...) -> ArgInjectors<Ts...>;

}//ns:OP::currying

#endif //_OP_COMMON_CURRYING__H_
