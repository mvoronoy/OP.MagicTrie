#pragma once
#ifndef _OP_FLUR_ORDEREDJOIN__H_
#define _OP_FLUR_ORDEREDJOIN__H_

#include <functional>
#include <cassert>
#include <memory>
#include <optional>

#include <op/flur/typedefs.h>
#include <op/flur/Comparison.h>
#include <op/flur/Sequence.h>
#include <op/common/has_member_def.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    /**
    * Specialization of OrderedSequence that allows improve performance of join operation
    * by exposing method `lower_bound`
    */
    template <class T>
    struct OrderedSequenceOptimizedJoin : OrderedSequence <T>
    {
        using base_t = OrderedSequence <T>;

        using base_t::base_t;

        virtual void lower_bound(const T& other) = 0;
    };
    namespace details{
        OP_DECLARE_CLASS_HAS_MEMBER(lower_bound);
    } //ns:details

    template <class T, class Left, class Right, class Comp, bool implement_exists_c = false>
    struct OrderedJoin : public OrderedSequence<T>
    {
        using base_t = OrderedSequence<T>;
        using left_container_t = details::unpack_t<Left>;
        using right_container_t = details::unpack_t<Right>;
        using element_t = typename base_t::element_t;

        constexpr OrderedJoin(Left&& left, Right&& right, Comp&& join_key_cmp) noexcept
            : _left(std::move(left))
            , _right(std::move(right))
            , _join_key_cmp(join_key_cmp)
            , _optimize_right_forward(false)
        {
            //"Join algorithm assumes ordering"
            assert(details::get_reference(_left).is_sequence_ordered() 
                && details::get_reference(_right).is_sequence_ordered());
        }

        virtual void start() override
        {
            auto& left = details::get_reference(_left);
            auto& right = details::get_reference(_right);

            left.start();
            if (left.in_range())
            {
                right.start();
                seek();
            }
        }
        
        virtual bool in_range() const override
        {
            auto& left = details::get_reference(_left);
            auto& right = details::get_reference(_right);
            return left.in_range() && right.in_range();
        }
        
        virtual element_t current() const override
        {
            return details::get_reference(_left).current();
        }

        virtual void next() override
        {
            auto& left = details::get_reference(_left);
            left.next();
            if (_optimize_right_forward && left.in_range())
            {
                auto& right = details::get_reference(_right);
                opt_next<false>(right, left.current());
            }
            seek();
        }

    private:
        template <class U, class V>
        static inline constexpr bool is_join_op_f()
        { //unfortunately optimization works with default key-compare only
            if constexpr(details::has_lower_bound<U>::value)
            {
                return std::is_invocable_v<decltype(&U::lower_bound), U&, const V&>;
            }
            else
                return false;
        
        }
        
        template <bool direction_c, class U, class V>
        void opt_next(U& src, const V& other_key) const
        {
            if constexpr (is_join_op_f<U, V>())
            {
                if( compare<direction_c>(src.current(), other_key) < 0 )
                { //allow optimization on strict key mismatch
                    src.lower_bound(other_key);
                    return;
                }
                //else proceed with default impl
            }
            //emulate lover_bound by sequential approximation to the target key
            do 
            {
                src.next();
            } while (src.in_range() && compare<direction_c>(src.current(), other_key) < 0);
            
        }

        template <bool direction_c, class U, class V>
        int compare(const U& left, const V& right) const
        {
            if constexpr (direction_c)
                return _join_key_cmp(left, right);
            else
                return -1* _join_key_cmp(right, left);
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
                    opt_next<true>(left, right.current());
                    left_succeed = left.in_range();
                }
                else 
                {
                    if (diff == 0) //eq 
                    {
                        if constexpr (implement_exists_c) 
                            _optimize_right_forward = true;
                        return;
                    }
                    opt_next<false>(right, left.current());
                    right_succeed = right.in_range();
                }
            }
        }

        Left _left;
        Right _right;

        Comp _join_key_cmp;
        bool _optimize_right_forward;
    };

    namespace details
    {
        template <class Left, class Right, class Comp>
        static constexpr auto make_join(Left&& left, Right&& right, Comp&& comp) noexcept
        {
            using left_t = std::decay_t<Left>;
            using right_t = std::decay_t<Right>;
            using element_t = details::sequence_element_type_t<left_t>;

            return OrderedJoin<element_t, left_t, right_t, Comp>(
                std::forward<Left>(left), 
                std::move(right),
                std::forward<Comp>(comp));
        }

        template <class Left, class Right, class Comp = full_compare_t>
        using ordered_joint_sequence_t = decltype(
            make_join(std::declval<Left>(), std::declval<Right>(), std::declval<Comp>())); 
    } //ns:details

    template <class Right, class Comp = full_compare_t >
    class OrderedJoinFactory : FactoryBase
    {
    public:

        template <class U>
        explicit constexpr OrderedJoinFactory(U&& right, Comp comp = {}) noexcept
            : _right(std::forward<U>(right))
            , _comp(std::move(comp))
        {
        }

        template <class Left>
        auto compound(Left&& left) const& noexcept
        {
            return details::make_join(
                std::move(left), 
                details::get_reference(_right).compound(), 
                _comp);
        }

        template <class Left>
        constexpr auto compound(Left&& left) && noexcept
        {
            //`std::move(deref(...))` used only as cast to T&& 
            return details::make_join(
                std::move(left), 
                std::move(details::get_reference(_right)).compound(),
                std::move(_comp));
        }

    private:

        Right _right;
        Comp _comp; 
    };

} //ns: OP::flur

#endif //_OP_FLUR_ORDEREDJOIN__H_
