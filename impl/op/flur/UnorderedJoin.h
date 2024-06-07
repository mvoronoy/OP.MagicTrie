#pragma once
#ifndef _OP_FLUR_UNORDEREDJOIN__H_
#define _OP_FLUR_UNORDEREDJOIN__H_

#include <functional>
#include <memory>
#include <unordered_set>

#include <op/flur/typedefs.h>
#include <op/flur/Comparison.h>
#include <op/flur/Sequence.h>
#include <op/flur/Filter.h>
#include <op/flur/OrderedJoin.h>

/** Namespace for Fluent Ranges (flur) library. Compile-time composed ranges */
namespace OP::flur
{
    
    template <class TElement>
    struct HashSetFilterPredicate
    {
        using unordered_set_t = std::unordered_set<TElement>;

        HashSetFilterPredicate(HashSetFilterPredicate&&) = default;

        template <class TSequence>
        HashSetFilterPredicate(int, TSequence&& seq)
            : _filter(std::begin(seq), std::end(seq))
        {
        }

        template <class T>
        bool operator()(const T& check) const
        {
            return _filter.end() != _filter.find(check);
        }

    private:
        unordered_set_t _filter;
    };

    template <class Left, class Predicate>
    struct UnorderedJoin : public Filter<
        Predicate,
        Left,
        Sequence<details::sequence_element_type_t<Left>>
        >
    {
        using element_t = details::sequence_element_type_t<Left>;

        constexpr UnorderedJoin(Left&& left, Predicate predicate) noexcept
            : Filter{std::move(left), std::move(predicate)}
        {
        }
                
    };

   /**
    * \brief Provides a join of two sets when at least one doesn't support ordering.
    *
    * Use it very cautiously because it has a significant overhead due to copying one of 
    *   the unordered sequences to `std::unordered_set`.
    */
    template <class Right>
    struct UnorderedJoinFactory : FactoryBase
    {

        using hashed_element_t = std::decay_t<
            details::sequence_element_type_t<Right>>;

        template <class U>
        constexpr UnorderedJoinFactory(U&& u) noexcept
            : _right(std::forward<U>(u))
        {
        }

        template <class Left>
        constexpr decltype(auto) compound(Left&& left) const& noexcept
        {
            return UnorderedJoin{ std::forward<Left>(left),
                HashSetFilterPredicate<hashed_element_t>{0, _right.compound()} };
        }

        template <class Left>
        constexpr decltype(auto) compound(Left&& left) && noexcept
        {
            return UnorderedJoin{ std::move(left),
                HashSetFilterPredicate<hashed_element_t>(0, 
                    //`move` used as cast to T&& only
                    std::move(details::get_reference(_right)).compound()
                    ) };
        }

    private:
        Right _right;
    };

    template <class Right>
    class AdaptiveJoinFactory : FactoryBase
    {
    public:

        template <class U>
        constexpr AdaptiveJoinFactory(U&& right) noexcept
            : _right(std::forward<U>(right))
        {
        }

        template <class Left >
        auto compound(Left&& left) noexcept
        {
            using left_element_t = details::sequence_element_type_t<Left>;
            using right_element_t = details::sequence_element_type_t<Right>;

            using left_predicate_t = HashSetFilterPredicate<left_element_t>;
            using right_predicate_t = HashSetFilterPredicate<right_element_t>;

            using left_unordered_t = UnorderedJoin<Right, left_predicate_t>; //pay attention of cross-swap
            using righ_unordered_t = UnorderedJoin<Left, right_predicate_t>; //pay attention of cross-swap
        
            using all_ordered_t = ordered_joint_sequence_t<Left, Right>;

            using target_sequence_t = SequenceProxy<
                all_ordered_t, 
                left_unordered_t, 
                righ_unordered_t
                >;

            bool is_left_ordered = 
                details::get_reference(left.is_sequence_ordered());

            auto right_seq = 
                details::get_reference(_right).compound();
            if( details::get_reference(right_seq).is_sequence_ordered() )
            {
                if(is_left_ordered)
                { //all ordered
                    return target_sequence_t{ details::make_join(std::move(left), std::move(right_seq), full_compare_t{})};
                }
                //left unordered
                return target_sequence_t{ 
                    left_unordered_t{std::move(right_seq), left_predicate_t{std::move(left)}} };    
            }
            else //right unordered
            {
                return target_sequence_t{ 
                    right_unordered_t{std::move(left), right_predicate_t{std::move(right_seq)}} };    
                
            }
        }

    private:
        Right _right;
    };

} //ns: OP::flur

#endif //_OP_FLUR_UNORDEREDJOIN__H_
