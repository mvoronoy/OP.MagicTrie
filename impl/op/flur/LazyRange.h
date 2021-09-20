#pragma once
#ifndef _OP_FLUR_LAZYRANGE__H_
#define _OP_FLUR_LAZYRANGE__H_

#include <functional>
#include <memory>
#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/flur/LazyRangeIter.h>
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
        template <class A>
        using trange_t = std::decay_t<decltype(details::get_reference(std::declval<A>()))>;

        static_assert(
            std::conjunction_v<std::is_base_of<FactoryBase, trange_t<Tx>>...>,
            "LazyRange must be instantiated by classes inherited from FactoryBase"
            );
        using this_t = LazyRange<Tx...>;

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

        template <class ... Ux>
        constexpr auto operator >> (const LazyRange<Ux ...>& lr) noexcept
        {
            using arg_t = LazyRange<Ux ...>;
            return LazyRange < Tx ..., Ux ... >(std::tuple_cat(_storage, lr._storage));
        }
        auto begin() const
        {
            return LazyRangeIterator< this_t >(*this);
        }
        auto end() const
        {
            return LazyRangeIterator< this_t >();
        }

        /** Apply functor Fnc to all items in this iterable starting from beginning */
        template <class Fnc>
        void for_each(Fnc f) const 
        {
            auto pipeline = compound();
            auto& r = details::get_reference(pipeline);
            for (r.start(); r.in_range(); r.next())
                f(r.current());
        }
        
        /**
        * \tparam Fnc - reducer callback must be of type T(T&& previous, pipeline_element_t)
        */
        template <class T, class Fnc>
        T reduce(Fnc f, T init = T{}) const
        {
            auto pipeline = compound();
            for (pipeline.start(); pipeline.in_range(); pipeline.next()) 
            {
                init = f(std::move(init), pipeline.current());
            }
            return init;
        }
        size_t count() const
        {
            size_t c = 0;
            auto pipeline = compound();
            auto& ref_pipeline = details::get_reference(pipeline);
            for (ref_pipeline.start(); ref_pipeline.in_range(); ref_pipeline.next())
            {
                ++c;
            }
            return c;
        }
        bool empty() const
        {
            auto pipeline = compound();
            pipeline.start();
            return !pipeline.in_range();
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