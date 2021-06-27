#pragma once
#ifndef _OP_FLUR_LAZYRANGE__H_
#define _OP_FLUR_LAZYRANGE__H_

#include <functional>
#include <memory>
#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <tuple>

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
        static_assert(
            std::conjunction_v<std::is_base_of<FactoryBase, Tx>...>,
            "LazyRange must be instantiated by classes inherited from FactoryBase"
            );

        using factory_store_t = std::tuple<Tx...>;
        factory_store_t _storage;


        constexpr LazyRange(std::tuple<Tx...>&& tup) noexcept
            : _storage{ std::move(tup) }
        {
        }

        constexpr LazyRange(Tx && ...tx) noexcept
            : _storage{ std::forward<Tx>(tx) ... }
        {
        }

        constexpr auto compound() const noexcept
        {
            return details::unpack(std::move(details::compound_impl(_storage)));
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
    };
    /** Simplifies creation of LazyRange */
    template <class ... Tx >
    constexpr auto make_lazy_range(Tx && ... tx) noexcept ->LazyRange<Tx ...>
    {
        return LazyRange<Tx ...>(std::forward<Tx>(tx) ...);
    }
} //ns:flur
} //ns:OP

#endif //_OP_FLUR_LAZYRANGE__H_