#ifndef _OP_RANGES_PREFIX_RANGE__H_
#define _OP_RANGES_PREFIX_RANGE__H_

#include <algorithm>
#include <memory>
#include <functional>
#include <type_traits>
#include <op/ranges/FunctionalRange.h>
#include <op/common/Utils.h>

#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif

namespace OP
{
    namespace ranges
    {

        template <class Iterator, bool is_ordered>
        struct PrefixRange;
        
        template <class SourceRange>
        struct FilteredRange;
            
        template <class SourceRange>
        struct OrderedFilteredRange;

        template <class SourceRange1, class SourceRange2>
        struct UnionAllRange;

        template <class SourceRange, class DeflateFunction >
        struct FlattenRange;

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

            /**
            *   Traits class declares how flatten range is rendered
            */
            //template <class DeflateFunction, class SourceIterator>
            //struct FlattenTraits
            //{
            //    using source_iterator_t = SourceIterator;

            //    using pre_applicator_result_t = typename std::result_of<DeflateFunction(const source_iterator_t&)>::type;
            //    /**Type of Range returned by DeflateFunction. Must be kind of OrderedRange
            //    */
            //    using applicator_result_t = typename std::conditional< //strip shared_ptr from pre_applicator_result_t if present
            //        OP::utils::is_generic<pre_applicator_result_t, std::shared_ptr>::value,
            //        typename pre_applicator_result_t::element_type,
            //        pre_applicator_result_t>::type; //type of 
            //    
            //};
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
            using filtered_range_t = std::conditional_t<
                is_ordered,
                OrderedFilteredRange<this_t>,
                FilteredRange<this_t>
            >;
            /**
            *   Produce new range that fitlers-out some record from this one
            */
            template <class UnaryPredicate>
            std::shared_ptr< filtered_range_t> filter(UnaryPredicate f) const
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
            /**
            *   Count number of entries in this range. Since range may be big use this operation responsibly because complexity is O(N).
            * If you need just check if range isn't empty use `!empty()` method
            */
            size_t count() const
            {
                size_t n = 0;
                for (auto i = begin(); in_range(i); next(i), ++n)
                {
                    /*nothing*/
                }
                return n;
            }
            /**Allows check if range is empty. Complexity is close to O(1) */
            bool empty() const
            {
                return in_range(begin());
            }
            /**
            *  Produce range consisting of the results of replacing each element of this range with the contents `deflate_function`.
            * \tparam DeflateFunction functor that accepts this range iterator and returns 
            *   ordered sequence
            * \param deflate_function converts single entry to another range
            * \return ordered range merged together from produced by `deflate_function`
            */
            template <class DeflateFunction>
            std::shared_ptr< FlattenRange<this_t, DeflateFunction > > flatten(DeflateFunction deflate_function) const;

        private:

            /** Case when filter supports orders*/
            template <class UnaryPredicate, class Q = std::enable_if_t<is_ordered, std::shared_ptr< filtered_range_t> > >
            Q filter_impl(UnaryPredicate && f, int) const
            {
                return std::make_shared<filtered_range_t>(
                    shared_from_this(), std::forward<UnaryPredicate>(f));
            }
            /** Case when filter over non-ordered sequence*/
            template <class UnaryPredicate>
            std::shared_ptr< filtered_range_t > filter_impl(UnaryPredicate && f, ...) const
            {
                return std::make_shared<filtered_range_t>(
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
       
        /**
        *   Implement range that filters source range by some `UnaryPredicate` criteria
        * \tparam UnaryPredicate boolean functor that accepts iterator as input argument and returns true 
        *       if position should appear in result.
        */
        template <class SourceRange, class Base >
        struct FilteredRangeBase : public Base
        {
            using iterator = typename SourceRange::iterator;

            template <class UnaryPredicate>
            FilteredRangeBase(std::shared_ptr<const SourceRange > source_range, UnaryPredicate && predicate)
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
            std::function<bool(const iterator&)> _predicate;
        };

        template <class SourceRange>
        struct FilteredRange : public FilteredRangeBase<SourceRange, PrefixRange<typename SourceRange::iterator, false>>
        {
            using base_t = FilteredRangeBase<SourceRange, PrefixRange<typename SourceRange::iterator, false>>;
            using base_t::base_t;
        };

        using shared_void_ptr = std::shared_ptr<void>;
        template<typename T>
        inline shared_void_ptr make_shared_void(T * ptr)
        {
            return std::shared_ptr<void>(ptr, [](void const * data) {
                T const * p = static_cast<T const*>(data);
                delete p;
            });
        }
        /**
        *   Helper class to add some payload for arbitrary iterator
        */
        template <class BaseIterator>
        struct IteratorWrap : public BaseIterator
        {
            IteratorWrap(const BaseIterator & base_iter, shared_void_ptr payload) noexcept
                : BaseIterator((base_iter))
                , _payload(std::forward<shared_void_ptr>(payload))
            {}
            auto key() -> decltype(key_discovery::key(std::declval<const BaseIterator&>())) const
            {
                return key_discovery::key(*this);
            }
            const shared_void_ptr& payload() const
            {
                return _payload;
            }
        private:
            shared_void_ptr _payload;
        };

        namespace key_discovery {

            template <class BaseIterator>
            inline auto key(const IteratorWrap<BaseIterator>& i) -> std::add_const_t< decltype(i.key()) >&
            {
                return i.key();
            }

            template <class BaseIterator>
            inline auto value(const IteratorWrap<BaseIterator>& i) 
                -> decltype( OP::ranges::key_discovery::value(static_cast<BaseIterator>(i) ))
            {
                return OP::ranges::key_discovery::value(static_cast<BaseIterator>(i) );
            }

            /**
            *   Specialization of discovery key from iterators for types that support dereferncing of "first". For example it 
            *   may be iterators produced by std::map
            */
            template <class I>
            auto key(const I& i) -> decltype(i->first)
            {
                return i->first;
            }
            template <class I>
            auto value(const I& i) -> decltype(i->second)
            {
                return i->second;
            }
            
            
            /**
            *   Specialization of discovery key from iterators for types that may return value by "key()" method. For example it
            *   may be iterators produced by other OrderedRange
            */
            template <class I>
            auto key(const I& i) ->decltype(i.key())
            {
                return i.key();
            }

            template <class I>
            auto value(const I& i) -> decltype(i.key(), *i )
            {
                return *i;
            }
        } //ns:key_discovery

        template <class Iterator>
        struct OrderedRange : public PrefixRange<Iterator, true>
        {
            using iterator = Iterator;
            
            using this_t = OrderedRange<iterator>;
            using key_type = decltype(OP::ranges::key_discovery::key(std::declval<const iterator&>()));
            using key_t = key_type;

            template <class OtherRange>
            using join_comparator_t =
                std::function<int(const key_type&, const typename OtherRange::key_type&)>;
            //typename join_range_t < OtherRange> ::iterator_comparator_t;

            using key_comparator_t =
                std::function<int(const key_type&, const key_type&)>;
            
            using join_iterator_t = IteratorWrap<iterator>;
            using join_range_t = OrderedRange< join_iterator_t > ;

            OrderedRange() = default;

            OrderedRange(key_comparator_t key_cmp)
                : _key_cmp(std::forward<key_comparator_t>(key_cmp))
            {}


            template <class OtherRange>
            inline std::shared_ptr< join_range_t > join(std::shared_ptr< OtherRange const > range,
                join_comparator_t<OtherRange> cmp) const;

            template <class OtherRange>
            inline std::shared_ptr< join_range_t > join(std::shared_ptr< OtherRange > range,
                join_comparator_t<OtherRange> cmp) const
            {
                std::shared_ptr< OtherRange const > cast{ range };
                return join(cast, cmp);
            }

            template <class OtherRange>
            inline std::shared_ptr< join_range_t > join(std::shared_ptr< OtherRange const > range) const
            {
                return this->join(range, [this](auto const& left, auto const& right)->int {
                    return key_comp()(left, right);
                });
            }
            
            const key_comparator_t& key_comp() const
            {
                return _key_cmp;
            }
            virtual iterator lower_bound(const key_type& key) const = 0;
        
        private:
            key_comparator_t _key_cmp;
        };


        template <class SourceRange>
        struct OrderedFilteredRange : 
            public FilteredRangeBase<SourceRange, OrderedRange< typename SourceRange::iterator > >
        {
            using ordered_base_t = OrderedRange< typename SourceRange::iterator >;
            using base_t = FilteredRangeBase<SourceRange, ordered_base_t>;
            using iterator = typename SourceRange::iterator;
            using base_t::base_t;

            iterator lower_bound(const typename ordered_base_t::key_type& key) const override
            {
                auto lower = static_cast<const base_t&>(*source_range()).lower_bound(key);
                seek(lower);
                return lower;
            } 
        };



}//ns:ranges
}//ns:OP


#include <op/ranges/UnionAllRange.h>
#include <op/ranges/FlattenRange.h>
#include <op/ranges/JoinRange.h>

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

        template<class Iterator, bool is_ordered>
        template <class DeflateFunction>
        std::shared_ptr< FlattenRange< PrefixRange<Iterator, is_ordered>, DeflateFunction > >  PrefixRange<Iterator, is_ordered>::flatten(DeflateFunction deflate_function) const
        {
            using traits_t = details::FlattenTraits<this_t, DeflateFunction>;
            static_assert(traits_t::applicator_result_t::is_ordered_c, "DeflateFunction function must produce ordered range, otherwise use unordered_flatten");
            std::shared_ptr<this_t> source = std::const_pointer_cast<this_t>(shared_from_this());
            auto ptr = new FlattenRange<this_t, DeflateFunction >(
                source,
                std::forward<DeflateFunction>(deflate_function),
                [](const auto& left, const auto& right) {
                auto const &inpl_l = OP::ranges::key_discovery::key(left);
                auto const &inpl_r = OP::ranges::key_discovery::key(right);
                return OP::ranges::str_lexico_comparator(
                    std::begin(inpl_l), std::end(inpl_l),
                    std::begin(inpl_r), std::end(inpl_r)
                );
            }
            );
            std::shared_ptr< FlattenRange< PrefixRange<Iterator, is_ordered>, DeflateFunction > > result{
                ptr
            };
            return result;
                //make_flatten_range(std::static_pointer_cast<const this_t>(shared_from_this()), std::forward<DeflateFunction>(deflate_function));
        }

        template <class Iterator>
        template <class OtherRange>
        inline std::shared_ptr< OrderedRange<IteratorWrap<Iterator>> > OrderedRange<Iterator>::join(std::shared_ptr<OtherRange const> range,
            join_comparator_t<OtherRange> cmp) const
        {
            std::shared_ptr<OrderedRange<Iterator> const> the_ptr( std::static_pointer_cast<OrderedRange<Iterator> const> (shared_from_this()) );
            using range_impl_t = JoinRange<this_t, OtherRange>;
            auto r = new range_impl_t(the_ptr, range, std::forward<join_comparator_t<OtherRange>>(cmp));
            return std::shared_ptr<join_range_t>(r);
        }
}//ns:ranges
}//ns:OP

#endif //_OP_RANGES_PREFIX_RANGE__H_
