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
#include <op/flur/Zip.h>
#include <op/flur/Conditional.h>
#include <op/flur/Filter.h>
#include <op/flur/TakeAwhile.h>
#include <op/flur/FlatMapping.h>
#include <op/flur/OrderingFlatMapping.h>
#include <op/flur/Mapping.h>
#include <op/flur/maf.h>
#include <op/flur/OrDefault.h>
#include <op/flur/Diff.h>
#include <op/flur/OnException.h>
#include <op/flur/Repeater.h>
#include <op/flur/Minibatch.h>
#include <op/flur/StringInput.h>
#include <op/flur/Distinct.h>
#include <op/flur/OrderedJoin.h>
#include <op/flur/UnorderedJoin.h>
#include <op/flur/UnionAll.h>
#include <op/flur/MergeSortUnion.h>
#include <op/flur/HierarchyTraversal.h>
#include <op/flur/PolymorphsBack.h>
#include <op/flur/Applicator.h>
#include <op/flur/stl_adapters.h>
#include <op/flur/ParallelSort.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    /** namespace for functions that are source of LazyRange */
    namespace src
    {
        /**
        * Create LazyRange from any container that supports pair std::begin / std::end.
        * For ordered containers like std::set or std::map result will generate OrderedSequence wrapper
        */
        template <class T>
        constexpr auto of_container(T&& t) noexcept
        {
            return make_lazy_range(OfContainerFactory<T>(0, std::forward<T>(t)));
        }
        
        template <class T, std::enable_if_t<std::is_invocable<decltype(of_container<T>), T&&>::value, int> = 0>
        constexpr auto of(T&& t)  noexcept
        {
            return of_container(std::forward<T>(t));
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

        /** Resolve always empty container. */
        template <class T>
        constexpr auto null() noexcept
        {
            return make_lazy_range(NullSequenceFactory<T>());
        }

        /**
        * Create LazyRange from single value. Result is ordered range provided iteration over exact one value.
        * Providing non-default parameter `limit` allows you repeat the same value `limit` times.
        * During iteration sequence will provide value by copy. In case you need avoid redundant copy 
        * use `of_cref_value` instead. 
        *
        * \tparam V type that must support copy/move semantic. 
        * \param v value to retrieve during this sequence iteration. Note that you have to provide correct policy
        *   to avoid undefined behavior. 
        *       reference policy                | type of sequence `current()` | comments
        *       --------------------------------|------------------------------|---------
        *       By value:                       | Result sequence is "by value"| Use carefully for a
        *       \code                           |as well.                      | heavy objects. To force const 
        *       src::of_value(std::string("a")) |                              |references result consider
        *       \endcode                        |                              |`of_cref_value` instead.
        *       --------------------------------|------------------------------|---------
        *       By const reference:             | Result sequence is `const    | Use carefully to avoid dangling references.
        *       \code                           | std::string&` that uses      | Bad example that creates a lazy sequence by 
        *       std::string str("abc");         | local variable to point      | referencing an uncontrolled variable:
        *       src::of_value(str) ...          | target value.                |  
        *       \endcode                        |                              | \code
        *                                       |                              | auto range_from_string(const std::string& arg)
        *                                       |                              | {
        *                                       |                              |    using namespace OP::flur;
        *                                       |                              |    return src::of_value(arg); //keeps arg as cref
        *                                       |                              | }
        *                                       |                              | \endcode
        *                                       |                              | To fix the issue, either copy `arg` by value or 
        *                                       |                              | strongly control the lifespan of `arg`.
        *       --------------------------------|------------------------------|---------
        *
        * \param limit number of times to repeat result v during sequence iteration. The default is 1. Value 0 is
        *   allowed, but from optiomization perspective better to use `OP::flur::src::null()` instead.
        * \sa of_cref_value
        */
        template <class V>
        constexpr auto of_value(V&& v, size_t limit = 1) noexcept
        {
            return make_lazy_range( SimpleFactory<std::decay_t<V>, OfValue<std::decay_t<V>>>(std::forward<V>(v)) );
        }
        

        /**
        * The same as `of_value` but allows use const-reference to the holding value during iteration.
        * \sa of_value
        */
        template <class V>
        constexpr auto of_cref_value(V&& v, size_t limit = 1) noexcept
        {
            return make_lazy_range( SimpleFactory<const V&, OfValue<V, const V&>>(std::forward<V>(v)) );
        }

        /**
        *   Create LazyRange from functor that produce single value. Functor is evaluated each time when 
        * Sequence::current() invoked. 
        * Result is ordered range iterable over exact one value
        * \param limit number of times to repeat result v during sequence iteration. The default is 1. Value 0 is
        *   allowed, but from optimization perspective better to use `OP::flur::src::null()` instead.
        * \tparam F - functor to return value. It may be declared as: 
        *   - no argument function;
        *   - any combination of:
        *   -- `const OP::flur::SequenceState&`, 
        *   -- `size_t` (copy of limit argument).
        */
        template <class F>
        constexpr auto of_lazy_value(F f, size_t limit = 1) noexcept
        {
            using functor_traits_t = OP::utils::function_traits<std::decay_t<F>>;
            using result_t = typename functor_traits_t::result_t;
            using sequence_t = OfLazyValue<result_t, F>;
            using simple_factory_param_t = typename sequence_t::simple_factory_param_t;
            return make_lazy_range( SimpleFactory<simple_factory_param_t, sequence_t>(
                simple_factory_param_t{ std::move(f), limit }) );
        }

        template <class TOnTrue, class TOnFalse>
        constexpr auto conditional(bool condition, TOnTrue on_true, TOnFalse on_false) noexcept
        {
            auto eval_arg = [](auto& arg){
                if constexpr( std::is_invocable_v<decltype(arg)> )
                    return arg();
                else
                    return arg;
            };
            using on_true_t = decltype( eval_arg(on_true) );
            using on_false_t = decltype( eval_arg(on_false) );
            using proxy_factory_t = ProxyFactory<on_true_t, on_false_t>;

            if( condition )
                return make_lazy_range(proxy_factory_t{eval_arg(on_true)});
            else
                return make_lazy_range(proxy_factory_t{eval_arg(on_false)});
        }

        /**
        * Create a LazyRange similar to std::iota, which is a generates a sequence of values by repeatedly incrementing 
        * a starting value. Result sequence produces `T` as value (if you need `const T&` use `of_cref_iota`).
        *
        *  \tparam T type must support prefixed ++ operator. Optionally if for this type there is a definition std::less
        *       then result sequence is ordered (eg. is_sequence_ordered() == true).

        * For example:\code
        *   for(auto i: src::of_iota(1, 3))
        *       std::cout << i;
        * \endcode
        * Prints `12` (sequential 1 and 2).
        *
        */
        template <class T>
        constexpr auto of_iota(T begin, T end) noexcept
        {
            using iota_value_t = std::decay_t<T>;
            using iota_t = OfIota<iota_value_t> ;
            using factory_t = SimpleFactory<typename iota_t::bounds_t, iota_t>;
            return make_lazy_range(factory_t(std::move(begin), std::move(end), T{ 1 }));
        }
        
        template <class T>
        constexpr auto of_iota(T begin, T end, T step) noexcept
        {
            using iota_value_t = std::decay_t<T>;
            using iota_t = OfIota<iota_value_t>;
            using factory_t = SimpleFactory<typename iota_t::bounds_t, iota_t>;
            return make_lazy_range(factory_t(std::move(begin), std::move(end), std::move(step)));
        }

        template <class T>
        constexpr auto of_cref_iota(T begin, T end, T step) noexcept
        {
            using iota_value_t = std::decay_t<T>;
            using iota_t = OfIota<iota_value_t, const iota_value_t&>;
            using factory_t = SimpleFactory<typename iota_t::bounds_t, iota_t>;
            return make_lazy_range(factory_t(std::move(begin), std::move(end), std::move(step)));
        }

        /** \brief Same as of_iota, but result sequence produces `const T&` */
        template <class T>
        constexpr auto of_cref_iota(T begin, T end) noexcept
        {
            using iota_value_t = std::decay_t<T>;
            using iota_t = OfIota<iota_value_t, const iota_value_t&>;
            using factory_t = SimpleFactory<typename iota_t::bounds_t, iota_t>;
            return make_lazy_range(factory_t(std::move(begin), std::move(end), T{ 1 }));
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
        *  @brief create LazyRange from generator functor. 
        *
        *  @tparam F the generator functor. The functor must return a value that is contextually convertible to 
        *           bool (i.e., supports `operator bool` or an equivalent operator). Additionally, it must 
        *           support dereferencing via `*`. Therefore, the generator class will work out-of-the-box for 
        *           raw pointers, std::optional.
        * 
        *           F may have the following input arguments:
        *           - No arguments (`f()`).
        *           - `const SequenceState&` to expose the current state of the sequence. For example:
        *               - Use `state.step() == 0` to check the current step or the beginning of the sequence.
        *
        */
        template <class F>
        constexpr auto generator(F&& f) noexcept
        {
            return make_lazy_range( GeneratorFactory<F, false>(std::forward<F>(f)) );
        }

        /*
        * \brief indicate that generator provides ordered sequence.
        * 
        */
        template <class F>
        constexpr auto keep_order_generator(F&& f) noexcept
        {
            return make_lazy_range( 
                GeneratorFactory<F, true>(std::forward<F>(f)) );
        }

        template <typename T, 
            typename = std::enable_if_t<
                OP::utils::is_generic<details::dereference_t<T>, std::basic_string>::value
            ||  OP::utils::is_generic<details::dereference_t<T>, std::basic_string_view>::value> >
        using String = T;
        /**
         *   @brief Create LazyRange to iterate over elements of a string split by a separator.
         * 
         * Iteration has minimal memory overhead since it uses `std::string_view` for access.
         * 
         * For example:
         * @code
         * std::for_each(src::of_string_split("a:b:c"s, ":"s), 
         *   [](const std::string_view& word) { std::cout << word << "\n"; });
         * @endcode
         * Produces:
         * @code
         * a
         * b
         * c
         * @endcode
         * 
         * @param str A string-like object to split.
         * @param separators A string-like object of possible separators. When multiple characters are specified, any of them
         *                   are treated as separators. For example, `of_string_split("a:b;c,d", ",:")` generates a sequence of
         *                   {"a", "b;c", "d"}. Note that an empty string is allowed; in this case, the result is the entire input `str`.
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
        OP_CONSTEXPR_CPP20 const static inline std::basic_string< typename Str::value_type > default_separators_c{ " " };

        /** \brief same as `of_string_split(str, separators) but use (" ") space as single separator */
        template <class Str>
        constexpr auto of_string_split(String<Str>&& str) noexcept
        {
            using raw_t = std::decay_t<Str>;
            using str_t = details::dereference_t<raw_t>;
            using str_view_t = std::basic_string_view< typename str_t::value_type >;

            using splitter_t = StringSplit<raw_t, str_view_t>;
            //Simple factory will use copy operation of the same instance
            return of_string_split(
                    std::move(str), default_separators_c<str_t>);
        }

        template <class Poly>
        auto back_to_lazy(Poly &&poly)
        {
            using poly_t = std::decay_t<Poly>;
            return make_lazy_range( OfReversePolymorphFactory<poly_t>(std::forward<Poly>(poly)) );
        }

        /**
         * \brief Combine multiple sequences using the zip algorithm and produce elements as a result of applying `F`.
         *
         *  Zip works on two or more sequences, combining them sequentially as multiple arguments to the `F` applicator.
         *  For example:
         *  \code
         *   src::zip(
         *       [](int i, char c) -> std::string { // Convert zipped pair to string
         *               std::ostringstream result;
         *               result << '[' << i << ", " << c << ']';
         *               return result.str();
         *       },
         *       src::of_container(std::array{1, 2, 3}),
         *       src::of_container(std::array{'a', 'b', 'c', 'd'}) //'d' will be omitted
         *   )
         *   >>= apply::for_each([](const std::string& r) { std::cout << r << "\n"; });
         *  \endcode
         *  This prints: \code
         * [1, a]
         * [2, b]
         * [3, c] \endcode
         *
         *  Note that by default, zip operates until the smallest sequence is exhausted, so you cannot control the trailing
         *  elements of longer sequences.
         *  To process all elements in the longest sequence, wrap all arguments of your applicator with `std::optional`. This
         *  gives the flur-library a hint that you would like to process all elements. For example, a 3-sequence zip with optional arguments:
         *  \code
         *   using namespace std::string_literals;
         *   auto print_optional = [](std::ostream& os, const auto& v) -> std::ostream&
         *      { return v ? (os << *v) : (os << '?'); };
         *   src::of_container(std::array{1, 2, 3})
         *       >> then::zip(
         *           // Convert zipped triplet to string with '?' when optional is empty
         *           // Note: All arguments must be `std::optional`
         *           [](zip_opt<int> i, zip_opt<char> c, zip_opt<float> f) -> std::string {
         *                   std::ostringstream result;
         *                   result << '[';
         *                   print_optional(result, i) << ", ";
         *                   print_optional(result, c) << ", ";
         *                   print_optional(result, f) << ']';
         *                   return result.str();
         *           },
         *           src::of_container("abcd"s), // String as source of 4 characters
         *           src::of_container(std::array{1.1f, 2.2f}) // Source of 2 floats
         *       )
         *       >>= apply::for_each([](const std::string& r) { std::cout << r << "\n"; });
         *  \endcode
         * This prints: \code
         * [1, a, 1.1]
         * [2, b, 2.2]
         * [3, c, ?]
         * [?, d, ?] \endcode
         */
        template <class F, class Alien, class ... Ax>
        constexpr auto zip(F f, Alien&& alien, Ax&& ...ax) noexcept
        {
            return make_lazy_range(
                ZipFactory<F, Alien, Ax...>(std::move(f), std::forward<Alien>(alien), std::forward<Ax>(ax)...)
                );
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
        * \tparam Alt - factory that produces alternative source in case of exception. 
        */
        template <class Ex, class Alt>
        constexpr auto on_exception(Alt&& t) noexcept
        {
            return OnExceptionFactory<Alt, Ex>(std::forward<Alt>(t));
        }

        /** Produce new source that is result of applying function to each element of origin source.
        * \tparam options_c - extra options to customize sequence behavior. Implementation recognizes 
        * none or Intrinsic::keep_order - to allow keep source sequence order indicator.
        */
        template <auto ... options_c, class F>
        constexpr auto mapping(F f) noexcept
        {
            //using f_t = std::decay_t<F>;
            //return MappingFactory<f_t, OP::utils::any_of<options_c...>(Intrinsic::keep_order)>(0, std::move(f));
            return MappingFactory<F, OP::utils::any_of<options_c...>(Intrinsic::keep_order)>(0, std::forward<F>(f));
        }

        /** Equivalent to call of `mapping<Intrinsic::keep_order>(std::move(f))`
        * Creates mapping factory to produces a sequence that keeps ordering.
        * Result sequence is not mandatory ordered it just commitment of developer of `F` to keep order  
        * if source sequencies ordered as well.
        */
        template <class F>
        constexpr auto keep_order_mapping(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MappingFactory<f_t, true>(0, std::move(f));
        }

        /** Map and Filter factory that execute both operations in the single functor.
        * \tparam maf_options_c - extra options to customize sequence behavior. Implementation recognizes 
        * none or any of:
        *       - Intrinsic::keep_order - to allow keep source sequence order indicator;
        *       - MaFOptions::result_by_value - to declare `current()` return result by value instead of
        *            const reference.
        * \tparam F functor with the signature `bool(typename Src::element_t, <desired-mapped-type> &result)`
        *       Note that implementation assumes that <desired-mapped-type> is default constructible
        */
        template <auto ... maf_options_c, class F>
        constexpr auto maf_cv(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MapAndFilterFactory<f_t, maf_options_c...>(std::move(f));
        }

        /** Equivalent to call of `maf_cv<MaFOptions::result_by_value>(std::move(f))`. Creates `maf` factory that  
        *  produces sequence to copy result instead of const-reference.
        */
        template <class F>
        constexpr auto maf(F f) noexcept
        {
            return maf_cv<MaFOptions::result_by_value>(std::move(f));
        }

        /** 
        * Equivalent to call of `maf_cv<Intrinsic::keep_order, MaFOptions::result_by_value>(std::move(f))`
        * Creates `maf` factory that  
        *  produces sequence that keeps ordering and copies result instead of const-reference.
        * Result sequence is not mandatory ordered it just commitment of developer of `F` to keep order  
        * if source sequencies ordered as well.
        * \tparam F functor with the signature `bool(typename Src::element_t, <desired-mapped-type> &result)`
        *       Note that implementation assumes that <desired-mapped-type> is default constructible
        */
        template <class F>
        constexpr auto keep_order_maf(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MapAndFilterFactory<f_t, Intrinsic::keep_order, MaFOptions::result_by_value>(std::move(f));
        }

        /** Equivalent to call of `maf_cv<Intrinsic::keep_order>(std::move(f))`. Creates `maf` factory that  
        *  that keeps ordering.
        * \tparam F functor with the signature `bool(typename Src::element_t, <desired-mapped-type> &result)`
        *       Note that implementation assumes that <desired-mapped-type> is default constructible
        */
        template <class F>
        constexpr auto keep_order_maf_cv(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MapAndFilterFactory<f_t, Intrinsic::keep_order>(std::move(f));
        }

        /** Same as mapping, but assume function F produces some set instead of single value.
        *
        * \tparam options_c - extra options to customize result sequence behavior. FlatMap implementation 
        * recognizes only `Intrinsic::keep_order` - to allow keep source sequence order indicator. All another
        *   options will be ignored.
        * \tparam F functor that accept 1 arg result of other `Sequence::current()` and returns Sequence or 
        *   FactoryBase for 'flat' result.
        */
        template <auto ... options_c, class F>
        constexpr auto flat_mapping(F f) noexcept
        {
            return FlatMappingFactory<F, options_c...>{0, std::move(f)};
        }

        /**
        *  Implement flat map logic and produce OrderedSequence from unordered source on condition that
        *   functor `F` produces ordered sequence as well.
        *  Note! Implementation allocates extra memory to provide binary-heap for all sequences.
        *
        *  \throws std::runtime_error if `F` creates non-ordered sequence.
        */
        template <class F, class TCompareTraits>
        constexpr auto ordering_flat_mapping(F applicator, TCompareTraits cmp) noexcept
        {
            return OrderingFlatMappingFactory<F, TCompareTraits>(
                std::move(applicator), std::move(cmp));
        }

        template <class F>
        constexpr auto ordering_flat_mapping(F applicator) noexcept
        {
            return OrderingFlatMappingFactory<F, CompareTraits>(
                std::move(applicator), CompareTraits{});
        }
        
        template <size_t N, class TThreads>
        constexpr auto parallel_sort(TThreads& pool) noexcept
        {
            return ParallelSortFactory<N, TThreads>{pool};
        }

        /** Same as '&' operator for LazyRange, but allows use `>>` operator 
        *
        */
        template <class Right>
        constexpr auto ordered_join(Right&& right) noexcept
        {
            return OrderedJoinFactory<Right>(std::forward<Right>(right));
        }

        /** Same as '&' operator for LazyRange, but allows specify comparator of joining keys
        *  \tparam Comp - functor that matches to signature `signed f( const Right::element_t& , const Right::element_t)`. Where 
        *   the result of the comparator functor is interpreted as follows: 
        *       - two elements are equal if the result is 0, 
        *       - the left element is less if the return value is less than zero, 
        *       - and the left element is greater if the return value is greater than zero."
        */
        template <class Right, class Comp>
        constexpr auto ordered_join(Right&& right, Comp comp) noexcept
        {
            return OrderedJoinFactory<Right, Comp>(
                std::forward<Right>(right), std::move(comp));
        }

        template <class Right, class Comp>
        constexpr auto unordered_join(Right&& right, Comp comp) noexcept
        {            
            return UnorderedJoinFactory<Right>(
                std::forward<Right>(right), std::move(comp));
        }

        template <class Right>
        constexpr auto unordered_join(Right&& right) noexcept
        {            
            return UnorderedJoinFactory<Right>(std::forward<Right>(right));
        }

        template <class Right>
        constexpr auto auto_join(Right&& right) noexcept
        {
            return AdaptiveJoinFactory<Right>(std::forward<Right>(right));
        }
        /**
        *   Find difference between set. When sets contains duplicates behaviour is same as std::set_difference, it will 
        *   be output std::max(m-n, 0), where m - number of duplicates from source and n number of duplicates in Subtrahend.
        */
        template <class TSubtrahend>
        constexpr auto ordered_diff(TSubtrahend&& right) noexcept
        {
            return DiffFactory<DiffAlgorithm::ordered, TSubtrahend>(
                    std::forward<TSubtrahend>(right));
        }

        /** 
        * \tparam TComparators - pack of 3 (less, equal, hash). You can use OP::OverrideComparisonAndHashTraits
        *   to simplify implementation
        */
        template <class TSubtrahend, class TComparators>
        constexpr auto ordered_diff(TSubtrahend&& right, TComparators&& cmp) noexcept
        {
            return DiffFactory<DiffAlgorithm::ordered, TSubtrahend, TComparators>(
                    std::forward<TSubtrahend>(right),
                    std::forward<TComparators>(cmp));
        }

        template <class TSubtrahend>
        constexpr auto unordered_diff(TSubtrahend&& right) noexcept
        {
            return DiffFactory<DiffAlgorithm::unordered, TSubtrahend>(
                    std::forward<TSubtrahend>(right)
                );
        }

        template <class TSubtrahend, class TComparators>
        constexpr auto unordered_diff(TSubtrahend&& right, TComparators&& cmp) noexcept
        {
            return DiffFactory<DiffAlgorithm::unordered, TSubtrahend, TComparators>(
                    std::forward<TSubtrahend>(right), 
                    std::forward<TComparators>(cmp)
                );
        }

        template <class Right>
        constexpr auto auto_diff(Right&& right) noexcept
        {
            return DiffFactory<DiffAlgorithm::automatic, Right>(
                    std::forward<Right>(right)
                );
        }

        /**
        *   Non-ordered sequence that unions all (place side-by-side)
        */
        template <class ... Rx>
        constexpr auto union_all(Rx&& ...right) noexcept
        {
            return UnionAllSequenceFactory<Rx...>(std::forward<Rx>(right) ...);
        }

        /**
        *   Ordered sequence that applies union-merge (placed side-by-side). It is undefined behaviour
        * when sequence doesn't support ordering.
        */
        template <class T>
        constexpr auto union_merge(T&& right) noexcept
        {
            return MergeSortUnionFactory<full_compare_t, T>(
                full_compare_t{}, std::forward<T>(right) );
        }

        template <class T, class Cmp>
        constexpr auto union_merge(T&& right, Cmp cmp) noexcept
        {
            return MergeSortUnionFactory<full_compare_t, T>(
                std::move(cmp), std::forward<T>(right) );
        }

        /** Convert source by filtering out some elements according to predicate 
        * If applied to ordered source then result is ordered as well
        * \tparam Fnc - must be a predicate producing false for items to filter out
        */
        template <class Fnc>
        constexpr auto filter(Fnc f) noexcept
        {
            return FilterFactory<Fnc>(std::move(f));
        }

        template <class Fnc>
        constexpr auto take_awhile(Fnc f) noexcept
        {
            return TakeAwhileFactory<Fnc>(std::move(f));
        }
        
        /** 
        *   Remove duplicates from unordered source sequence. 
        *
        * \tparam EqCmp - binary comparator to return true if 2 items are equal
        */
        template <class Eq>
        constexpr auto unordered_distinct(Eq eq) noexcept
        {
            return DistinctFactory<UnorderedDistinctPolicyWithCustomComparator<Eq>>(std::move(eq));
        }
        
        constexpr auto unordered_distinct() noexcept
        {
            return DistinctFactory<UnorderedHashDistinctPolicy>(UnorderedHashDistinctPolicy{});
        }
        
        /**
        *   Remove duplicates from sorted source sequence. 
        *   Internaly uses `std::equal_to`
        */
        constexpr auto ordered_distinct() noexcept
        {
            return DistinctFactory<OrderedDistinctPolicy>(OrderedDistinctPolicy{});
        }

        /**
        *   Remove duplicates from sorted source sequence using the custom comparator F. 
        */
        template <class F>
        constexpr auto ordered_distinct(F f) noexcept
        {
            return DistinctFactory<OrderedDistinctPolicyWithCustomComparator<F>>(
                OrderedDistinctPolicyWithCustomComparator<F>(std::move(f)));
        }

        /** Distinct that depending on support ordering in source sequence automaticlly applies 
        * either `ordered_distinct` or `unordered_distinct`
        */
        constexpr auto auto_distinct() noexcept
        {
            auto d1 = ordered_distinct();
            auto d2 = unordered_distinct();
            return SmartDistinctFactory(std::move(d1), std::move(d2));
        }
        /**
        *   Remove duplicates using the custom comparator F and automatic detection if source sequence is ordered. 
        */
        template <class F>
        constexpr auto auto_distinct(F f) noexcept
        {
            auto d1 = ordered_distinct(f);//copy
            auto d2 = unordered_distinct(std::move(f));//move
            return SmartDistinctFactory(std::move(d1), std::move(d2));
        }

        /**
        *  \brief Produce cartesian product of multiple (1...N) flur sources. 
        *
        *  Cartesian product . This function is from `src` namespace \sa `then::cartesian` when need combine pipeline with product.
        * \tparam F - function that accept N+1 arguments (element from source and element from Alien and Ax...) then 
        *           produces target result element. As an example of result you may use std::pair
        * \tparam Alien, Ax... - some other pipeline(s) to make cartesian product with main flur sequence
        */
        template <class F, class Alien, class ... Ax>
        constexpr auto cartesian(F f, Alien&& alien, Ax&& ...ax) noexcept
        {
            //const second_src_t<Alien>&
            //auto cros = std::function<R(const Src&, const R&)>(fnc);
            return CartesianFactory<F, Alien, Ax...>(std::move(f), std::forward<Alien>(alien), std::forward<Ax>(ax)...);
        }

        /**
        * \brief Combine multiple sequences using the zip algorithm and produce elements as a result of applying `F`.
        * 
        * This function from `then` namespace is almost the same as \sa `src::zip` but intended to be used 
        * with pipeline operator `>>`
        */ 
        template <class F, class Alien, class ... Ax>
        constexpr auto zip(F f, Alien&& alien, Ax&& ...ax) noexcept
        {
            return 
                ZipFactory<F, Alien, Ax...>(std::move(f), std::forward<Alien>(alien), std::forward<Ax>(ax)...)
            ;
        }

        /** Produce repeater (for details see OP::flur::Repeater */
        constexpr auto repeater() noexcept
        {
            return RepeaterFactory();
        }

        /** Produce repeater (for details see OP::flur::Repeater */
        template <template <typename...> class TContainer = std::deque>
        constexpr auto repeater() noexcept
        {
            return RepeaterFactory<TContainer>();
        }

        template <size_t N, class TThreads>
        constexpr auto minibatch(TThreads& thread_pool) noexcept
        {
            return MinibatchFactory<N, TThreads>{thread_pool};
        }

        /** Treate source sequence as a roots of hierarchy and then iterate over children elements
        * using deep-first algorithm. Implementation doesn't use recursion so depth of hierarchy 
        * restricted only by available memory
        * \tparam FChildrenResolve functor that resolves children lazy range.
        * 
        * Usage example :\code
        *    using namespace OP::flur;
        *    for (auto x : src::of(std::vector{ 1, 2 })
        *        >> then::hierarchy_deep_first([](int i) {
        *                // children are int numbers bigger than source by 10
        *                std::vector<int> result{};
        *                if( i < 100 )
        *                {
        *                    result.emplace_back(i * 10);
        *                    result.emplace_back(i * 10 + 1);
        *                }
        *                return src::of(result);
        *            }))
        *        std::cout << x << ",";
        *     std::cout << "\n";
        * \endcode
        * It prints: 10,100,101,11,110,111,20,200,201,21,210,211,
        */
        template <class FChildrenResolve>
        constexpr auto hierarchy_deep_first(FChildrenResolve children_resolve) noexcept
        {
            return HierarchyTraversalFactory<std::decay_t<FChildrenResolve>, HierarchyTraversal::deep_first>
                {std::move(children_resolve)};
        }

        /** Treate source sequence as a roots of hierarchy and then iterate over children elements
        * using breadth-first algorithm. Implementation doesn't use recursion so depth of hierarchy 
        * restricted only by available memory
        * \tparam FChildrenResolve functor that resolves children lazy range. 
        *
        * Usage example :\code
        *    using namespace OP::flur;
        *    for (auto x : src::of(std::vector{ 1, 2 })
        *        >> then::hierarchy_breadth_first([](int i) {
        *                // children are int numbers bigger than source by 10
        *                std::vector<int> result{};
        *                if( i < 100 )
        *                {
        *                    result.emplace_back(i * 10);
        *                    result.emplace_back(i * 10 + 1);
        *                }
        *                return src::of(result);
        *            }))
        *        std::cout << x << ",";
        *     std::cout << "\n";
        * \endcode
        * It prints: 10,11,20,21,100,101,110,111,200,201,210,211,
        */
        template <class FChildrenResolve>
        constexpr auto hierarchy_breadth_first(FChildrenResolve children_resolve) noexcept
        {
            return HierarchyTraversalFactory<std::decay_t<FChildrenResolve>, HierarchyTraversal::breadth_first>{
                std::move(children_resolve)};
        }

        

        template <class TFactory>
        auto make_polymorph(TFactory &&poly)
        {
            using impl_t = PolymorphFactory<TFactory>;
            using base_t = typename impl_t::base_t;
            return std::shared_ptr< base_t>{new PolymorphFactory( std::forward<TFactory>(poly) )};
        }
    } //ns:then

    namespace apply
    {
        /**
        * ( src::of(...) >> then::filter(...) 
                >> apply::copy(std::back_inserter(to_vector)) 
                >> apply::collect(apply::to_stream) 
                >> apply::cartesian(f3_arg, seq2, seq3)
                >> apply::zip(f2_arg, seq2) 
          ).run( thread_pool )
        template <class OutIter>
        auto copy(OutIter out)
        {
            return MultiConsumer([](const auto& seq_element){
                    
            });
        }
        */

        template <class ... Lx>
        constexpr auto cartesian(Lx&& ... lx)
        {
            return CartesianApplicator(std::forward<Lx>(lx)...);    
        }

        template <class F, class ... Lx>
        constexpr auto zip()
        {return 42;}

    }//ns:apply
} //ns:OP::flur

#endif //_OP_FLUR_FLUR__H_
