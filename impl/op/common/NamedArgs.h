#pragma once
#ifndef _OP_TRIE_NAMEDARGS__H_
#define _OP_TRIE_NAMEDARGS__H_

#include <type_traits>
#include <tuple>

namespace OP
{
    namespace utils
    {
        /**
        *   Argument identifier that allows reference argument in the context by this ID.
        * Uniqueness is create by int, so it is your respnse to avoid clash inside single context. For details see NamedArgContext
        */
        template <int X>
        struct ArgId
        {
            using id_t = ArgId<X>;
        };

        /** Holder for the value. User don't need to deal with this class explictly */
        template <int X, class T>
        struct Arg : public ArgId<X>
        {
            constexpr Arg(T avalue) noexcept
                : value(avalue) {}

            operator const T& () const
            {
                return value;    
            }
            const T value;
        };
        template <int X, class T>
        inline constexpr OP::utils::Arg<X, T> operator << (OP::utils::ArgId<X>, T&& t)
        {
            return OP::utils::Arg<X, T>{std::forward<T>(t)};
        }
        /** Store named values. In compare with hash-map this class:
        * - allows store polymorphic values;
        * - handles access by key (see ArgId<X> ) at compile time;
        *
        * Usage example: 
        * \code
        * namespace keys{
        * //declare const to access
        *    const ArgId<1> important_param_1;
        *    const ArgId<2> important_param_2;
        * }
        * auto named_args = make_named_args( imprtant_param_1 << "my const str", important_param_2 << 84848.11);
        * // later on code:
        * auto double_extract = named_args.get( important_param_2 );
        * \endcode
        */
        template <class ... Tx>
        struct NamedArgContext
        {
            using holder = std::tuple<Tx ...>;
            const holder _values;

            template <class A>
            const auto& get(const A& _) const
            {
                return get_first_bid <A>(_values).value;
            }
            template <class A>
            const auto& get() const
            {
                return get_first_bid <A>(_values).value;
            }
        private:
            template <std::size_t, bool ...>
            struct tuple2predicate_t
             { };

            template <std::size_t I, bool ... Bs>
            struct tuple2predicate_t<I, false, Bs...> : public tuple2predicate_t<I+1u, Bs...>
             { };

            template <std::size_t I, bool ... Bs>
            struct tuple2predicate_t<I, true, Bs...> : public std::integral_constant<std::size_t, I>
             { };

            template <typename Aa, typename ... Us,
               std::size_t I = tuple2predicate_t < 0, std::is_same<typename Aa::id_t, typename Us::id_t>::value...>::value>
            const auto& get_first_bid (std::tuple<Us...> const & t) const
             { return std::get<I>(t); }


        };

        template <class ... Tx>
        auto make_named_args(Tx && ... args)
        {
            return NamedArgContext<Tx ...>{ std::make_tuple(std::forward<Tx>(args)...) };
        }


    } //utils
} //OP



#endif //_OP_TRIE_NAMEDARGS__H_
