#pragma once
#ifndef _OP_FLUR_FLUR__H_
#define _OP_FLUR_FLUR__H_

#include <functional>
#include <memory>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRange.h>
#include <op/flur/OfConatiner.h>
#include <op/flur/OfOptional.h>
#include <op/flur/OfValue.h>
#include <op/flur/OfIota.h>
#include <op/flur/OfIterators.h>
#include <op/flur/OfGenerator.h>
#include <op/flur/SimpleFactory.h>

#include <op/flur/Cartesian.h>
#include <op/flur/Filter.h>
#include <op/flur/FlatMapping.h>
#include <op/flur/Mapping.h>
#include <op/flur/OrDefault.h>
#include <op/flur/OnException.h>
#include <op/flur/Repeater.h>
#include <op/flur/Minibatch.h>
#include <op/flur/StringInput.h>
#include <op/flur/stl_adapters.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** namespace for function that are source of LazyRange */
    namespace src
    {
        /**
        * Create LazyRange from any container that supports pair std::begin / std::end.
        * For ordered containers like std::set or std::map result will generate OrderedSequence wrapper
        */
        template <class T>
        constexpr auto of_container(T&& t) noexcept
        {
            return make_lazy_range(OfContainerFactory<T>(std::forward<T>(t)));
        }
        
        template <class T, std::enable_if_t<std::is_invocable<decltype(of_container<T>), T&&>::value, int> = 0>
        constexpr auto of(T&& t)  noexcept
        {
            return of_container(std::move(t));
        }
        /**
        * Create new LazyRange from std::optional. Result range is ordered and allows 0 to 1 iteration by 
        * the contained value
        */
        template <class V>
        constexpr auto of_optional(std::optional<V>&& v) noexcept
        {
            return make_lazy_range(
                SimpleFactory<std::optional<V>, OfOptional<V>>(std::forward<std::optional<V>>(v)) );
        }
        template <class V>
        constexpr auto of_optional(V v) noexcept
        {
            return make_lazy_range(
                SimpleFactory<std::optional<V>, OfOptional<V>>(std::optional <V>(std::move(v))));
        }
        template <class V>
        constexpr auto of_optional() noexcept
        {
            return make_lazy_range(
                SimpleFactory<std::optional<V>, OfOptional<V>>(std::optional <V>{}));
        }
        template <class T, std::enable_if_t<std::is_invocable<decltype(of_optional<T>), T>::value, int> = 0>
        constexpr auto of(T t) noexcept
        {
            return of_optional(std::move(t));
        }

        /**
        * Create LazyRange from single value. Result is ordered range iterable over exact one value
        */
        template <class V>
        constexpr auto of_value(V&& v) noexcept
        {
            return make_lazy_range( SimpleFactory<V, OfValue<V>>(std::forward<V>(v)) );
        }
        /**
        *   Create LazyRange from single value that evaluated each time when start/next loop starts. 
        *   Result is ordered range iterable over exact one value
        *   \tparam F - functor without arguments to return value
        */
        template <class F>
        constexpr auto of_lazy_value(F f) noexcept
        {
            using r_t = decltype(f());
            return make_lazy_range( SimpleFactory<F, OfLazyValue<r_t, F>>(std::move(f)) );
        }
        /**
        * Create LazyRange in a way similar to std::iota - specify range of values that support ++ operator.
        * For example:\code
        * 
        * \endcode
        */
        template <class V>
        constexpr auto of_iota(V&& begin, V&& end) noexcept
        {
            using iota_value_t = std::decay_t<V>;
            return make_lazy_range( SimpleFactory<std::pair<V, V>, OfIota<iota_value_t>>(std::move(begin), std::move(end)) );
        }

        /**
        * Create LazyRange in a way similar to std::iota - specify range of values that support ++ operator.
        * For example:\code
        * 
        * \endcode
        */
        template <class It>
        constexpr auto of_iterators(It begin, It end) noexcept
        {
            return make_lazy_range( SimpleFactory<std::pair<It, It>, OfIterators<It>>(std::move(begin), std::move(end)) );
        }

        /**
        *  create LazyRange from function. For details see OP::flur::Generator
        */
        template <class F>
        constexpr auto generator(F&& f) noexcept
        {
            return make_lazy_range( GeneratorFactory<F, false>(std::forward<F>(f)) );
        }

        template <typename T, 
            typename = std::enable_if_t<
                OP::utils::is_generic<details::dereference_t<T>, std::basic_string>::value
            ||  OP::utils::is_generic<details::dereference_t<T>, std::basic_string_view>::value> >
        using String = T;
        /**
        * Create LazyRange to iterate over elements of string splitted by separator. Iteration 
        * has minimal memory footprint since to access uses std::string_view
        * For example:\code
        * 
        * \endcode
        */
        template <class Str1, class Str2>
        constexpr auto of_string_split(String<Str1>&& str, String<Str2>&& separators) noexcept
        {
            using raw_t = std::decay_t<Str1>;
            using str_t = details::dereference_t<raw_t>;
            using str_view_t = std::basic_string_view< typename str_t::value_type >;

            using splitter_t = StringSplit<raw_t, str_view_t>;
            //Simple factory will use copy operation of the same instance
            return make_lazy_range( 
                SimpleFactory<splitter_t, splitter_t>(
                    splitter_t(std::move(str), std::forward<String<Str2>>(separators))));
        }
        template <class Str>
        constexpr auto of_string_split(String<Str>&& str) noexcept
        {
            using raw_t = std::decay_t<Str>;
            using str_t = details::dereference_t<raw_t>;
            const static std::basic_string< typename str_t::value_type > separators(" ");
            using str_view_t = std::basic_string_view< typename str_t::value_type >;

            using splitter_t = StringSplit<raw_t, str_view_t>;
            //Simple factory will use copy operation of the same instance
            return of_string_split(
                    std::move(str), separators);
        }


    } //ns:src
    /** namespace for functions that creates elements of pipeline following after elements from `srs` namespace */
    namespace then
    {
        /** Provides source or single value that can be consumed if origin is empty 
        * Usage \code
        * src::of(std::vector<int>{}) // empty vector as a source
        *   >> then::or_default( 57 ) // single value used as alternative
        * \endcode
        *   Default can be composed with more complicated containers:
        * \code
        * src::of(std::vector<int>{}) // empty vector as a source
        *   >> then::or_default( 
        *       src::of_container({1, 2, 3)}) >> then::filter([](auto i){ return i % 2;})  // odd values container
        * )
        * \endcode
        */
        template <class T>
        constexpr auto or_default(T t) noexcept
        {
            return OrDefaultFactory<T>(std::move(t));
        }

        /**
        *   Add handle of exception to previous definition of pipeline. For more details and examples see OnException.
        * \tparam Ex - type of exception to intercept
        * \tparam Alt - factory that produces alternatice source in case of exception. 
        */
        template <class Ex, class Alt>
        constexpr auto on_exception(Alt&& t) noexcept
        {
            return OnExceptionFactory<Alt, Ex>(std::forward<Alt>(t));
        }

        /** Produce new source that is result of applying function to each element of origin source */
        template <class F>
        constexpr auto mapping(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MappingFactory<f_t>(std::move(f));
        }

        /** Same as mapping, but assume function F produces some set instead of single value 
        *
        */
        template <class F>
        constexpr auto flat_mapping(F&& f) noexcept
        {
            return FlatMappingFactory<F>(std::forward<F>(f));
        }
        /** Same as '&' operator for LazyRange, but allows use `>>` operator 
        *
        */
        template <class Right>
        constexpr auto join(Right&& right) noexcept
        {
            return PartialJoinFactory<Right>(std::forward<Right>(right));
        }
        /** Same as '&' operator for LazyRange, but allows specify comparator of joining keys
        *  \tparam Comp - functor that matches functor `signed f( const Right::element_t& , const Right::element_t)`
        */
        template <class Right, class Comp>
        constexpr auto join(Right&& right, Comp&& comp) noexcept
        {
            return PartialJoinFactory<Right, Comp>(
                std::forward<Right>(right), std::forward<Comp>(comp));
        }

        /** Convert source by filtering out some ellements according to predicate 
        * If applied to ordered source then result is ordered as well
        * \tparam Fnc - must be a predicate producing false for items to filter out
        */
        template <class Fnc>
        constexpr auto filter(Fnc f) noexcept
        {
            return FilterFactory<Fnc>(std::move(f));
        }
        /**
        *  Produce cartesian multiplication of 2 sources. 
        * \tparam Alien - some other pipeline
        * \tparam F - function that accept 2 arguments (element from source and element from Alien) then produces target 
        *           result element. As an example of result you may use std::pair
        */
        template <class Alien, class F>
        constexpr auto cartesian(Alien&& alien, F f) noexcept
        {
            //const second_src_t<Alien>&
            //auto cros = std::function<R(const Src&, const R&)>(fnc);
            return CartesianFactory<Alien, F>(std::move(alien), std::move(f));
        }

        /** Produce repeater (for details see OP::flur::Repeater */
        constexpr auto repeater() noexcept
        {
            return RepeaterFactory();
        }

        template <size_t N, class TThreads>
        constexpr auto minibatch(TThreads& thread_pool) noexcept
        {
            return MinibatchFactory<N, TThreads>{thread_pool};
        }

    } //ns:then

} //ns:flur
} //ns:OP

#endif //_OP_FLUR_FLUR__H_
