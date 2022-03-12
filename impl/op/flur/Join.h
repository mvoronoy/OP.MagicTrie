#pragma once
#ifndef _OP_FLUR_JOIN__H_
#define _OP_FLUR_JOIN__H_

#include <functional>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Sequence.h>
#include <op/common/has_member_def.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    /**
    * Specialization of OrderedSequence that allows improve performance of join operation
    * by exposing method `next_lower_bound_of`
    */
    template <class T>
    struct OrderedRangeOptimizedJoin : OrderedSequence <T>
    {
        using base_t = OrderedSequence <T>;

        using base_t::base_t;

        virtual void next_lower_bound_of(const T& other) = 0;
    };
    namespace details{
        OP_DECLARE_CLASS_HAS_MEMBER(next_lower_bound_of);
    } //ns:details

    template <class T, class Left, class Right, class Comp, bool implement_exists_c = false>
    struct Join : public OrderedSequence<T>
    {
        using base_t = OrderedSequence<T>;
        using left_container_t = details::unpack_t<Left>;
        using right_container_t = details::unpack_t<Right>;
        using element_t = typename base_t::element_t;

        constexpr Join(Left&& left, Right&& right, Comp&& join_key_cmp) noexcept
            : _left(std::forward<Left>(left))
            , _right(std::forward<Right>(right))
            , _join_key_cmp(std::forward<Comp>(join_key_cmp))
        {
        }

        virtual void start()
        {
            auto& left = details::get_reference(_left);
            auto& right = details::get_reference(_right);

            left.start();
            if (left.in_range())
            {
                right.start();
            }
            seek();
        }
        virtual bool in_range() const
        {
            auto& left = details::get_reference(_left);
            auto& right = details::get_reference(_right);
            return left.in_range() && right.in_range();
        }
        virtual element_t current() const
        {
            return details::get_reference(_left).current();
        }
        virtual void next()
        {
            auto& left = details::get_reference(_left);
            left.next();
            if (_optimize_right_forward && left.in_range())
            {
                auto& right = details::get_reference(_right);
                opt_next(right, left.current());
            }
            seek();
        }


    private:
        template <class U>
        constexpr static bool is_join_optimized_c = 
            details::has_next_lower_bound_of<U>::value;

        template <class U>
        static void opt_next(U& src, const T& other_key)
        {
            if constexpr (is_join_optimized_c<U>)
                src.next_lower_bound_of(other_key);
            else
            {   //emulate lover_bound by sequential iteration
                do 
                {
                    src.next();
                } while (src.in_range() && _join_key_cmp(src.current(), other_key) < 0);
            }
        }

        void seek()
        {
            auto& left = details::get_reference(_left);
            auto& right = details::get_reference(_right);
             
            _optimize_right_forward = false;
            bool left_succeed = left.in_range();
            bool right_succeed = right.in_range();
            while (left_succeed && right_succeed)
            {
                auto diff = _join_key_cmp(left.current(), right.current());
                if (diff < 0) 
                {
                    opt_next(left, right.current());
                    left_succeed = left.in_range();
                }
                else 
                {
                    if (diff == 0) //eq 
                    {
                        if constexpr (!implement_exists_c) 
                            _optimize_right_forward = true;
                        return;
                    }
                    opt_next(right, left.current());
                    right_succeed = right.in_range();
                }
            }
        }

        Left _left;
        Right _right;

        Comp _join_key_cmp;
        bool _optimize_right_forward;
    };
    template <class Left, class Right, class Comp>
    auto make_join(Left&& left, Right&& right, Comp&& comp)
    {
        using src_conatiner_t = details::sequence_type_t<details::dereference_t<Left>>;
        using element_t = typename src_conatiner_t::element_t;
        return Join<element_t, Left, Right, Comp>(std::forward<Left>(left), std::forward<Right>(right), 
            std::forward<Comp>(comp));
    }

    template <class Cont>
    struct plain_less_t
    {
        using src_conatiner_t = details::sequence_type_t<details::dereference_t<Cont>>;
        using element_t = typename src_conatiner_t::element_t;
        int operator()(const element_t& left, const element_t& right) const
        {
            return left < right ? (-1) : (right < left ? 1 : 0);
        }
    };
    
    template <class Right, class Comp = plain_less_t<Right> >
    struct PartialJoinFactory : FactoryBase
    {
        constexpr PartialJoinFactory(Right&& right, Comp&& comp = Comp()) noexcept
            :  _right(std::forward<Right>(right))
            , _comp(std::forward<Comp>(comp))
        {
        }

        template <class Left >
        auto compound(Left&& left)const& noexcept
        {
            return make_join(std::forward<Left>(left), 
                _right.compound(), _comp);
        }
        template <class Left>
        constexpr auto compound(Left&& left) && noexcept
        {
            return make_join(std::forward<Left>(left).compound(), 
                std::move(_right).compound(), std::move(_comp));
        }
        Right _right;
        Comp _comp; 
    };

    template <class Left, class Right, class Comp = plain_less_t<Left> >
    struct JoinFactory : FactoryBase
    {
        constexpr JoinFactory(Left&& left, Right&& right, Comp&& comp = Comp()) noexcept
            : _left(std::forward<Left>(left))
            , _right(std::forward<Right>(right))
            , _comp(std::forward<Comp>(comp))
        {
        }

        constexpr auto compound() const& noexcept
        {
            return make_join(_left.compound(), _right.compound(), _comp);
        }

        constexpr auto compound() && noexcept
        {
            return make_join(std::move(_left).compound(), std::move(_right).compound(), std::move(_comp));
        }

        Left _left;
        Right _right;
        Comp _comp; 
    };

} //ns: OP::flur

#endif //_OP_FLUR_FILTER__H_
