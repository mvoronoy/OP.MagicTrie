#pragma once
#ifndef _OP_FLUR_LAZYRANGE__H_
#define _OP_FLUR_LAZYRANGE__H_

#include <functional>
#include <memory>
#include <tuple>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRangeIter.h>
#include <op/flur/Polymorphs.h>
#include <op/flur/OrderedJoin.h>
#include <op/flur/FactoryBase.h>
#include <op/flur/Applicator.h>
#include <op/flur/Bookmark.h>
#include <op/flur/UnionAll.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    /** \brief Predicate used together with OP::utils::type_filter_t to demarcate types inherited from FactoryBase */
    struct PredicateSelectFactories
    {
        template <class T>
        static constexpr bool check = is_factory_c<std::decay_t<details::dereference_t<T>>>;
    };

    /** 
     * \brief Main building element of OP::flur to form a pipeline of transformations.
     *  
     * LazyRange is built with respect to C++ metaprogramming concepts to compound
     * simple units of source information processing together at compile time. 
     * If you are familiar with functional programming conceptions - LazyRange acts similarly 
     * to a monad transformer, combining the effects of various monads into a single lazy computation pipeline.
     *
     * Developers almost never use LazyRange explicitly; instead, there are many inline functions
     * from the namespaces `OP::flur::src` and `OP::flur::then` that define the flow in the final LazyRange 
     * definition. For example:
     * \code
     * using namespace OP::flur;
     * src::of_value(42) >> then::mapping([](const auto& n) { return n * 57; });
     * \endcode
     * This, in fact, creates `LazyRange<SimpleFactory<...>, MappingFactory<...>>`. 
     *   
     * \tparam Tx Building unit of pipeline transformation. Must be inherited from `FactoryBase`. The first
     * entry of `Tx` must expose an `auto compound()` method that is a factory for some `Sequence<T>` interface. The 
     * subsequent entries of `Tx` must expose `template <TSequence> auto compound(TSequence&& previous)` 
     * implementation that takes the previous factory result as a Sequence and concatenates some processing unit. 
     * 
     * It is recommended that developers use `constexpr` and `noexcept` in the implementations of both `compound` methods to 
     * ensure compile-time evaluation and minimizing exception frame ovehead.
     * The main contribution of LazyRange is to expose several important operators:
     * 1) `operator >>` - to combine two FactoryBase instances into a single lazy chain.
     * 2) `operator >>=`, to consume the lazy chain by some applicator.
     * 3) `operator >>(Bookmark)`, to inject additional parameters for result evaluation.
     *
     * Also, LazyRange can be used in C++ syntax sugar for the `for` loop as it exposes specializations for 
     *  `std::begin` and `std::end`. For example: \code
     *  for(auto i: src::of_value(42) >> then::mapping([](const auto& n) { return n * 57; }))
     *      std::cout << i << "\n";
     *  \endcode
     * Prints to std out: `2394`.
     */
    template <class ... Tx>
    struct LazyRange : FactoryBase
    {
        static_assert(
            ((PredicateSelectFactories::check<Tx>
                || is_bookmark_c<details::dereference_t<Tx>>) && ...),
            "LazyRange must be instantiated by classes inherited from FactoryBase"
            );
        using this_t = LazyRange<Tx...>;

        using factories_t = std::tuple<Tx...>;//OP::utils::type_filter_t<PredicateSelectFactories, Tx...>;
        using bookmarks_t = std::tuple<>;//OP::utils::type_filter_t<PredicateSelectBookmarks, Tx...>;

        factories_t _factories;
        bookmarks_t _bookmarks;

        template <class ...Ux>
        constexpr LazyRange(
            std::tuple<Ux...> factories) noexcept
            : _factories{ std::move(factories) }
            , _bookmarks{}
        {
            static_assert(sizeof...(Ux) > 0, "non empty FactoryBase set must be specified");
        }
        
        constexpr LazyRange(std::in_place_t, Tx&& ...tx) noexcept
            : _factories{ std::forward<Tx>(tx) ... }
        {
            static_assert(std::tuple_size_v<decltype(_factories)> > 0, "non empty FactoryBase set must be specified");
        }

        constexpr auto compound() const& noexcept
        {
            return details::compound_impl(_factories);
        }

        constexpr auto compound() && noexcept
        {
            return details::compound_impl(std::move(_factories));
        }

        template <class TAction>
        void apply(TAction action) const
        {
            action(*this);
        }

        //template <class T>
        //constexpr auto operator >> (T&& t) const& noexcept
        //{
        //    return std::apply([&](const auto& ...a) {//use copy semantic
        //        return LazyRange<std::decay_t<Tx>..., std::decay_t<T> >{ 
        //            std::in_place_t{}, a..., std::forward<T>(t) };
        //        }, _factories);
        //}

        template <class T>
        constexpr auto operator >> (T&& t) && noexcept
        {
            return std::apply([&t](auto&& ...a) {
                return LazyRange<Tx..., T>{
                    std::in_place_t{}, std::move(a)..., std::forward<T>(t) };
                }, std::move(_factories));
        }

        template <class T>
        constexpr auto operator >> (const T& t) const& noexcept
        {
            return std::apply([&](const auto& ...a) {//uses copy semantic
                return LazyRange<Tx..., std::decay_t<T>>{
                    std::in_place_t{}, Tx(a)..., std::decay_t<T>{t} };
                }, _factories);
        }

        template <class ... Ux>
        constexpr auto operator >> (LazyRange<Ux ...> && lr) && noexcept
        {
            return std::apply([&](auto&& ...la) {
                return std::apply([&](auto&& ...ra) {
                    return LazyRange<Tx..., Ux...>{ std::in_place_t{}, std::move(la)..., std::move(ra)... };
                    }, std::move(lr));
                }, std::move(_factories));
        }

        template <class ... Ux>
        constexpr auto operator >> (std::tuple<Ux ...> && lr) && noexcept
        {
            return std::apply([&](auto&& ...la) {
                return std::apply([&](auto&& ...ra) {
                    return LazyRange<Tx ..., Ux...>{ std::in_place_t{}, std::move(la)..., std::move(ra)... };
                    }, std::move(lr));
                }, std::move(_factories));
        }

        template <class ... Ux>
        constexpr auto operator >> (const LazyRange<Ux ...>& lr)const& noexcept
        {
            return std::apply([&](const auto& ...la) {
                return std::apply([&](const auto& ...ra) {
                    return LazyRange{ std::in_place_t{}, la..., ra... };
                    }, lr._factories);
                }, _factories);
        }

        auto begin() const
        {
            return details::begin_impl(*this);
        }

        auto end() const
        {
            return details::end_impl<this_t>();
        }

    };

    /** Simplifies creation of LazyRange */
    template <class ... Tx >
    constexpr LazyRange<Tx...> make_lazy_range(Tx&& ... tx) noexcept
    {
        return LazyRange<Tx...>{ std::in_place_t{}, std::forward<Tx>(tx) ... };
    }

    //
    //  Commissioned as arguable reason
    //
    //template <class ... Tx >
    //constexpr auto make_lazy_range(std::piecewise_construct_t, Tx && ... tx) noexcept
    //{
    //    return LazyRange<std::decay_t<Tx> ...>(std::forward<Tx>(tx) ...);
    //}

    //
    //  Commissioned as arguable reason
    //
    ///** Create polymorph LazyRange (with type erasure). 
    //*   @return instance of AbstractPolymorphFactory as std::unique_ptr
    //*/
    //template <class ... Tx >
    //constexpr auto make_unique(Tx && ... tx) 
    //{
    //    using impl_t = PolymorphFactory<LazyRange<Tx ...>>;
    //    using interface_t = typename impl_t::base_t;
    //
    //    return std::unique_ptr<interface_t>( new impl_t{LazyRange<Tx ...>(std::forward<Tx>(tx) ...)});
    //}
    //
    //template <class ... Tx >
    //constexpr auto make_unique(LazyRange<Tx ...> && range) 
    //{
    //    using lrange_t = LazyRange<Tx ...>;
    //    using impl_t = PolymorphFactory<lrange_t>;
    //    using interface_t = typename impl_t::base_t;
    //
    //    return std::unique_ptr<interface_t>( new impl_t{ std::forward<lrange_t>(range) } );
    //}

    /**
    *   Wraps specific `TFactoryBase` to polymorph std::share_ptr<AbstractPolymorphFactory<U>> (with type erasure) 
    *   where `U` is element produced by sequence of `TFactoryBase`.
    */ 
    template <class TFactoryBase, 
        std::enable_if_t<std::is_base_of_v<FactoryBase, TFactoryBase>, int> = 0>
    constexpr auto make_shared(TFactoryBase&& tx)
    {
        static_assert( 
            !std::is_rvalue_reference_v<TFactoryBase>,
            "Please don't use make_shared with left-reference (always use move semantic)");
        using impl_t = PolymorphFactory<TFactoryBase>;
        using interface_t = typename impl_t::base_t;
        return std::shared_ptr<interface_t>( new impl_t{ std::move(tx) });
    }

    template <class TLeft, class TRight,
        std::enable_if_t<
            std::is_base_of_v<FactoryBase, TLeft> &&
            std::is_base_of_v<FactoryBase, TRight>, int > = 0
    >
    inline constexpr auto operator | (TLeft&& l, TRight&& r) noexcept
    {
        using union_all_factory_t = UnionAllSequenceFactory<TLeft, TRight>;

        return LazyRange{ std::in_place_t{},
            union_all_factory_t(std::forward<TLeft>(l), std::forward<TRight>(r)) };
    }

    template <class TLeft, class TRight, 
        std::enable_if_t<
                std::is_base_of_v<FactoryBase, TLeft> &&
                std::is_base_of_v<FactoryBase, TRight>, int > = 0
    >
    inline constexpr auto operator| (std::shared_ptr<TLeft> l, std::shared_ptr<TRight> r)
    {
        using effective_left_t = std::shared_ptr<TLeft>;
        using effective_right_t = std::shared_ptr<TRight>;
        using union_all_factory_t = UnionAllSequenceFactory<effective_left_t, effective_right_t>;
        using impl_t = PolymorphFactory<union_all_factory_t>;
        using interface_t = typename impl_t::base_t;

        return std::shared_ptr<interface_t>( new impl_t{ union_all_factory_t{std::move(l), std::move(r)} } );
    }

    template <class TLeft, class TRight,
        std::enable_if_t<
        std::is_base_of_v<FactoryBase, TLeft>&&
        std::is_base_of_v<FactoryBase, TRight>, int > = 0
    >
    inline constexpr auto operator& (TLeft l, TRight r) noexcept
    {
        using right_range_t = OrderedJoinFactory<TRight>;
        return std::move(l) >> right_range_t{std::move(r)};
    }

    template <class TLeft, class TRight,
        std::enable_if_t<
        std::is_base_of_v<FactoryBase, TLeft>&&
        std::is_base_of_v<FactoryBase, TRight>, int > = 0
    >
    inline constexpr auto operator& (std::shared_ptr<TLeft> l, std::shared_ptr<TRight> r)
    {
        using right_range_t = OrderedJoinFactory<std::shared_ptr<TRight>>;

        return make_shared(make_lazy_range(
            std::move(l), right_range_t{std::move(r)} ));
    }

    /** 
    * Allow multiple applicators like: \code
    *  src::of(...) >>= Sum(...) >>= Sum(...)
    * \endcode.
    *
    */
    template <class ...Tx, class TApplicator>
    const auto& operator >>= (const LazyRange<Tx...>& range, const TApplicator& applicator)
    {
        range.apply(applicator);
        return range; //allow multiple applicators
    }

    /** 
    * Allow multiple applicators like: \code
    *  src::of(...) >>= Sum(...) >>= Sum(...)
    * \endcode.
    *
    */
    template <class ...Tx, class TApplicator>
    const auto& operator >>= (const LazyRange<Tx...>& range, TApplicator& applicator)
    {
        range.apply(applicator);
        return range; //allow multiple applicators
    }

    template <class TFactoryBase, class TApplicator, 
        std::enable_if_t<
            std::is_base_of_v<FactoryBase, TFactoryBase> &&
            std::is_base_of_v<ApplicatorBase, TApplicator>, int > = 0>
    const auto& operator >>= (TFactoryBase& range, TApplicator applicator)
    {
        applicator(range);
        return range; //allow multiple applicators
    }


    //template <class ... Tx >
    //constexpr auto make_shared(LazyRange<Tx ...> range) 
    //{
    //    using lrange_t = LazyRange<Tx ...>;
    //    using impl_t = PolymorphFactory<lrange_t>;
    //    using interface_t = typename impl_t::base_t;

    //    return std::shared_ptr<interface_t>( new impl_t{ std::move(range) } );
    //}

    template <class ... Tx >
    size_t consume_all(const LazyRange<Tx ...>& range) 
    {
        size_t count = 0;
        auto seq = range.compound();
        for (seq.start(); seq.in_range(); seq.next())
        {
            static_cast<void>(seq.current()); //ignore return value
            ++count;
        }
        return count;
    }

    template <class G>
    std::enable_if_t<std::is_base_of_v<FactoryBase, G>, size_t> consume_all(const G& range) 
    {
        size_t count = 0;
        auto seq = range.compound();
        auto& seq_ref = details::get_reference(seq);
        for (seq_ref.start(); seq_ref.in_range(); seq_ref.next())
        {
            static_cast<void>(seq_ref.current()); //ignore return value
            ++count;
        }
        return count;
    }

    struct consume_all_stub {};
    constexpr static inline consume_all_stub consume_all_c;

    template <class ...Tx>
    auto operator >>= (const LazyRange<Tx...>& range, consume_all_stub)
    {
        return consume_all(range); 
    }


    /*template <class T >
    auto first( Sequence<T>& seq) 
    {
        seq.start(); 
        if(!seq.in_range())
        {
            throw std::out_of_range("takin `first` of empty lazy range");
        }
        return seq.current();
    }
    template <class T >
    auto first(std::unique_ptr< T > pseq)
    {
        return first(*pseq);
    }
    template <class T >
    auto first(std::shared_ptr< T > pseq)
    {
        return first(details::get_reference(pseq));
    }

    template <class T, typename P = std::enable_if_t<std::is_base_of_v<FactoryBase,T>>>
    auto first(T& range) 
    {
        return first( range.compound() );
    }*/
    template <class T >
    auto first(T&& flur_obj)
    {
        auto seq = details::get_reference(flur_obj).compound();
        auto& rseq = details::get_reference(seq);
        rseq.start();
        if (!rseq.in_range())
        {
            throw std::out_of_range("taking `first` of empty lazy range");
        }
        return rseq.current();

    }

} //ns:OP::flur

#endif //_OP_FLUR_LAZYRANGE__H_
