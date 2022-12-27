#pragma once
#ifndef _OP_FLUR_COMPARISON__H_
#define _OP_FLUR_COMPARISON__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/common/has_member_def.h>

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
            equals,
            greater
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
                {
                    return _base(std::forward<T>(left), std::forward<U>(right)) < 0;
                }
                else if constexpr(operation == CmpOp::greater)
                {
                    return _base(std::forward<T>(left), std::forward<U>(right)) > 0;
                }
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

        /** bool implementation of std::greater on top of comparison_t, may be used by stl */
        using greater_t = details::CompareOnTopOfFullComparison<details::CmpOp::greater, comparison_t >;

        using equals_t = details::CompareOnTopOfFullComparison<details::CmpOp::equals, comparison_t >;

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

        constexpr auto greater_factory() const noexcept
        {
            return greater_t{};
        }

        constexpr auto equals_factory() const noexcept
        {
            return equals_t{};
        }

        /*
        template <class TElement>
        constexpr auto hash_factory() const noexcept
        {
            return hash_t<TElement>{};
        }
        */
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

        /** bool implementation of std::greater on top of comparison_t, may be used by stl */
        using greater_t = details::CompareOnTopOfFullComparison<details::CmpOp::greater, comparison_t >;

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

        constexpr auto greater_factory() const noexcept
        {
            return greater_t{ _cmp };
        }

        constexpr auto equals_factory() const noexcept
        {
            return equals_t(_cmp);
        }

        F _cmp;
    };
    
    template <class F>
    constexpr auto custom_compare(F f)
    {
        return OverrideComparisonTraits<F>(std::move(f));
    }

    /**
    *   customize CompareTraits by providing custom hash and comparison functions.
    */
    template <class F, class H>
    struct OverrideComparisonAndHashTraits
    {
        using comparison_t = F;
        using hash_t = H;

        /** bool implementation of std::less on top of comparison_t */
        using less_t = details::CompareOnTopOfFullComparison<
            details::CmpOp::less, comparison_t >;

        using equals_t = details::CompareOnTopOfFullComparison<
            details::CmpOp::equals, comparison_t >;

        constexpr OverrideComparisonAndHashTraits() = default;

        constexpr OverrideComparisonAndHashTraits(F f, H h) noexcept
            : _cmp(std::move(f))
            , _hash(std::move(h))
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

        constexpr auto hash_factory() const noexcept
        {
            return _hash;
        }

        F _cmp;
        H _hash;
    };
    
    /** Define compile-time check `has_hash_factory` if class has `hash_factory` method */
    OP_DECLARE_CLASS_HAS_MEMBER(hash_factory);

    template <class F, class H>
    constexpr auto custom_compare(F f, H h)
    {
        return OverrideComparisonAndHashTraits<F, H>(std::move(f), std::move(h));
    }

} //ns:OP

#endif //_OP_FLUR_COMPARISON__H_
