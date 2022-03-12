#pragma once
#ifndef _OP_FLUR_LAZYRANGE__H_
#define _OP_FLUR_LAZYRANGE__H_

#include <functional>
#include <memory>
#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRangeIter.h>
#include <tuple>
#include <op/flur/Polymorphs.h>
#include <op/flur/Join.h>

namespace OP
{
/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace flur
{
    /** 
    * Represent holder that allows at compile time form pipeline of source transformations 
    *  
    */
    template <class ... Tx>
    struct LazyRange : FactoryBase
    {
        template <class A>
        using target_range_t = std::decay_t<decltype(details::get_reference(std::declval<A>()))>;

        static_assert(
            std::conjunction_v<std::is_base_of<FactoryBase, target_range_t<Tx>>...>,
            "LazyRange must be instantiated by classes inherited from FactoryBase"
            );
        using this_t = LazyRange<Tx...>;

        using factory_store_t = std::tuple<Tx...>;
        factory_store_t _storage;

        //constexpr LazyRange(this_t&&) noexcept = default;

        constexpr LazyRange(std::tuple<Tx...>&& tup) noexcept
            : _storage{ std::move(tup) }
        {
        }

        constexpr LazyRange(Tx && ...tx) noexcept
            : _storage{ std::forward<Tx>(tx) ... }
        {
        }

        constexpr auto compound() const& noexcept
        {
            return std::move(details::compound_impl(_storage));
        }
        constexpr auto compound() && noexcept
        {
            return std::move(details::compound_impl(std::move(_storage)));
        }

        template <class T>
        constexpr auto operator >> (T&& t) noexcept
        {
            return LazyRange < Tx ..., T >(std::tuple_cat(_storage, std::make_tuple(std::forward<T>(t))));
        }

        template <class ... Ux>
        constexpr auto operator >> (LazyRange<Ux ...> && lr) noexcept
        {
            using arg_t = LazyRange<Ux ...>;
            return LazyRange < Tx ..., Ux ... >(std::tuple_cat(_storage, lr._storage));
        }

        template <class ... Ux>
        constexpr auto operator >> (const LazyRange<Ux ...>& lr) noexcept
        {
            using arg_t = LazyRange<Ux ...>;
            return LazyRange < Tx ..., Ux ... >(std::tuple_cat(_storage, lr._storage));
        }

        template <class ... Ux>
        constexpr auto operator & (const LazyRange<Ux ...>& lr) const& noexcept
        {
            using arg_t = LazyRange<Ux ...>;
            using join_factory_t = JoinFactory<this_t, arg_t>;

            return LazyRange < join_factory_t >(
                join_factory_t(*this, lr));
        }
        template <class ... Ux>
        constexpr auto operator & (LazyRange<Ux ...>&& lr) && noexcept
        {
            using arg_t = LazyRange<Ux ...>;
            using join_factory_t = JoinFactory<this_t, arg_t>;
            return LazyRange < join_factory_t >(
                join_factory_t(std::move(*this), std::move(lr)));
        }

        auto begin() const
        {
            using t_t = decltype(compound());
            auto seq_ptr = std::make_shared<t_t>(std::move(compound()));
            return LazyRangeIterator< decltype(seq_ptr) >(std::move(seq_ptr));
        }
        auto end() const
        {
            using t_t = decltype(compound());
            return LazyRangeIterator< std::shared_ptr<t_t> >(nullptr);
        }

    };
    /** Simplifies creation of LazyRange */
    template <class ... Tx >
    constexpr LazyRange<Tx ...> make_lazy_range(Tx && ... tx) noexcept 
    {
        return LazyRange<Tx ...>(std::forward<Tx>(tx) ...);
    }
    
    /** Create polymorph LazyRange (with type erasure). 
    *   @return instance of AbstractPolymorphFactory as std::unique_ptr
    */
    template <class ... Tx >
    constexpr auto make_unique(Tx && ... tx) 
    {
        using impl_t = PolymorphFactory<LazyRange<Tx ...>>;
        using interface_t = typename impl_t::polymorph_base_t;

        return std::unique_ptr<interface_t>( new impl_t{LazyRange<Tx ...>(std::forward<Tx>(tx) ...)});
    }
    template <class ... Tx >
    constexpr auto make_unique(LazyRange<Tx ...> && range) 
    {
        using lrange_t = LazyRange<Tx ...>;
        using impl_t = PolymorphFactory<lrange_t>;
        using interface_t = typename impl_t::polymorph_base_t;

        return std::unique_ptr<interface_t>( new impl_t{ std::forward<lrange_t>(range) } );
    }
    template <class ... Tx >
    constexpr auto make_shared(Tx &&... tx) 
    {
        using impl_t = PolymorphFactory<LazyRange<Tx ...>>;
        using interface_t = typename impl_t::polymorph_base_t;

        return std::shared_ptr<interface_t>( new impl_t{LazyRange<Tx ...>(std::forward<Tx>(tx) ...)});
    }
    template <class ... Tx >
    constexpr auto make_shared(LazyRange<Tx ...> && range) 
    {
        using lrange_t = LazyRange<Tx ...>;
        using impl_t = PolymorphFactory<lrange_t>;
        using interface_t = typename impl_t::polymorph_base_t;

        return std::shared_ptr<interface_t>( new impl_t{ std::forward<lrange_t>(range) } );
    }

    template <class ... Tx >
    size_t consume_all(LazyRange<Tx ...>& range) 
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

} //ns:flur
} //ns:OP
#endif //_OP_FLUR_LAZYRANGE__H_
