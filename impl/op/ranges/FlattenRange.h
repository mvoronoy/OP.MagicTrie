#ifndef _OP_RANGES_FLATTEN_RANGE__H_
#define _OP_RANGES_FLATTEN_RANGE__H_

#include <op/ranges/PrefixRange.h>
#include <op/common/Utils.h>
#include <set>

namespace OP
{
    namespace ranges
    {
        namespace storage_policy
        {
            template <class Range>
            struct StoragePolicy
            {
                typedef std::shared_ptr<const Range> range_ptr;
                typedef std::pair<range_ptr, typename Range::iterator> flat_item_t;
                typedef std::shared_ptr<flat_item_t> flat_item_ptr;

                virtual ~StoragePolicy() = default;
                virtual void push(flat_item_ptr& ) = 0;
                virtual flat_item_ptr pop() = 0;
                virtual flat_item_ptr smallest() const = 0;
                virtual bool is_empty() const = 0;

            };
            template <class Range, class IteratorComparator>
            struct TreeSetStorage : public StoragePolicy<Range>
            {
                TreeSetStorage(const IteratorComparator& comparator) noexcept
                    :_comparator(comparator)
                {}
                void push(flat_item_ptr& item) override
                {
                    _item_set.emplace(item);
                }
                virtual flat_item_ptr pop() override
                {
                    auto b = _item_set.begin();
                    auto result = *b;
                    _item_set.erase(b);
                    return result;
                }
                flat_item_ptr smallest() const override
                {
                    return *_item_set.begin();
                }
                bool is_empty() const override
                {
                    return _item_set.empty();
                }
            private:
                struct flat_less
                {
                    flat_less() = delete;
                    flat_less(const IteratorComparator& comparator):
                        _comparator(comparator)
                    {}
                    bool operator ()(const flat_item_ptr& left, const flat_item_ptr& right) const
                    {
                        assert(left->first->in_range(left->second) &&
                            right->first->in_range(right->second));
                        auto test = _comparator(left->second, right->second);
                        return test < 0;
                    }
                    const IteratorComparator& _comparator;
                };
                typedef std::set<flat_item_ptr, flat_less> flat_item_set_t;
                const IteratorComparator& _comparator;
                flat_item_set_t _item_set = flat_item_set_t(_comparator);
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
                using iterator_comparator_t = std::function<int(const typename applicator_result_t::iterator&, const typename applicator_result_t::iterator&)> ;
                using store_t = typename storage_policy::TreeSetStorage< applicator_result_t, iterator_comparator_t > ;
            };

        }
        template <class FlattenTraits>
        struct FlattenRangeIterator 
        {
            using iterator_category = std::forward_iterator_tag;
            using source_iterator_t = typename FlattenTraits::source_iterator_t;
            using this_t = FlattenRangeIterator<FlattenTraits> ;
            using value_type = typename FlattenTraits::value_type;
            using key_type = typename FlattenTraits::key_type ;
            using key_t = typename FlattenTraits::key_type;
            using owner_range_t = typename FlattenTraits::range_t;
            using store_t = typename FlattenTraits::store_t ;
            friend FlattenTraits::range_t;

            FlattenRangeIterator(std::shared_ptr< const owner_range_t > owner_range, std::unique_ptr<store_t> store) noexcept
                : _owner_range(owner_range)
                , _store(std::move(store))
            {}
            this_t& operator ++()
            {
                _owner_range.next(*this);
                return *this;
            }
            this_t operator ++(int)
            {
                static_assert(false, "Sorry, no! There is no copy semantic for this kind of iterator");
            }
            value_type operator* () const
            {
                return *_store->smallest()->second;
            }
            key_type key() const
            {
                return _store->smallest()->second.key();
            }

        private:
            std::shared_ptr< const owner_range_t> _owner_range;
            std::unique_ptr<store_t> _store;
        };
        /**
        * \tparam DeflateFunction functor that has spec: PrefixRange(const OriginIterator& )
        */
        template <class SourceRange, class DeflateFunction >
        struct FlattenRange : public OrderedRange<
            FlattenRangeIterator< details::FlattenTraits<SourceRange, DeflateFunction> >
        >
        {
            using this_t = FlattenRange<SourceRange, DeflateFunction>;
            using traits_t = details::FlattenTraits<SourceRange, DeflateFunction>;
            using iterator = FlattenRangeIterator< traits_t> ;

            using applicator_result_t = typename traits_t::applicator_result_t;
            using value_type = typename traits_t::value_type;

            using iterator_comparator_t = typename traits_t::iterator_comparator_t;
            using store_t = storage_policy::TreeSetStorage< applicator_result_t, iterator_comparator_t > ;

            FlattenRange(std::shared_ptr<const SourceRange> source, DeflateFunction && deflate, iterator_comparator_t && iterator_comparator) noexcept
                : _source_range(source)
                , _deflate(std::forward<DeflateFunction >(deflate))
                , _iterator_comparator(std::forward<iterator_comparator_t>(iterator_comparator))
            {
            }
            iterator begin() const override
            {
                auto store = std::make_unique< store_t >(_iterator_comparator);
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
                return iterator(std::static_pointer_cast<const this_t>(shared_from_this()), std::move(store));
            }
            iterator lower_bound(const typename key_type& key) const override
            {
                auto store = std::make_unique< store_t >(_iterator_comparator);
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
                return iterator(std::static_pointer_cast<const this_t>(shared_from_this()), std::move(store));
            }

            bool in_range(const iterator& check) const override
            {
                return !(*check._store).is_empty();
            }
            void next(iterator& pos) const override
            {
                auto smallest = (*pos._store).pop();
                smallest->first->next( smallest->second );
                if (smallest->first->in_range(smallest->second))
                { //if iter is not exhausted put it back
                    (*pos._store).push(smallest);
                }
            }

        private:
            std::shared_ptr<const SourceRange> _source_range;
            const iterator_comparator_t _iterator_comparator;
            const DeflateFunction _deflate;

        };

        template <class SourceRange, class DeflateFunction >
        inline std::shared_ptr< FlattenRange<SourceRange, DeflateFunction > > make_flatten_range(std::shared_ptr<SourceRange> src, DeflateFunction && f)
        {
            return std::make_shared<FlattenRange<SourceRange, DeflateFunction > > (
                src, 
                std::forward<DeflateFunction>(f), [](const auto& left, const auto& right) {
                return OP::ranges::str_lexico_comparator(std::begin(left.key()), std::end(left.key()),
                    std::begin(right.key()), std::end(right.key()));
            });
            
        }
    } //ns:trie
}//ns:OP
#endif //_OP_RANGES_FLATTEN_RANGE__H_
