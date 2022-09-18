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
        constexpr F(TCallable callable):
            _callable(std::move(callable))
            {}

        decltype(auto) operator()()
        {
            return _callable();
        }

    private:
        TCallable _callable;
    };
    
    template <class T>
    auto of_callable(T t)
    {
        return F(std::move(t));
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

        template <class TCallable>
        constexpr decltype(auto) tdef(TCallable f)&& noexcept
        {
            return of_callable([f = std::move(f), 
                args = CurryingTuple<Tx...>(std::move(this->_arguments))]() mutable -> decltype(auto){
                return args.typed_invoke(f);
            });
        }

        template <typename F>
        constexpr auto invoke(F& func) 
        {
            return do_invoke(func, std::make_index_sequence<sizeof...(Tx)>());
        }

        template <typename F>
        auto typed_invoke(F& func) 
        {
            using ftraits_t = OP::utils::function_traits<F>;
            return typed_invoke_impl(
                func, std::make_index_sequence<ftraits_t::arity_c>{});
        }

    private:

        template <typename F, size_t... I, typename ftraits_t = OP::utils::function_traits<F> >
        auto typed_invoke_impl(F& func, std::index_sequence<I...>)
        {
            return do_invoke(func, 
                std::integer_sequence<size_t, 
                // reindex tuple elements according to types
                index_of_type<typename ftraits_t::template arg_i<I>>()...>{}
                );
        }

        template <typename F, size_t... I>
        auto do_invoke(F& func, std::index_sequence<I...>)
        {
            return func(
                inject_argument(std::get<I>(_arguments))...
                );
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
        template <typename arg_t>
        static constexpr decltype(auto) inject_argument(arg_t& a) noexcept
        {
            using t_t = std::decay_t<arg_t>;
            if constexpr(std::is_base_of_v<CurryingArgSpec, t_t>)
                return a();
            else
                return deref(a);
        }
        template <size_t I>
        using raw_t = std::decay_t<
            decltype(inject_argument(std::get<I>(std::declval<arguments_t>())))
        >;

        /** Taken tuple #arguments_t return index of type `T` entry */
        template<typename T, size_t I = 0>
        static constexpr size_t index_of_type() noexcept
        {
            static_assert(I < sizeof...(Tx), "Cannot find matched type T in arguments tuple");
            if constexpr (std::is_same_v<std::decay_t<T>, raw_t<I>>)
            {
                return I;
            }
            else
            {
                return index_of_type<T, I + 1>();
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
