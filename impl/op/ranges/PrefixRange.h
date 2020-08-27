#ifndef _OP_RANGES_PREFIX_RANGE__H_
#define _OP_RANGES_PREFIX_RANGE__H_

#include <algorithm>
#include <memory>
#include <functional>
#include <type_traits>
#include <op/common/Utils.h>
#include <iterator>

#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif

namespace OP
{
    namespace ranges
    {
        template <class K, class V>
        struct RangeBase;

        template <class K, class V>
        struct RangeIterator
        {
            using iterator_category = std::forward_iterator_tag;
            using key_t = K;
            using value_type = V;
            using value_t = V;

            struct RangeIteratorImpl
            {
                virtual ~RangeIteratorImpl() = default;
                virtual const K& key() const = 0;
                virtual const V& value() const = 0;
                virtual std::unique_ptr<RangeIteratorImpl> clone() const = 0;
            };
            using impl_t = RangeIteratorImpl;
            using impl_ptr = std::unique_ptr<impl_t>;
            using base_t = RangeIterator<K, V>;

            RangeIterator(std::shared_ptr< RangeBase<K, V> const> owner,
                impl_ptr && impl) noexcept
                : _owner(std::move(owner))
                , _impl(std::move(impl))
                {}

            RangeIterator(const RangeIterator& other) noexcept
                : _owner(other._owner)
                , _impl(other._impl ? other._impl->clone() : impl_ptr{nullptr})
                {}

            RangeIterator& operator = (const base_t& other) noexcept
            {
                _owner = other._owner;
                _impl = std::move(other._impl ? other._impl->clone() : impl_ptr{nullptr});
                return *this;
            }
            std::pair<K, V> operator * () const
            {
                return std::pair<K, V>{key(), value()};
            }

            const K& key() const
            {
                return _impl->key();
            }
            const V& value() const
            {
                return _impl->value();
            }
            template <class U = impl_t>
            U& impl()
            {
                return dynamic_cast<U&>(*_impl);
            }
            template <class U = impl_t>
            const U& impl() const
            {
                return dynamic_cast<const U&>(*_impl);
            }
            
            /**This operator is very primitive and allows check only equality to `end` to support STL operation
            * @return true only when comparing this with `end()` at the end of range
            */
            bool operator == (const base_t& other) const noexcept
            {
                if( !_owner )
                    return !other._owner;
                if( !_owner->in_range(*this) )
                    return !other._owner || !other._owner->in_range(other);
                return false;
            }
            bool operator != (const base_t& other) const noexcept
            {
                return !operator == (other);
            }
            bool operator == (std::nullptr_t) const noexcept
            {
                return !_owner||!_impl;
            }
            bool operator != (std::nullptr_t) const noexcept
            {
                return !operator == (nullptr);
            }
            bool operator !() const noexcept
            {
                return operator == (nullptr);
            }
            base_t& operator ++ ()
            {
                _owner->next(*this);
                return *this;
            }
            base_t operator ++ (int)
            {
                base_t result(*this);
                _owner->next(*this);
                return result;
            }
        private:
            impl_ptr _impl;
            std::shared_ptr< RangeBase<K, V> const > _owner;
        };

        template <class K, class V>
        struct OrderedRange;

        template <class SourceRange >
        struct FilteredRange;
        
        template <class SourceRange>
        struct OrderedFilteredRange;

        template <class SourceRange>
        struct UnionAllRange;

        template <class SourceRange, class DeflateFunction >
        struct FlattenRange;

        namespace flatten_details
        {
            template <class DF, class Iterator>
            using DeflateResultType = typename std::result_of<DF(const Iterator&)>::type::element_type::key_t;

            template <class DF, class Iterator>
            using MapResultType = typename std::result_of<DF(const Iterator&)>::type;
        }

        /** Unordered range */
        template <class K, class V>
        struct RangeBase : public std::enable_shared_from_this< RangeBase<K, V> >
        {
            static constexpr bool is_ordered_c = false;
            using iterator = RangeIterator<K, V>;
            using key_t = K;
            using value_t = V;

            using range_t = RangeBase<K, V>;
            using range_ptr = std::shared_ptr<range_t const>;

            virtual ~RangeBase() = default;
            virtual iterator begin() const = 0;
            virtual bool in_range(const iterator & check) const = 0;
            virtual void next(iterator & i) const = 0;

            iterator end() const
            {
                return iterator{nullptr, nullptr};
            }

            //range_ptr merge_all(range_ptr other) const;

            template<class ...Rs>
            range_ptr merge_all(Rs... other) const;

            /** 
            * Convert one range to another with same number of items by applying functor to iterator
            * @return new un-ordered range
            */
            template <class Converter>
            std::shared_ptr< RangeBase<flatten_details::MapResultType<Converter, iterator>, V> const> map( Converter f ) const;
            
            template <class F>
            range_ptr filter(F f) const;
            /** 
            *   Apply operation `f` to the each item of this range
            *   \tparam Operation - functor in the following form `void f(const& iterator)` 
            *   \return number of items processed
            *   \see `awhile` to conditionally stop iteration and `first_that` to find exact item
            */
            template <class Operation>
            size_t for_each(Operation && f) const
            {
                size_t n = 0;
                for (auto i = begin(); in_range(i); next(i), ++n)
                {                                                                                                                 
                    f(i);
                }
                return n;
            }

            /** 
            *   Apply operation `f` to the items of this range until `f` returns true
            *   \tparam Operation - functor in the following form `bool f(const& iterator)` 
            *   \return number of items processed
            */
            template <class Predicate>
            size_t awhile(Predicate && f) const
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
            /** Acts same as begin, but allows skip some records before
            * @param p predicate that should return true for the first matching and false to skip a record
            */
            iterator first_that(bool (*p)(const iterator&) ) const
            {
                auto i = begin();
                for (; in_range(i) && !p(i); next(i))
                {/* empty */
                }
                return i;
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
            size_t size() const
            { return count(); }
            /**Allows check if range is empty. Complexity is close to O(1) */
            bool empty() const
            {
                return !in_range(begin());
            }

            /**
            *  Produce range consisting of the results of replacing each element of this range with the contents `deflate_function`.
            * \tparam DeflateFunction functor that accepts this range iterator and returns 
            *   ordered sequence
            * \param deflate_function converts single entry to another range
            * \return ordered range merged together from produced by `deflate_function`
            */
            template <class DeflateFunction>
            std::shared_ptr< OrderedRange< flatten_details::DeflateResultType<DeflateFunction, iterator>, V > const > flatten(DeflateFunction deflate_function) const;
        };
        
        namespace details 
        {
            /** void structure for IdentityIteratorRange payload */
            struct DummyIdentityRangePayload{};
        }//ns::details
        /** This abstract range used as a base for implementation of other ranges that can reuse origin iterator. 
        *  IdentityIteratorRange is useful when derived class needs only filter and/or reposition result iterator. 
        * \tparam Base - base range that is also wrapped 
        * \tparam Payload - state keeping structure used between pair calls `on_before_reposition` and `on_after_reposition` 
        */
        template <class Base, class Payload = details::DummyIdentityRangePayload>
        struct IdentityIteratorRange : public Base
        {
            using base_t = Base;
            using iterator = typename Base::iterator;
            using key_t = typename Base::key_t;
            using state_payload_t = Payload;

            using base_t::base_t;

            template <class ... Tx>
            IdentityIteratorRange(std::shared_ptr<base_t const> wrap, Tx&& ... args)
                : base_t(std::forward<Tx>(args)...)
                , _wrap(std::move(wrap))
                {}

            iterator begin() const override
            {
                state_payload_t state;
                auto i = _wrap->begin();
                on_after_reposition(i, state);
                return std::move(i);    
            }
            bool in_range(const iterator & check) const override
            {
                return _wrap->in_range(check);
            }
            void next(iterator & i) const override
            {
                state_payload_t state;
                on_before_reposition(i, state);
                _wrap->next(i);
                on_after_reposition(i, state);
            }
            iterator lower_bound(const key_t&k) const
            {
                state_payload_t state;
                auto l = _wrap->lower_bound(k);
                on_after_reposition(l, state);
                return std::move(l);
            }

        protected:
            /** Called right before re-position iterator on next value. Note this method is not called on `begin` 
            * or `lower_bound` - when iteration begins
            */
            virtual void on_before_reposition(iterator& current, state_payload_t& state) const = 0;
            /** Called after iterator position was altered (may be out of range). 
            * @param current new state of iterator;
            * @param state - inside `begin` and `lower_bound` this method invoked with
            *               empty state. On `next` this argument contains result from `on_before_reposition`
            */
            virtual void on_after_reposition(iterator& current, state_payload_t& state) const = 0;
            
            
            const std::shared_ptr<base_t const>& wrap() const
            { return _wrap; }
            std::shared_ptr<base_t const>& wrap() 
            { return _wrap; }
        private:
            std::shared_ptr<base_t const> _wrap;

        };

        /** Implement range that allows peek current processed value without altering iteration or range-pipeline 
        * Common use-case:
        * \code
        *   some_range
        *   // peek a copy
        *   .peek([&current](auto& i){ current = i.key(); })
        *   // apply peek and map
        *   .map([&current](auto& i){ return i.key() + current; })
        * \endcode
        */
        template <class Base>
        struct PeekRange : public IdentityIteratorRange<Base>
        {
            using base_t = Base;
            using iterator = typename base_t::iterator;

            using peek_f = std::function<void(iterator&)>;

            PeekRange(std::shared_ptr<base_t const> wrap, peek_f && f)
                : base_t(std::move(wrap))
                , _applicator(std::move(f))
                {}
         private:
            using state_payload_t = typename base_t::state_payload_t;
            void on_before_reposition(iterator&, state_payload_t& ) const override
            {/*do nothing*/}
            void on_after_reposition(iterator& current, state_payload_t& ) const override
            {
                if( in_range(current) )
                    _applicator(current);
            }

            std::shared_ptr<base_t const> _wrap;
            peek_f _applicator;
        };
        namespace details
        {
            /** Keep key value for distinct operator */
            template <class Key>
            struct DistinctIdentityRangePayload
            {
                bool _has_value = false;
                Key _key;
            };
        }//ns::details

        /** Implement `distinct` operation for ordered ranges */
        template <class Base>
        struct DistinctRange : IdentityIteratorRange<Base, details::DistinctIdentityRangePayload<typename Base::key_t> >
        {
            using base_t = Base;
            using super_t = IdentityIteratorRange<base_t, details::DistinctIdentityRangePayload<typename Base::key_t> >;
            using iterator = typename base_t::iterator;

            using super_t::super_t;//reuse all constructors

         private:
            using state_payload_t = typename super_t::state_payload_t;
            void on_before_reposition(iterator& current, state_payload_t& payload) const override
            {
                if( in_range(current) )
                {
                    payload._has_value = true;
                    payload._key = current.key();
                }
            }
            void on_after_reposition(iterator& current, state_payload_t& payload) const override
            {
                if( payload._has_value )
                {
                    for(auto const* wp = wrap().get(); wp->in_range(current); wp->next(current))
                    {
                        if( wp->key_comp()(payload._key, current.key()) != 0 )
                        {
                            return;
                        }
                    }
                }
            }
        };

        /** Ordered range abstraction */
        template <class K, class V>
        struct OrderedRange : public RangeBase<K, V>
        {
            static constexpr bool is_ordered_c = true;
            using ordered_range_t = OrderedRange<K, V>;
            using ordered_range_ptr = std::shared_ptr<ordered_range_t const>;
            using key_comparator_t = std::function<int(const K&, const K&)>;

            virtual iterator lower_bound(const K&) const = 0;

            virtual ordered_range_ptr distinct() const
            {
                auto the_ptr(std::static_pointer_cast<ordered_range_t const> (shared_from_this()));
                using distinct_range_t = DistinctRange<ordered_range_t>;

                return ordered_range_ptr(new distinct_range_t(the_ptr, key_comp()));
                
            }

            /** Perform conjunction of ranges. 
            * \see JoinRange for implementation details
            */
            virtual ordered_range_ptr join(ordered_range_ptr range) const
            {
                //std::shared_ptr<OrderedRange<Iterator> const> the_ptr(std::static_pointer_cast<OrderedRange<Iterator> const> (shared_from_this()));
                auto the_ptr(std::static_pointer_cast<ordered_range_t const> (shared_from_this()));
                using range_impl_t = JoinRange<ordered_range_t, ordered_range_t>;
                return ordered_range_ptr(new range_impl_t(std::move(the_ptr), std::move(range), key_comp()));
            }

            /** FIlter that keep ordering of this range hide the same function form parent RangeBase */
            template <class UnaryPredicate>
            ordered_range_ptr filter(UnaryPredicate f) const;

            const key_comparator_t& key_comp() const
            {
                return _key_cmp;
            }
            template <class F>
            ordered_range_ptr peek(F f) const
            {
                auto the_ptr(std::static_pointer_cast<ordered_range_t const> (shared_from_this()));
                return ordered_range_ptr(new PeekRange<ordered_range_t>(the_ptr, std::move(f)));
            }

        protected:
            OrderedRange(key_comparator_t key_cmp)
                : _key_cmp(std::move(key_cmp))
            {}

        private:
            key_comparator_t _key_cmp;
        };

        /** 
        * Specialization of OrderedRange that allows improve performance of join operation
        * by exposing method `next_lower_bound_of`
        */
        template <class K, class V>
        struct OrderedRangeOptimizedJoin : public OrderedRange<K, V>
        {
            using base_t = OrderedRange<K, V>;

            using base_t::base_t;

            virtual void next_lower_bound_of(iterator& i, const K& k) const = 0;
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
        */
        template <class SourceRange>
        struct FilteredRange : public IdentityIteratorRange<SourceRange>
        {
            using base_t = SourceRange;
            using super_t = IdentityIteratorRange<base_t>;
            using iterator = typename base_t::iterator;
            /** 
            * \tparam UnaryPredicate predicate that accepts iterator as input argument and returns true 
            * if position should appear in result.
            */
            template <class UnaryPredicate, class ... Tx>
            FilteredRange(std::shared_ptr<const SourceRange > source_range, UnaryPredicate&& predicate, Tx&& ... args)
                : super_t(std::move(source_range), std::forward<Tx>(args)...)
                , _predicate(std::move(predicate))
            {}

        private:
            using state_payload_t = typename super_t::state_payload_t;
            void on_before_reposition(iterator&, state_payload_t& ) const override
            {
            }
            void on_after_reposition(iterator& current, state_payload_t& payload) const override
            {
                for(base_t const* wp = wrap().get(); wp->in_range(current); wp->next(current))
                {
                    if (_predicate(current))
                    {
                        return;
                    }
                }
            }
            std::function<bool(iterator&)> _predicate;
        };

        /** Specialization of FilteredRange to support OrderedRange 
        * \tparam SourceRange - any inherited from OrderedRange
        */
        template <class SourceRange>
        struct OrderedFilteredRange : public FilteredRange< SourceRange >
        {
            using base_t = FilteredRange<SourceRange>;
            using base_t::base_t;
        };
        /*
        template <class SourceRange>
        struct OrderedFilteredRange : 
            public FilteredRange< SourceRange >
        {
            using base_t = FilteredRange<SourceRange>;

            using base_t::base_t;

            base_iter_t lower_bound(const typename base_t::key_t& key) const override
            {
                auto lower = static_cast<const base_t&>(*source_range()).lower_bound(key);
                seek(lower);
                return lower;
            } 
        };*/

}//ns:ranges
}//ns:OP
namespace std
{
    template<class K, class V> struct iterator_traits< OP::ranges::RangeIterator<K, V> >
    {
        typedef std::forward_iterator_tag iterator_category;
        typedef V value_type;
        typedef int difference_type;
    };
}
#include <op/ranges/FlattenRange.h>
#include <op/ranges/FunctionalRange.h>
#include <op/ranges/JoinRange.h>
#include <op/ranges/UnionAllRange.h>

namespace OP{
    namespace ranges {
        

        template<class K, class V>
        template<class ...Rs>
        std::shared_ptr<RangeBase<K, V> const> RangeBase<K, V>::merge_all(Rs... other) const
        {
            using merge_all_t = UnionAllRange<range_t>;
            return std::shared_ptr<RangeBase<K, V> const>(new merge_all_t(
                {
                    std::const_pointer_cast<range_t const>(shared_from_this()),
                    std::forward<Rs>(other)...
                }
            ));
        }

        template<class K, class V>
        template <class Converter>
        std::shared_ptr< RangeBase<flatten_details::MapResultType<Converter, typename RangeBase<K, V>::iterator>, V> const> RangeBase<K, V>::map(Converter f) const
        {
            using map_range_t = FunctionalRange<range_t, Converter>;
            using result_key_t = flatten_details::MapResultType<Converter, typename RangeBase<K, V>::iterator>;

            map_range_t *ptr = new map_range_t(
                std::const_pointer_cast<range_t const>(shared_from_this()), std::move(f));
            
            return std::shared_ptr< RangeBase<result_key_t, V> const>(ptr);
        }
        template<class K, class V>
        template <class UnaryPredicate>
        std::shared_ptr< RangeBase<K, V> const> RangeBase<K, V>::filter(UnaryPredicate f) const
        {
            using filter_t = FilteredRange<range_t>;
            return range_ptr ( new filter_t(
                shared_from_this(), std::forward<UnaryPredicate>(f)) );
        }

        template<class K, class V>
        template <class UnaryPredicate>
        std::shared_ptr< OrderedRange<K, V> const> OrderedRange<K, V>::filter(UnaryPredicate f) const
        {
            using filter_t = OrderedFilteredRange< OrderedRange<K, V> >;
            return ordered_range_ptr(new filter_t(
                std::static_pointer_cast<ordered_range_t const>(shared_from_this()), std::forward<UnaryPredicate>(f), this->_key_cmp));
        }

        template<class K, class V>
        template <class DeflateFunction>
        std::shared_ptr< OrderedRange< flatten_details::DeflateResultType<DeflateFunction, typename RangeBase<K, V>::iterator>, V > const >
            RangeBase<K, V>::flatten(DeflateFunction deflate_function) const
        {
            using traits_t = details::FlattenTraits<range_t, DeflateFunction>;
            static_assert(traits_t::applicator_result_t::is_ordered_c, "DeflateFunction function must produce ordered range, otherwise use unordered_flatten");
            //std::shared_ptr<range_t> source = std::const_pointer_cast<range_t>(shared_from_this());
            auto ptr = new FlattenRange<range_t, DeflateFunction >(
                shared_from_this(),
                std::forward<DeflateFunction>(deflate_function),
                [](const auto& left, const auto& right) {
                return OP::ranges::str_lexico_comparator(
                    std::begin(left), std::end(left),
                    std::begin(right), std::end(right)
                );
            }
            );
            std::shared_ptr< OrderedRange< flatten_details::DeflateResultType<DeflateFunction, typename RangeBase<K, V>::iterator>, V > const > result{
                ptr
            };
            return result;
                //make_flatten_range(std::static_pointer_cast<const this_t>(shared_from_this()), std::forward<DeflateFunction>(deflate_function));
        }
}//ns:ranges
}//ns:OP

#endif //_OP_RANGES_PREFIX_RANGE__H_
