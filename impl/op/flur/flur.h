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
#include <op/flur/TakeAwhile.h>
#include <op/flur/FlatMapping.h>
#include <op/flur/Mapping.h>
#include <op/flur/maf.h>
#include <op/flur/OrDefault.h>
#include <op/flur/Diff.h>
#include <op/flur/OnException.h>
#include <op/flur/Repeater.h>
#include <op/flur/Minibatch.h>
#include <op/flur/StringInput.h>
#include <op/flur/Distinct.h>
#include <op/flur/UnionAll.h>
#include <op/flur/HierarchyTraversal.h>
#include <op/flur/PolymorphsBack.h>
#include <op/flur/Applicator.h>
#include <op/flur/stl_adapters.h>

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
            return make_lazy_range(OfContainerFactory<T>(std::forward<T>(t)));
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
        * \param v value to resolve during sequence iteration.
        * \param limit number of times to repeat result v during sequence iteration. The default is 1. Value 0 is
        *   allowed, but from optiomization perspective better to use `OP::flur::src::null()` instead.
        * \sa of_cref_value
        */
        template <class V>
        constexpr auto of_value(V&& v, size_t limit = 1) noexcept
        {
            return make_lazy_range( SimpleFactory<V, OfValue<V>>(std::forward<V>(v)) );
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
        *   allowed, but from optiomization perspective better to use `OP::flur::src::null()` instead.
        * \tparam F - functor to return value. It may be declated as: 
        *   - no argument function;
        *   - any combination of `const OP::flur::PipelineAttrs&`, `size_t` (copy of limit argument);
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

        /**
        * Create LazyRange in a way similar to std::iota - specify range of values that support ++ operator.
        * For example:\code
        * 
        * \endcode
        */
        template <class V>
        constexpr auto of_iota(V begin, V end) noexcept
        {
            using iota_value_t = std::decay_t<V>;
            return make_lazy_range( SimpleFactory<std::pair<V, V>, 
                OfIota<iota_value_t>>(std::move(begin), std::move(end)) );
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
        * has minimal memory overhead since to access uses std::string_view
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
        OP_CONSTEXPR_CPP20 const static inline std::basic_string< typename Str::value_type > default_separators_c(" ");

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

        /** Produce new source that is result of applying function to each element of origin source.
        * \tparam options_c - extra options to customize sequence behavior. Implementation recognizes 
        * none or Intrinsic::keep_order - to allow keep source sequence order indicator.
        */
        template <auto ... options_c, class F>
        constexpr auto mapping(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MappingFactory<f_t, OP::utils::any_of<options_c...>(Intrinsic::keep_order)>(std::move(f));
        }

        /** Equivalent to call of `mapping<Intrinsic::keep_order>(std::move(f))`
        * Creates mapping factory to produces a sequence that keeps ordering.
        * Result sequence is not mandatory ordered it just commitment of developer of `F` to keep order  
        * if source sequenceis ordered as well.
        */
        template <class F>
        constexpr auto keep_order_mapping(F f) noexcept
        {
            using f_t = std::decay_t<F>;
            return MappingFactory<f_t, true>(std::move(f));
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
        * if source sequenceis ordered as well.
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
        constexpr auto flat_mapping(F&& f) noexcept
        {
            return FlatMappingFactory<F, options_c...>(std::forward<F>(f));
        }

        /** Same as '&' operator for LazyRange, but allows use `>>` operator 
        *
        */
        template <class Right>
        constexpr auto ordered_join(Right&& right) noexcept
        {
            return PartialJoinFactory<Right>(std::forward<Right>(right));
        }

        /** Same as '&' operator for LazyRange, but allows specify comparator of joining keys
        *  \tparam Comp - functor that matches functor `signed f( const Right::element_t& , const Right::element_t)`
        */
        template <class Right, class Comp>
        constexpr auto ordered_join(Right&& right, Comp&& comp) noexcept
        {
            return PartialJoinFactory<Right, Comp>(
                std::forward<Right>(right), std::forward<Comp>(comp));
        }

        /**
        *   Find difference between set. When sets contains dupplicates behaviour is same as std::set_difference, it will 
        *   be output std::max(m-n, 0), where m - number of dupplicates from source and n number of dupplicates in Subtrahend.
        */
        template <class TSubtrahend>
        constexpr auto ordered_diff(TSubtrahend&& right) noexcept
        {
            return DiffFactory(
                PolicyFactory<true, TSubtrahend>(std::forward<TSubtrahend>(right))
            );
        }

        /** 
        * \tparam TComparators - pack of 3 (less, equal, hash). You can use OP::OverrideComparisonAndHashTraits
        *   to simplify implementation
        */
        template <class TSubtrahend, class TComparators>
        constexpr auto ordered_diff(TSubtrahend&& right, TComparators&& cmp) noexcept
        {
            return DiffFactory(
                PolicyFactory<true, TSubtrahend, TComparators>(
                    std::forward<TSubtrahend>(right),
                    std::forward<TComparators>(cmp))
                );
        }

        template <class TSubtrahend>
        constexpr auto unordered_diff(TSubtrahend&& right) noexcept
        {
            return DiffFactory(
                PolicyFactory<false, TSubtrahend>(std::forward<TSubtrahend>(right))
            );
        }
        template <class TSubtrahend, class TComparators>
        constexpr auto unordered_diff(TSubtrahend&& right, TComparators&& cmp) noexcept
        {
            return DiffFactory(
                PolicyFactory<false, TSubtrahend, TComparators>(
                    std::forward<TSubtrahend>(right), std::forward<TComparators>(cmp))
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

        /** Convert source by filtering out some ellements according to predicate 
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
        *   Remove duplicates from source sequence. (Current implementation works only with 
        *   sorted)
        * \tparam EqCmp - binary comparator to return true if 2 items are equal
        */
        template <class Eq>
        constexpr auto unordered_distinct(Eq eq) noexcept
        {
            return DistinctFactory<UnorderedDistinctPolicyWithCustomComparator<Eq>>(std::move(eq));
        }
        
        constexpr auto unordered_distinct() noexcept
        {
            return DistinctFactory<UnorderedHashDistinctPolicy>();
        }
        
        /**
        *   Remove duplicates from sorted source sequence. 
        *   Internaly uses `std::equal_to`
        */
        constexpr auto ordered_distinct() noexcept
        {
            return DistinctFactory<OrderedDistinctPolicy>();
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

        /**
        *  Produce cartesian product of main flur source with 1...N other sources. 
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
