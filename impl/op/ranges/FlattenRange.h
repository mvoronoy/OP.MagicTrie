#ifndef _OP_RANGES_FLATTEN_RANGE__H_
#define _OP_RANGES_FLATTEN_RANGE__H_
#pragma once

#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <op/common/Utils.h>
#include <queue>

namespace OP
{
    namespace ranges
    {
        namespace storage_policy
        {
            template <class Range>
            struct StoragePolicy : public Range::iterator::RangeIteratorImpl
            {
                using range_ptr = std::shared_ptr<const Range> ;
                using flat_item_t = std::pair<range_ptr, typename Range::iterator> ;
                using flat_item_ptr = std::shared_ptr<flat_item_t>;
                using key_t = typename Range::key_t;
                using value_t = typename Range::value_t;

                virtual ~StoragePolicy() = default;
                virtual void push(flat_item_ptr& ) = 0;
                virtual flat_item_ptr pop() = 0;
                virtual flat_item_ptr smallest() const = 0;
                virtual bool is_empty() const = 0;


                const key_t& key() const override
                {
                    return smallest()->second.key();
                }
                const value_t& value() const override
                {
                    return smallest()->second.value();
                }
            };
            template <class Range, class KeyComparator>
            struct PriorityQueueSetStorage : public StoragePolicy<Range>
            {
                PriorityQueueSetStorage(const KeyComparator& comparator) noexcept
                    : _item_set(flat_less(comparator))
                {}
                PriorityQueueSetStorage(const PriorityQueueSetStorage& other) noexcept
                    : _item_set(other._item_set)
                {}
                void push(flat_item_ptr& item) override
                {
                    _item_set.emplace(item);
                }
                virtual flat_item_ptr pop() override
                {
                    auto res = _item_set.top();
                    _item_set.pop();
                    return res;
                }
                flat_item_ptr smallest() const override
                {
                    return _item_set.top();
                }
                bool is_empty() const override
                {
                    return _item_set.empty();
                }
                std::unique_ptr<typename Range::iterator::RangeIteratorImpl> clone() const override
                {
                    return std::unique_ptr<RangeIteratorImpl>(new PriorityQueueSetStorage(*this));
                }
            private:
                struct flat_less
                {
                    flat_less() = delete;
                    flat_less(const KeyComparator& comparator):
                        _comparator(comparator)
                    {}
                    bool operator ()(const flat_item_ptr& left, const flat_item_ptr& right) const
                    {
                        assert(left->first->in_range(left->second) &&
                            right->first->in_range(right->second));
                        auto test = _comparator(left->second.key(), right->second.key());
                        return test > 0; // >0 implments 'greater' - to invert default priority_queue behaviour
                    }
                    const KeyComparator& _comparator;
                };
                using flat_store_t = std::vector<flat_item_ptr>;
                using flat_item_set_t = std::priority_queue<flat_item_ptr, flat_store_t, flat_less> ;
                flat_item_set_t _item_set;
            };
        } //ns:storage_policy

        template <class SourceRange, class DeflateFunction >
        struct FlattenRange;

        namespace details {
            template <class SourceRange, class DeflateFunction >
            struct FlattenTraits
            {
                using range_t = FlattenRange<SourceRange, DeflateFunction>;
                using source_iterator_t = typename SourceRange::iterator;
                //need ensure that applicator_result_t is kind of PrefixRange
                using pre_applicator_result_t = typename std::result_of<DeflateFunction(const source_iterator_t&)>::type;
                /**Type of Range returned by DeflateFunction. Must be kind of PrefixRange
                */
                using applicator_result_t = typename std::conditional< //strip shared_ptr from pre_applicator_result_t if present
                    OP::utils::is_generic<pre_applicator_result_t, std::shared_ptr>::value,
                    typename pre_applicator_result_t::element_type,
                    pre_applicator_result_t>::type; //type of 
                using key_type = typename applicator_result_t::key_t ;
                using key_t = typename applicator_result_t::key_t;

                using value_type = typename applicator_result_t::iterator::value_type ;
                using key_comparator_t = typename range_t::key_comparator_t;
                using store_t = typename storage_policy::PriorityQueueSetStorage< applicator_result_t, key_comparator_t > ;
            };

        } //ns:details

        /**
        * \tparam DeflateFunction functor that has spec: PrefixRange(const OriginIterator& )
        */
        template <class SourceRange, class DeflateFunction >
        struct FlattenRange : public 
            OrderedRange< flatten_details::DeflateResultType<DeflateFunction, typename SourceRange::iterator>, typename SourceRange::value_t >
        {
            using this_t = FlattenRange<SourceRange, DeflateFunction>;
            using base_t = OrderedRange< flatten_details::DeflateResultType<DeflateFunction, typename SourceRange::iterator>, typename SourceRange::value_t >;
            using traits_t = details::FlattenTraits<SourceRange, DeflateFunction>;

            using applicator_result_t = typename traits_t::applicator_result_t;
            static_assert(applicator_result_t::is_ordered_c, "DeflateFunction must produce range that support ordering");

            using value_type = typename traits_t::value_type;
            using iterator = typename base_t::iterator;
            using key_comparator_t = typename base_t::key_comparator_t;
            using store_t = storage_policy::PriorityQueueSetStorage< applicator_result_t, key_comparator_t > ;

            FlattenRange(std::shared_ptr<SourceRange const> source
                , DeflateFunction deflate
                , key_comparator_t key_comparator) noexcept
                : ordered_range_t(key_comparator)
                , _source_range(source)
                , _deflate(std::forward<DeflateFunction >(deflate))
            {
            }
            iterator begin() const override
            {
                auto store = std::make_unique< store_t >(key_comp());
                _source_range->for_each([&](const auto& i) {
                    auto range = _deflate(i);
                    auto range_beg = range->begin();
                    if (!range->in_range(range_beg)) //don't need add empty range
                    {
                        return;
                    }
                    auto new_itm = std::make_shared<store_t::flat_item_t> (
                        std::move(range), std::move(range_beg)
                    );
                    store->push(new_itm);
                });
                return iterator(shared_from_this(), std::move(store));
            }
            iterator lower_bound(const typename traits_t::key_type& key) const /*override*/
            {
                auto store = std::make_unique< store_t >(key_comp());
                _source_range->for_each([&](const auto& i) {
                    auto range = _deflate(i);
                    auto range_beg = range->begin();
                    if (!range->in_range(range_beg)) //don't need add empty range
                    {
                        return;
                    }
                    auto new_itm = std::make_shared<store_t::flat_item_t> (
                        std::move(range), std::move(range_beg)
                    );
                    store->push(new_itm);
                });
                return iterator(shared_from_this(), std::move(store));
            }

            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                return !check.impl< store_t>().is_empty();
            }
            void next(iterator& pos) const override
            {
                auto &impl = pos.impl< store_t>();
                auto smallest = impl.pop();
                smallest->first->next( smallest->second );
                if (smallest->first->in_range(smallest->second))
                { //if iter is not exhausted put it back
                    impl.push(smallest);
                }
            }

        private:
            std::shared_ptr<SourceRange const> _source_range;
            const DeflateFunction _deflate;
        };

        template <class K, class V, class DeflateFunction >
        inline std::shared_ptr< OrderedRange<flatten_details::DeflateResultType<DeflateFunction, RangeIterator<K, V>>, V > const>
            make_flatten_range(std::shared_ptr<RangeBase<K, V> const> src, DeflateFunction f)
        {
            return src->flatten(std::move(f));
            /*using src_range_t = decltype(src)::element_type;
            using flatten_range_t = FlattenRange<src_range_t, DeflateFunction >;
            return std::shared_ptr<OrderedRange<flatten_details::DeflateResultType<DeflateFunction, typename src_range_t::iterator>, typename src_range_t::value_t >>  (
                new flatten_range_t(
                src, 
                std::forward<DeflateFunction>(f), [](const auto& left, const auto& right) {
                const auto& k_left = left.key();
                const auto& k_right = right.key();
                return OP::ranges::str_lexico_comparator(
                    std::begin(k_left), std::end(k_left),
                    std::begin(k_right), std::end(k_right)
                );
            }));
            */
        }
    } //ns:trie
}//ns:OP
#endif //_OP_RANGES_FLATTEN_RANGE__H_
