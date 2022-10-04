#pragma once
#ifndef _OP_FLUR_COMPARISON__H_
#define _OP_FLUR_COMPARISON__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>

namespace OP::flur
{
    /** Utility for three-way items compare inside arbitrary flur container */
    struct full_compare_t
    {
#ifdef OP_CPP20_FEATURES
        std::compare_three_way _impl;
        
        template <class T, class U>
        constexpr auto operator()(T&& left, U&& right) const
        {
            return _impl(left, right);
        }
#else
        template <class T, class U>
        constexpr int operator()(T&& left, U&& right) const
        {
            using less_impl_t = std::less<T>;
            less_impl_t impl;
            return impl(left, right) ? (-1) : (impl(right, left) ? 1 : 0);
        }
#endif //OP_CONSTEXPR_CPP20
    };
    
    namespace details
    {
        enum class CmpOp
        {
            less,
            equals
        };
        /** std::less or std::equal - like bool implementation on top of full comparison_t */
        template <CmpOp operation, class TCompareBase>
        struct CompareOnTopOfFullComparison
        {
            constexpr CompareOnTopOfFullComparison(TCompareBase base = TCompareBase{}) noexcept
                : _base(std::move(base))
            {}

            template <class T, class U>
            constexpr bool operator()(T&& left, U&& right) const
            {
                if constexpr(operation == CmpOp::less)
                    return _base(std::forward<T>(left), std::forward<U>(right)) < 0;
                else
                    return _base(std::forward<T>(left), std::forward<U>(right)) == 0;
            }
        private:
            TCompareBase _base;
        };
        
    }//ns:details

    /** Traits that aggregates default factory implementations of 3 operations :
    *    - three-way
    *    - less
    *    - equals
    * 
    */
    struct CompareTraits
    {
        /** full comparison operator (< 0, == 0, > 0) */
        using comparison_t = full_compare_t;

        /** bool implementation of std::less on top of comparison_t, may be used by stl */
        using less_t = details::CompareOnTopOfFullComparison<details::CmpOp::less, comparison_t >;

        using equals_t = details::CompareOnTopOfFullComparison<details::CmpOp::equals, comparison_t >;

        //template <class TSequence>
        //using hash_t = std::hash<typename full_compare_t<TSequence>::element_t>;

        //constexpr CompareTraits() noexcept = default;
        //constexpr CompareTraits(CompareTraits&&) noexcept = default;

        constexpr auto compare_factory() const noexcept
        {
            return comparison_t{};
        }

        constexpr auto less_factory() const noexcept
        {
            return less_t{};
        }

        constexpr auto equals_factory() const noexcept
        {
            return equals_t{};
        }

        //template <class TSequence>
        //constexpr auto hash_factory() const noexcept
        //{
        //    return hash_t<TSequence>{};
        //}
    };

    /**
    *  Allows customize CompareTraits by specifying custom three-way comparison function (c++20 operator `<=>`
    * but applicable for all c++1x versions) 
    *  \tparam F - functor implementing operator:\code
    * template <typename T, typename U>
    * int (T&& t, U&& u)
    * \endcode
    * 
    */
    template <class F>
    struct OverrideComparisonTraits
    {

        using comparison_t = F;

        /** bool implementation of std::less on top of comparison_t */
        using less_t = details::CompareOnTopOfFullComparison<
            details::CmpOp::less, comparison_t >;

        using equals_t = details::CompareOnTopOfFullComparison<
            details::CmpOp::equals, comparison_t >;

        constexpr OverrideComparisonTraits(F f) noexcept
            : _cmp(std::move(f))
        {
        }

        constexpr auto compare_factory() const noexcept
        {
            return _cmp;
        }

        constexpr auto less_factory() const noexcept
        {
            return less_t(_cmp);
        }

        constexpr auto equals_factory() const noexcept
        {
            return equals_t(_cmp);
        }

        F _cmp;
    };
} //ns:OP

#endif //_OP_FLUR_COMPARISON__H_