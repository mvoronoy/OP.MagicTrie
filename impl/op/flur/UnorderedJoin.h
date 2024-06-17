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
            : _filter{ }
        {
            auto& rseq = details::get_reference(seq);
            for (rseq.start(); rseq.in_range(); rseq.next())
                _filter.insert(rseq.current());
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
        using base_t = Filter<
            Predicate,
            Left,
            Sequence<element_t>
        >;


        template <class U>
        constexpr UnorderedJoin(U&& left, Predicate predicate) noexcept
            : base_t{std::forward<U>(left), std::move(predicate)}
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
            HashSetFilterPredicate<hashed_element_t> predicate{ 0, _right.compound() };
            return UnorderedJoin<Left, decltype(predicate)>{ 
                std::forward<Left>(left), std::move(predicate)
                };
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
    private:
        //need let know to _right what to use && or const&
        template <class RightSeq, class LeftSeq>
        static auto auto_join_impl(RightSeq&& right_seq, LeftSeq&& left)
        {
            using left_element_t = details::sequence_element_type_t<LeftSeq>;
            using right_element_t = details::sequence_element_type_t<RightSeq>;
            static_assert(std::is_same_v< left_element_t, right_element_t>, "must be same item to join");

            using left_predicate_t = HashSetFilterPredicate< std::decay_t<left_element_t>>;
            using right_predicate_t = HashSetFilterPredicate< std::decay_t<right_element_t>>;

            using left_unordered_t = UnorderedJoin<RightSeq, left_predicate_t>; //pay attention of cross-swap
            using right_unordered_t = UnorderedJoin<LeftSeq, right_predicate_t>; //pay attention of cross-swap


            using all_ordered_t = details::ordered_joint_sequence_t<LeftSeq, RightSeq>;

            /*
            using dummy_t = NullSequenceFactory< right_element_t>;
            using target_t = SequenceProxy< details::unpack_t<dummy_t> >;
            std::cout
                << "1)" << typeid(all_ordered_t).name() << "\n"
                << "2a) elem = " << typeid(left_element_t).name() << "\n"
                << "2b) r-seq = " << typeid(right_seq_t).name() << "\n"
                << "2)" << typeid(left_unordered_t).name() << "\n"
                << "3)" << typeid(right_unordered_t).name() << "\n"
                ;
            return target_t{ dummy_t{}.compound() };
             */
            using target_sequence_t =
                std::conditional_t<
                //occasionally `left_unordered_t` may be the same as `right_unordered_t`
                // that means `std::variant` can fail to distinguish them, so reduce 
                // number of proxied types
                std::is_same_v<left_unordered_t, right_unordered_t>,
                SequenceProxy<all_ordered_t, left_unordered_t>,
                SequenceProxy<all_ordered_t, left_unordered_t, right_unordered_t>
                >;

            bool is_left_ordered =
                details::get_reference(left).is_sequence_ordered();

            if (details::get_reference(right_seq).is_sequence_ordered())
            {
                if (is_left_ordered)
                { //all ordered
                    return target_sequence_t{
                        details::make_join(std::move(left), std::move(right_seq), full_compare_t{})
                    };
                }
                //left unordered
                return target_sequence_t{
                    left_unordered_t{std::move(right_seq), left_predicate_t{0, std::move(left)}}
                };
            }
            else //right unordered
            {
                return target_sequence_t{
                    right_unordered_t{std::move(left), right_predicate_t{0, std::move(right_seq)}}
                };

            }
        }
        Right _right;
    public:

        template <class U>
        constexpr AdaptiveJoinFactory(U&& right) noexcept
            : _right(std::forward<U>(right))
        {
        }

        template <class Left>
        auto compound(Left&& left) const& noexcept
        {
            return auto_join_impl(
                details::get_reference(_right).compound(), std::move(left));
        }

        template <class Left>
        auto compound(Left&& left) && noexcept
        {
            return auto_join_impl(
                //tell factory use move semantic
                std::move(details::get_reference(_right)).compound(), std::move(left));
        }
    };

} //ns: OP::flur

#endif //_OP_FLUR_UNORDEREDJOIN__H_
