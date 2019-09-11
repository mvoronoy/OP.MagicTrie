#ifndef _OP_RANGES_PREFIX_RANGE__H_
#define _OP_RANGES_PREFIX_RANGE__H_

#include <algorithm>
#include <type_traits>
#include <op/ranges/FunctionalRange.h>

#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif

namespace OP
{
    namespace ranges
    {

        template <class SourceRange, class UnaryPredicate>
        struct FilteredRange;

        template <class SourceRange, class UnaryPredicate>
        struct OrderedFilteredRange;

        template <class Iterator, class Base >
        struct OrderedRange;

        template <class SourceRange1, class SourceRange2>
        struct JoinRange;

        template <class SourceRange1, class SourceRange2>
        struct UnionAllRange;

        namespace details {

            template<class T,
                class D = void>
                struct DiscoverIteratorKeyType
            {   // default definition
                typedef D key_t;
            };

            template<class T>
            struct DiscoverIteratorKeyType<T, std::void_t< decltype(std::declval<typename T::reference>().first)> >
            {   // defined if iterator declared as std::map????::iterator and key-type detected as decltype of reference::first
                typedef typename std::remove_reference< decltype(std::declval<typename T::reference>().first) >::type key_t;
            };

            template<class T>
            struct DiscoverIteratorKeyType<T, std::void_t<typename T::key_type> >
            {   // defined if iterator contains explicit definition of key_type
                typedef typename T::key_type key_t;
            };

        } //ns:details
        namespace policy
        {
            /**Policy for PrefixRange::map that always evaluate new key for single iterator position. Result always uses
            copy by value (or if available rvalue optimization)*/
            struct no_cache {};
            /**Policy for PrefixRange::map that evaluate new key for single iterator position only once, all other calls
            return cached value. Result of iterator::key is const reference type*/
            struct cached {};

            namespace details
            {
                template <class Iter, class Func, class Policy>
                using effective_policy_t = typename std::conditional<
                    std::is_same<Policy, policy::cached> ::value,
                    FunctionResultCachedPolicy<Iter, Func>,
                    typename std::conditional<
                    std::is_same<Policy, policy::no_cache> ::value,
                    FunctionResultNoCachePolicy<Iter, Func>,
                    Policy
                >::type >
                    ::type
                    ;
            }
        } //ns:policy
        
        /**
        *
        */
        template <class Iterator, bool is_ordered>
        struct PrefixRange : std::enable_shared_from_this< PrefixRange<Iterator, is_ordered> >
        {
            static constexpr bool is_ordered_c = is_ordered;
            typedef Iterator iterator;
            typedef PrefixRange<Iterator, is_ordered> this_t;

            virtual ~PrefixRange() = default;

            virtual iterator begin() const = 0;
            /** Acts same as begin, but allows skip some records before
            * @param p predicate that should return true for the first matching and false to skip record
            */
            template <class UnaryPredicate>
            iterator first_that(UnaryPredicate && p) const
            {
                auto i = begin();
                for (; in_range(i) && !p(i); next(i))
                {/* empty */
                }
                return i;
            }

            virtual bool in_range(const iterator& check) const = 0;
            virtual void next(iterator& pos) const = 0;

            template <class UnaryFunction, class KeyPolicy>
            using functional_range_t = FunctionalRange<this_t, UnaryFunction, policy::details::effective_policy_t<iterator, UnaryFunction, KeyPolicy> >;
            /**
            *   \code std::result_of<UnaryFunction(typename iterator::value_type)>::type >::type
            */
            template <class KeyPolicy, class UnaryFunction>
            inline std::shared_ptr< functional_range_t<UnaryFunction, KeyPolicy > > map(UnaryFunction && f) const
            {
                return std::make_shared<functional_range_t<UnaryFunction, KeyPolicy >>(*this, std::forward<UnaryFunction>(f));
            }

            template <class UnaryFunction>
            inline std::shared_ptr< functional_range_t<UnaryFunction, policy::no_cache> > map(UnaryFunction && f) const
            {
                return map<policy::no_cache>(std::forward<UnaryFunction>(f));
            }
            /**
            *   Declare type for result of `filter` operation. Depending on suppot of this_t order. Fitlered range may or may not support order as well
            */
            template <class UnaryPredicate>
            using filtered_range_t = std::conditional_t<
                is_ordered,
                OrderedFilteredRange<this_t, UnaryPredicate>,
                FilteredRange<this_t, UnaryPredicate>
            >;
            /**
            *   Produce new range that fitlers-out some record from this one
            */
            template <class UnaryPredicate>
            std::shared_ptr< filtered_range_t<UnaryPredicate> > filter(UnaryPredicate && f) const
            {
                return filter_impl(std::forward<UnaryPredicate>(f), 0);
            }

            template <class OtherRange>
            inline std::shared_ptr< UnionAllRange<this_t, OtherRange> > merge_all(std::shared_ptr< OtherRange > & range,
                typename UnionAllRange<this_t, OtherRange>::iterator_comparator_t && cmp) const;
            /** 
            *   Apply operation `f` to the each item of this range
            *   \tparam Operation - functor that may have one of the following form:<ul>
            *       <li> `void f(const& iterator)` - use to iterate all items </li>
            *       <li> `bool f(const& iterator)` - use to iterate untill predicate returns true (take-while semantic)</li>
            *   </ul>
            *   \return number of items processed
            */
            template <class Operation>
            typename std::enable_if< /*Apply to `void Operation(const &iterator)` */
                !std::is_convertible<
                typename std::result_of<
                Operation(const iterator&) >::type,
                bool>::value, size_t
            >::type for_each(Operation && f) const
            {
                size_t n = 0;
                for (auto i = begin(); in_range(i); next(i), ++n)
                {
                    f(i);
                }
                return n;

            }
            template <class Operation>
            typename std::enable_if</*Apply to `bool Operation(const &iterator)` */
                std::is_convertible<typename std::result_of< Operation(const iterator&)>::type, bool>::value, size_t
            >::type for_each(Operation && f) const
            {
                size_t n = 0;
                for (auto i = begin(); in_range(i); next(i), ++n)
                {
                    if (!f(i)) {
                        return n;
                    }
                }
                return n;
            }
        private:

            /** Case when filter supports orders*/
            template <class UnaryPredicate, class Q = std::enable_if_t<is_ordered, std::shared_ptr< filtered_range_t<UnaryPredicate> > > >
            Q filter_impl(UnaryPredicate && f, int) const
            {
                using filter_it_t = filtered_range_t<UnaryPredicate>;
                return std::make_shared<filter_it_t>(
                    shared_from_this(), std::forward<UnaryPredicate>(f));
            }
            /** Case when filter over non-ordered sequence*/
            template <class UnaryPredicate>
            std::shared_ptr< filtered_range_t<UnaryPredicate> > filter_impl(UnaryPredicate && f, ...) const
            {
                using filter_it_t = filtered_range_t<UnaryPredicate>;
                return std::make_shared<filter_it_t>(
                    shared_from_this(), std::forward<UnaryPredicate>(f));
            }
        };

        /**@return \li 0 if left range equals to right;
        \li < 0 - if left range is lexicographical less than right range;
        \li > 0 - if left range is lexicographical greater than right range. */
        template <class I1, class I2>
        inline int str_lexico_comparator(I1& first_left, const I1& end_left,
            I2& first_right, const I2& end_right)
        {
            for (; first_left != end_left && first_right != end_right; ++first_left, ++first_right)
            {
                int r = static_cast<int>(static_cast<unsigned>(*first_left)) -
                    static_cast<int>(static_cast<unsigned>(*first_right));
                if (r != 0)
                    return r;
            }
            if (first_left != end_left)
            { //left is longer
                return 1;
            }
            return first_right == end_right ? 0 : -1;
        }
        template <class Iterator, class Base = PrefixRange<Iterator, true>>
        struct OrderedRange : public Base
        {
            using Base::Base;
            using this_t = OrderedRange<Iterator, Base>;
            using iterator = typename Base::iterator;
            virtual iterator lower_bound(const typename iterator::key_type& key) const = 0;


            template <class OtherRange>
            using join_range_t = JoinRange<this_t, OtherRange>;

            template <class OtherRange>
            using join_range_comparator_t = typename join_range_t < OtherRange> ::iterator_comparator_t;

            template <class OtherRange>
            inline std::shared_ptr< join_range_t< OtherRange> > join(std::shared_ptr< OtherRange > & range,
                join_range_comparator_t<OtherRange> && cmp) const
            {
                using join_t = JoinRange<PrefixRange<typename Iterator, is_ordered>, OtherRange>;
                return std::make_shared<join_t>(
                    shared_from_this(),
                    other,
                    std::forward<typename join_t::iterator_comparator_t>(cmp)
                    );
            }

        };

       
        /**
        *   Implement range that filters source range by some `UnaryPredicate` criteria
        * \tparam UnaryPredicate boolean functor that accepts iterator as input argument and returns true 
        *       if position should appear in result.
        */
        template <class SourceRange, class UnaryPredicate>
        struct FilteredRange : public SourceRange
        {
            using iterator = typename SourceRange::iterator;
            FilteredRange(std::shared_ptr<const SourceRange > source_range, UnaryPredicate && predicate)
                : _source_range(source_range)
                , _predicate(std::forward<UnaryPredicate>(predicate))
            {}
            iterator begin() const override
            {
                auto i = _source_range->begin();
                seek(i);
                return i;
            }
            bool in_range(const iterator& check) const override
            {
                return _source_range->in_range(check);
            }
            void next(iterator& pos) const override
            {
                _source_range->next(pos);
                seek(pos);
            }

        protected:
            std::shared_ptr<const SourceRange> source_range() const
            {
                return _source_range;
            }
            void seek(iterator& i)const
            {
                for (; _source_range->in_range(i); _source_range->next(i))
                {
                    if (_predicate(i))
                    {
                        return;
                    }
                }
            }
        private:

            std::shared_ptr<const SourceRange> _source_range;
            UnaryPredicate _predicate;
        };

        template <class SourceRange, class UnaryPredicate>
        struct OrderedFilteredRange : public OrderedRange< typename SourceRange::iterator, FilteredRange<SourceRange, UnaryPredicate> >
        {
            using base_t = OrderedRange< typename SourceRange::iterator, FilteredRange<SourceRange, UnaryPredicate> >;
            OrderedFilteredRange(std::shared_ptr<const SourceRange > source_range, UnaryPredicate && predicate)
                : base_t(source_range, std::forward<UnaryPredicate>(predicate))
            {}


            iterator lower_bound(const typename iterator::key_type& key) const override
            {
                auto lower = static_cast<const base_t&>( *source_range() ) .lower_bound(key);
                seek(lower);
                return lower;
            }
        };
/*!!!!!!!!!!!!!!!!!!!!!!!!!! to rempve !!!!!!!!!!!!!!!!!!!!!
        template<class Iterator, false>
        template<class UnaryPredicate>
        inline std::shared_ptr< FilteredRange<PrefixRange<Iterator>, UnaryPredicate> > PrefixRange<Iterator>::filter(UnaryPredicate && f) const
        {
            return std::make_shared<FilteredRange<this_t, UnaryPredicate>>(
                shared_from_this(), std::forward<UnaryPredicate>(f));
        }
*/
}//ns:ranges
}//ns:OP
#include <op/ranges/JoinRange.h>
#include <op/ranges/UnionAllRange.h>
namespace OP{
    namespace ranges {
        

        template<class Iterator, bool is_ordered>
        template<class OtherRange>
        inline std::shared_ptr<UnionAllRange<PrefixRange<Iterator, is_ordered>, OtherRange>> PrefixRange<Iterator, is_ordered>::merge_all(
            std::shared_ptr< OtherRange> & other, typename UnionAllRange<PrefixRange<Iterator, is_ordered>, OtherRange>::iterator_comparator_t && cmp) const
        {
            
            typedef UnionAllRange<PrefixRange<typename Iterator, is_ordered>, OtherRange> merge_all_t;
            return std::make_shared<merge_all_t>(
                shared_from_this(),
                other,
                std::forward<typename merge_all_t::iterator_comparator_t>(cmp)
            );
        }
}//ns:ranges
}//ns:OP
#endif //_OP_RANGES_PREFIX_RANGE__H_
