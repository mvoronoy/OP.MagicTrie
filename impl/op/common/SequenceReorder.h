#pragma once

#ifndef _OP_COMMON_SEQUENCEREORDER__H_
#define _OP_COMMON_SEQUENCEREORDER__H_

#include <compare> //for partial_order
#include <utility>

namespace OP::common
{
    /**
     * @brief Compile-time sequence of indices with some usefull utilities, including reordering and tuple traversal.
     *
     * `SequenceReorder<Kx...>` is a lightweight compile-time wrapper around
     * `std::index_sequence<Kx...>`. It provides operations for manipulating
     * the order of indices and for invoking a callable with tuple elements
     * in that order.
     *
     * Several utility methods perform a compile-time reordering of index values, like:
     * - #append - concatenate several indexes together into new one;
     * - #reverse - create new sequence in revert order;
     * - #reorder - topological-style ordering according to a user-supplied partial-order
     * predicate. 

     * The resulting sequence can be used with `apply()` to traverse a tuple
     * in the calculated order. This is particularly useful when a tuple
     * represents services or other objects whose initialization or shutdown
     * order is constrained by dependencies.
     *
     * @tparam Kx Indices contained in the sequence.
     */
    template <size_t ...Kx>
    class SequenceReorder : public std::index_sequence<Kx...>
    {
        template <size_t ... Ix, size_t ... Jx>
        static constexpr auto cat_seq(std::index_sequence<Ix...>, std::index_sequence<Jx...>) noexcept
        {
            return std::index_sequence<Ix..., Jx...>{};
        }

        template <class U>
        static constexpr const auto cat_seq(U u) noexcept
        {
            return u;
        }

        template <class U, class ...Ux>
        static constexpr auto cat_seq(U u, Ux ...ux) noexcept
        {
            return cat_seq(u, cat_seq(ux...));
        }

        template <class FReorderPredicate, size_t I, size_t J, size_t ... Jx>
        constexpr static auto ordered_ins(std::index_sequence<I> s1, std::index_sequence<J, Jx...> ordered) noexcept
        {
            constexpr auto ij_rel = FReorderPredicate::template predicate<I, J>;
            if constexpr (std::partial_ordering::greater == ij_rel || std::partial_ordering::unordered == ij_rel)
                return cat_seq(
                    std::index_sequence<J>{}, 
                    ordered_ins<FReorderPredicate>(std::index_sequence<I>{}, std::index_sequence<Jx...>{}));
            else //all other cases keep source order unchanged
                return std::index_sequence<I, J, Jx...>{};
        }

        template <class FReorderPredicate, size_t I, size_t J>
        constexpr static auto ordered_ins(std::index_sequence<I> s1, std::index_sequence<J> ordered) noexcept
        {
            constexpr auto ij_rel = FReorderPredicate::template predicate<I, J>;
            if constexpr (std::partial_ordering::greater == ij_rel )
                return std::index_sequence<J, I>{};
            else //all other cases keep source order unchanged
                return std::index_sequence<I, J>{};
        }

        template <class FReorderPredicate, size_t I>
        constexpr static auto ordered_ins(std::index_sequence<I> s1, std::index_sequence<> ordered) noexcept
        {
            return s1;
        }

        template <class FReorderPredicate, size_t ... Jx>
        constexpr static auto busort(std::index_sequence<> unordered, std::index_sequence<Jx...> ordered) noexcept
        {
            return ordered;
        }

        template <class FReorderPredicate, size_t I0, size_t ... Jx>
        constexpr static auto busort(std::index_sequence<I0> unordered, std::index_sequence<Jx...> ordered) noexcept
        { // linear scan
            return ordered_ins<FReorderPredicate>(unordered, ordered);
        }

        template <class FReorderPredicate, size_t I0, size_t I1, size_t ... Ix, size_t ... Jx>
        constexpr static auto busort(std::index_sequence<I0, I1, Ix...> unordered, std::index_sequence<Jx...> ordered) noexcept
        {
            constexpr auto i0i1rel = FReorderPredicate::template predicate<I0, I1>;
            if constexpr (std::partial_ordering::equivalent == i0i1rel)
                return busort<FReorderPredicate>(std::index_sequence<I0, Ix...>{}, ordered);
            else if constexpr (std::partial_ordering::less == i0i1rel)
                return busort<FReorderPredicate>(std::index_sequence<I0, Ix...>{},
                    ordered_ins<FReorderPredicate>(std::index_sequence<I1>{}, ordered)
                );
            else if constexpr (std::partial_ordering::greater == i0i1rel)
                return busort<FReorderPredicate>(std::index_sequence<I1, Ix...>{},
                    ordered_ins<FReorderPredicate>(std::index_sequence<I0>{}, ordered)
                );
            else  //std::partial_ordering::unordered == i0i1rel
                return busort<FReorderPredicate>(std::index_sequence<Ix...>{},
                    ordered_ins<FReorderPredicate>(std::index_sequence<I0>{}, 
                        ordered_ins<FReorderPredicate>(std::index_sequence<I1>{}, ordered))
                );
        }

        template <size_t I, size_t J>
        static constexpr auto reverse_impl(std::index_sequence<I, J>) noexcept
        {
            return std::index_sequence<J, I>{};
        }

        template <size_t I>
        static constexpr auto reverse_impl(std::index_sequence<I> s1) noexcept
        {
            return s1;
        }

        static constexpr auto reverse_impl(std::index_sequence<> s0) noexcept
        {
            return s0;
        }

        template <size_t I, size_t ...Ix>
        static constexpr auto reverse_impl(std::index_sequence<I, Ix...>) noexcept
        {
            return cat_seq(reverse_impl(std::index_sequence<Ix...>{}), std::index_sequence<I>{});
        }

        constexpr static inline size_t reindex_item(size_t i) noexcept
        {
            return (((i-- == 0) ? Kx : 0) | ...); //use feature that (x | 0 |...) = x
        }
        
        template <size_t ...Ix>
        constexpr static auto factory(std::index_sequence<Ix...> sq) noexcept
        {
            return SequenceReorder<Ix...>{sq};
        }
    public:
        using this_t = SequenceReorder<Kx...>;

        constexpr SequenceReorder() = default;

        template <size_t ...Ix>
        constexpr SequenceReorder(std::index_sequence<Ix...> sq) noexcept
        {
        }

        /**
         * @brief Reorders the sequence according to a compile-time partial order.
         *
         * The predicate is evaluated for pairs of indices and the source sequence
         * is transformed into an order consistent with the relationships described
         * by the predicate. The operation supports partial orders: when the
         * predicate reports `std::partial_ordering::unordered`, no dependency
         * relationship is imposed between the two indices.
         *
         * For example, following predicate implementing the natural numerical ordering
         * transforms `SequenceReorder<2, 1, 3>` into
         * `SequenceReorder<1, 2, 3>`: 
         * template named `predicate`:
         *
         * @code
         * struct Predicate
         * {
         *     template <size_t I, size_t J>
         *     constexpr static std::partial_ordering predicate =
         *         (I < J) ? std::partial_ordering::less :
         *         (J < I) ? std::partial_ordering::greater :
         *                    std::partial_ordering::equivalent;
         * };
         * @endcode
         *The predicate is expected to expose a static variable `predicate` of std::partial_ordering type.
         * `std::partial_ordering` is intentional: in addition to `less`,
         * `greater`, and `equivalent`, the predicate may return `unordered`.
         * This allows the caller to express topological sorting without imposing an
         * artificial order on independent elements.
         *  
         *
         * The operation is entirely compile-time and returns a new
         * `SequenceReorder`; the source sequence is not modified.
         *
         * @tparam FReorderPredicate Type providing
         *         `template <size_t I, size_t J> static constexpr
         *         std::partial_ordering predicate`.
         *
         * @return A `SequenceReorder` containing the indices in the order
         *         determined by `FReorderPredicate`.
         */
        template <class FReorderPredicate>
        static constexpr auto reorder() noexcept
        {
            return factory(
                busort<FReorderPredicate>(std::index_sequence<Kx...>{}, std::index_sequence<>{}));
        }

        /**
         * @brief Reverses the order of all indices in the sequence.
         *
         * For example, if the sequence is `SequenceReorder<0, 1, 2>`, the result
         * is `SequenceReorder<2, 1, 0>`.
         *
         * @return A new `SequenceReorder` containing the same indices in reverse
         *         order.
         * The operation is entirely compile-time and returns a new
         * `SequenceReorder`; the source sequence is not modified.
        */
        static constexpr auto reverse() noexcept
        {
            return factory(reverse_impl(std::index_sequence<Kx...>{}));
        }
        
        /**
         * @brief Appends one or more index sequences to the current sequence.
         *
         * Elements are appended in the same order in which the sequences are
         * passed to the function. No sorting, deduplication, or other
         * transformation is performed.
         *
         * Example:
         * @code
         * using S = SequenceReorder<3, 4, 5>;
         * constexpr auto result =
         *     S::append(std::index_sequence<10>{},
         *               std::index_sequence<11, 12>{});
        * @endcode
         *
         * The type of `result` is `SequenceReorder<3, 4, 5, 10, 11, 12>`.
         *
         * @tparam Ux Sequence-like types parameterized by `size_t` indices,
         *         such as `std::index_sequence` and `SequenceReorder`.
         * @param ux Sequences whose indices are to be appended.
         *
         * @return A new `SequenceReorder` containing the current indices followed
         *         by all indices from the supplied sequences.
         * The operation is entirely compile-time and returns a new
         * `SequenceReorder`; the source sequence is not modified.
        */
        template <class ...Ux>
        static constexpr auto append(Ux ...ux) noexcept
        {
            return cat_seq(this_t{}, ux...);
        }

        /**
         * @brief Invokes a callable with tuple elements in this sequence's order.
         *
         * This function is analogous to `std::apply`, but the tuple elements are
         * selected according to `Kx...` rather than their natural index order.
         *
         * For example, `SequenceReorder<2, 0, 1>::apply(f, tuple)` invokes `f`
         * with `std::get<2>(tuple)`, then `std::get<0>(tuple)`, and finally
         * `std::get<1>(tuple)`.
         *
         * A typical use case is a tuple containing services whose initialization
         * or shutdown operations must respect dependencies. A sequence produced
         * by `reorder()` can be used with `apply()` to execute the operation in
         * that dependency-respecting order.
         *
         * @tparam FCallback Callable type, receiving the selected tuple elements.
         * @tparam Tuple Tuple-like type providing `std::get<Kx>(tuple)` for every
         *         index in the sequence.
         *
         * @param callback Callable to invoke with the selected tuple elements.
         * @param tuple Tuple-like object whose elements are passed to `callback`.
         *
         * @return The value returned by `callback`. If `callback` returns `void`,
         *         this function also returns `void`.
         *
         * @note The exception specification is propagated from the corresponding
         *       invocation of `callback` with the selected tuple elements.
         */
        template <class FCallback, class Tuple>
        static constexpr decltype(auto) apply(FCallback&& callback, Tuple&& tuple) noexcept(
            noexcept(std::invoke(std::forward<FCallback>(callback),
                std::get<Kx>(std::forward<Tuple>(tuple))...))
            )
        {
            return std::invoke(
                std::forward<FCallback>(callback), 
                std::get<Kx>(std::forward<Tuple>(tuple))...);
        }
    };
    // explicit deduction guide
    template<size_t... Ix> SequenceReorder(std::index_sequence<Ix...>) -> SequenceReorder<Ix...>;

    /** Just for debug purpose operator allows print sequence */
    template<size_t ...Ix>
    inline std::ostream& operator << (std::ostream& os, const SequenceReorder<Ix...>&)
    {
        os << "<";
        size_t i = 0;
        ((os << (i++ == 0 ? "" : ", ") << Ix), ...);
        os << ">";
        return os;
    }
}//ns:OP::common

#endif //_OP_COMMON_SEQUENCEREORDER__H_
