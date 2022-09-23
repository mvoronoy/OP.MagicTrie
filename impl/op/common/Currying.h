#ifndef _OP_COMMON_CURRYING__H_
#define _OP_COMMON_CURRYING__H_

#include <tuple>
#include <op/common/ftraits.h>

namespace OP::currying
{
    
    /** Marker that allows add extra behaviour for Currying argument evaluation. This is a base
    * class that in implementation must provide some no-arg `operator ()()`;
    */
    struct CurryingArgSpec{};

    template <class TCallable>
    struct F : CurryingArgSpec
    {
        using result_t = decltype(std::declval<TCallable>()());

        constexpr F(TCallable callable):
            _callable(std::move(callable))
            {}

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
        static constexpr bool can_handle()
        {
            return std::is_same_v<std::decay_t<result_t>, std::decay_t<U> >;
        }

    private:
        TCallable _callable;
    };
    
    template <class T>
    struct Var : CurryingArgSpec
    {
        using result_t = T&;

        constexpr Var(T& val) :
            _val(&val)
        {}

        constexpr Var() :
            _val(nullptr)
        {}

        template <typename U>
        static constexpr bool can_handle()
        {
            return std::is_same_v<T, U>;
        }

        decltype(auto) operator()()
        {
            return *_val;
        }

        void set(T& val)
        {
            _val = val;
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

    template <class T>
    struct Unpackable : Var<T>
    {
        template <typename >
        static constexpr bool can_handle();
        template <typename U>
        auto& extract();
    };

    template <>
    struct Unpackable<std::any> : Var<std::any>
    {
        using base_t = Var<std::any>;
        using base_t::base_t;

        template <typename >
        static constexpr bool can_handle() 
        {
            return true;
        }
        
        template <typename U>
        auto& extract()
        {
            std::any& holder = base_t::extract<std::any>();
            if constexpr(std::is_same_v<std::decay_t<U>, std::any>)
                return holder;
            else
            {
                return *std::any_cast<std::decay_t<U>>(&holder);
            }
        }
    };

    template <class T>
    auto of_unpackable(T&& t)
    {
        return Unpackable<std::decay_t<T>>(std::forward<T>(t));
    }

    template <class T>
    auto of_callable(T t)
    {
        return F(std::move(t));
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

    template <class ... Tx>
    struct CurryingTuple
    {
        using arguments_t = std::tuple<Tx...>;
        arguments_t _arguments;

        template <class ...Ax>
        constexpr CurryingTuple(Ax&& ...ax) noexcept
            : _arguments(std::forward<Ax>(ax)...)
        {}

        constexpr CurryingTuple(std::tuple<Tx ...>&& ax) noexcept
            : _arguments(std::forward<std::tuple<Tx ...>>(ax))
        {}


        template <class T>
        auto& assign(T&& t)
        {
            std::get<T>(_arguments) = std::forward<T>(t);
            return *this;
        }

        template <class T>
        decltype(auto) get()
        {
            constexpr size_t I = index_of_type<T>();
            auto& element = std::get<I>(_arguments);
            using element_t = std::decay_t<decltype(element)>;
            if constexpr (std::is_base_of_v<CurryingArgSpec, element_t>)
            {
                return element.template extract<T>();
            }
            else
                return element;
        }

        template <class TCallable>
        constexpr decltype(auto) def(TCallable f)& noexcept
        {
            return [f = std::move(f), args = std::ref(*this)]() -> decltype(auto){
                return args.invoke(f);
            };
        }

        template <class TCallable>
        constexpr decltype(auto) def(TCallable f)&& noexcept
        {
            return of_callable([f = std::move(f), 
                args = CurryingTuple<Tx...>(std::move(this->_arguments))]() mutable -> decltype(auto){
                return args.invoke(f);
            });
        }

        /** Collate existing arguments and bind them to the TCallable arguments
        * using type matching.
        * @return function of zero arguments that is bind to the current tuple
        */ 
        template <class TCallable, typename ...Ax>
        constexpr decltype(auto) tdef(TCallable f, Ax ... )&& noexcept
        {
            using ftraits_t = OP::utils::function_traits<TCallable>;
            return 
                [f = std::move(f), 
                //move this tuple to lambda owning since don't need this anymore
                args = CurryingTuple(std::move(this->_arguments))]
            //declare free (binding) arguments
            (typename ftraits_t::template arg_i<std::is_placeholder<Ax>::value - 1>&&...ax) mutable -> decltype(auto)
            {

                return args
                    .typed_invoke(f, std::forward<decltype(ax)>(ax)...);
            };
        }

        template <typename F>
        constexpr auto invoke(F& func) 
        {
            return do_invoke<true>(func, std::make_index_sequence<sizeof...(Tx)>());
        }

        template <typename TCallable, typename ... Ax>
        auto typed_invoke(TCallable& func, Ax&&...ax)
        {
            using ftraits_t = OP::utils::function_traits<TCallable>;
            return typed_invoke_impl<true>(
                func, std::make_index_sequence<ftraits_t::arity_c - sizeof...(Ax)>{},
                std::forward<Ax>(ax)...);
        }

        template <class TCallable>
        constexpr decltype(auto) typed_bind(TCallable f) const& noexcept
        {
            return [
                f = std::move(f),
                    //copy this tuple to lambda owning 
                    args = CurryingTuple(this->_arguments)
            ] () mutable -> decltype(auto)
            {
                return args.typed_invoke(f);
            };
        }

        /**
        * Same as #typed_def collates existing arguments and bind them to the TCallable arguments
        * using type matching, but keeps N front arguments unbinded to allow specify during call time.
        * @return function of N arguments where other expected bind to the current tuple
        */
        template <size_t N, class TCallable>
        constexpr decltype(auto) typed_bind_front(TCallable f) && noexcept
        {
            return typed_bind_impl<true>(
                f, std::move(_arguments), std::make_index_sequence<N>{});
        }

        template <size_t N, class TCallable>
        constexpr decltype(auto) typed_bind_front(TCallable f) const& noexcept
        {
            return typed_bind_impl<true>(
                f, _arguments, std::make_index_sequence<N>{});
        }

    private:

        template <bool front_invoke, typename F, size_t... I, typename ... Ax, typename ftraits_t = OP::utils::function_traits<F> >
        auto typed_invoke_impl(F& func, std::index_sequence<I...>, Ax&& ...ax)
        {
            constexpr size_t Pn = sizeof...(Ax);
            return do_invoke<front_invoke>(
                func,
                std::integer_sequence<size_t,
                // reindex tuple elements according to types
                index_of_type<typename ftraits_t::template arg_i<I + Pn>>()...>{},
                std::forward<Ax>(ax)...
                );
        }

        template <bool front_invoke, typename TCallable, size_t... Ns>
        constexpr static decltype(auto) typed_bind_impl(TCallable& func, arguments_t args, std::index_sequence<Ns...>) noexcept
        {
            using ftraits_t = OP::utils::function_traits<TCallable>;

            return
                [func = std::move(func),
                //move this tuple to lambda owning since don't need this anymore
                args = CurryingTuple(std::move(args))]
            //declare free (binding) arguments
            (typename ftraits_t::template arg_i<Ns>&&...ax) mutable -> decltype(auto)
            {
                constexpr size_t Pn = sizeof...(ax);
                return args.typed_invoke_impl<front_invoke>(
                    func, std::make_index_sequence<ftraits_t::arity_c - Pn>{},
                    std::forward<decltype(ax)>(ax)...);
            };
        }

        template <bool front_invoke, typename TCallable, size_t... I, typename ... Ax>
        auto do_invoke(TCallable& func, std::index_sequence<I...>, Ax&& ...ax)
        {
            using ftraits_t = OP::utils::function_traits<TCallable>;
            constexpr size_t Pn = sizeof ... (Ax);
            if constexpr (front_invoke) //free args go in front
                return func(std::forward<Ax>(ax)...,
                    inject_argument<typename ftraits_t::arg_i<I + Pn>>(
                        std::get<I>(_arguments))...
                );
            else // bind free args back
                return func(inject_argument<typename ftraits_t::arg_i<I>>(
                    std::get<I>(_arguments))...,
                    std::forward<Ax>(ax)...)
                ;
        }

        template <class T>
        static T& deref(T& t) noexcept
        {
            return t;
        }

        template <typename T>
        static const T& deref(std::reference_wrapper<T const> t) noexcept 
        {
            return (T&)t.get();
        }

        template <typename T>
        static T& deref(std::reference_wrapper<T> t) noexcept 
        {
            return (T&)t.get();
        }

        /** Method allows preprocess function arguments before invocation with help of CurryingArgSpec
         *
         * @return reference 
         */
        template <typename expected_t, typename arg_t>
        static constexpr decltype(auto) inject_argument(arg_t& a) noexcept
        {
            using t_t = std::decay_t<arg_t>;
            
            if constexpr (std::is_base_of_v<CurryingArgSpec, t_t>)
            {
                if constexpr (std::is_same_v<t_t, std::decay_t<expected_t> >)
                    return deref(a);
                else
                    return a.extract<expected_t>();
            }
            else
                return deref(a);
        }

        /** Taken tuple #arguments_t return index of type `T` entry */
        template<typename T, size_t I = 0>
        static constexpr size_t index_of_type() noexcept
        {
            constexpr size_t type_idx = match_type_explicit<T, I>();
            if constexpr (type_idx < sizeof...(Tx))
                return type_idx;
            else
            {
                return index_of_implicit_type<T, I>();
            }
        }

        /** Find exact (explicit) matching of type `T` in `arguments_t` starting from index `I`*/
        template<typename T, size_t I>
        static constexpr size_t match_type_explicit() noexcept
        {
            if constexpr(I >= sizeof...(Tx))
                return I;
            else 
            {
                using current_t = std::decay_t<std::tuple_element_t<I, arguments_t>>;
                using t_t = std::decay_t<T>;
                if constexpr (std::is_same_v<t_t, current_t>)
                {
                    return I;
                }
                else
                {
                    return match_type_explicit<T, I + 1>();
                }
            }
        }

        /** Find fuzzy (implicit) matching of type `T` in `arguments_t` starting from index `I`*/
        template<typename T, size_t I>
        static constexpr size_t index_of_implicit_type() noexcept
        {
            static_assert(I < sizeof...(Tx),
                "Cannot find matched type T in arguments tuple");

            using current_t = std::decay_t<std::tuple_element_t<I, arguments_t>>;
            using t_t = std::decay_t<T>;
            if constexpr (std::is_base_of_v<CurryingArgSpec, current_t>)
            {
                if constexpr (current_t::can_handle<t_t>())
                    return I;
                else
                    return index_of_implicit_type<T, I + 1>();
            }
            else
            {
                return index_of_implicit_type<T, I + 1>();
            }
        }

    };

    template <class ...Tx>
    auto arguments(Tx&& ...tx)
    {
        return CurryingTuple(std::make_tuple(std::forward<Tx>(tx)...));
    }


}//ns:OP::currying
#endif //_OP_COMMON_CURRYING__H_
