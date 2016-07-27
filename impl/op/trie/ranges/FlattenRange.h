#ifndef _OP_TRIE_RANGES_FLATTEN_RANGE__H_
#define _OP_TRIE_RANGES_FLATTEN_RANGE__H_

#include <op/trie/ranges/SuffixRange.h>
#include <set>

namespace OP
{
    namespace trie
    {
        namespace storage_policy
        {
            template <class Range>
            struct StoragePolicy
            {
                typedef std::pair<Range, typename Range::iterator> flat_item_t;
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
                        assert(left->first.in_range(left->second) &&
                            right->first.in_range(right->second));
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

        template <class OwnerRange>
        struct FlattenRangeIterator :
            public std::iterator<std::forward_iterator_tag, typename OwnerRange::iterator::value_type>
        {
            typedef typename OwnerRange::iterator source_iterator_t;
            typedef FlattenRangeIterator<OwnerRange> this_t;
            typedef typename source_iterator_t value_type;
            typedef typename OwnerRange::key_t key_type;
            typedef typename OwnerRange::store_t store_t;
            friend OwnerRange;
            FlattenRangeIterator(const OwnerRange& owner_range, std::unique_ptr<store_t> store)
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
                return *_store->smallest()->second.key();
            }

        private:
            const OwnerRange& _owner_range;
            std::unique_ptr<store_t> _store;
        };
        /**
        * \tparam DeflateFunction functor that has spec: SuffixRange(const OriginIterator& )
        */
        template <class SourceRange, class DeflateFunction >
        struct FlattenRange : public SuffixRange<
            FlattenRangeIterator<
            FlattenRange<SourceRange, DeflateFunction>
        > >
        {
            typedef FlattenRange<SourceRange, DeflateFunction> this_t;
            typedef FlattenRangeIterator<this_t> iterator;
            typedef typename SourceRange::iterator source_iterator_t;
            //need ensure that applicator_result_t is kind of SuffixRange
            typedef typename std::result_of<DeflateFunction(const source_iterator_t&)>::type applicator_result_t;
            typedef std::function<int(const source_iterator_t&, const source_iterator_t&)> iterator_comparator_t;
            typedef storage_policy::TreeSetStorage< applicator_result_t, iterator_comparator_t > store_t;

            FlattenRange(const SourceRange & source, DeflateFunction && deflate, iterator_comparator_t && iterator_comparator)
                : _source_range(source)
                , _deflate(std::forward<DeflateFunction >(deflate))
                , _iterator_comparator(std::forward<iterator_comparator_t>(iterator_comparator))
            {
            }
            iterator begin() const override
            {
                auto store = std::make_unique< store_t >();
                _source_range.for_each([&store](const auto& i) {
                    //store->push(std::make_shared(i->key()));
                });
                return iterator(std::move(store));
            }
            bool in_range(const iterator& check) const override
            {
                return false; //_source_range.in_range(check._source);
            }
            void next(iterator& pos) const override
            {
                //_source_range.next(pos._source);
            }

        private:
            const SourceRange& _source_range;
            const iterator_comparator_t _iterator_comparator;
            const DeflateFunction _deflate;

        };

        template <class SourceRange, class DeflateFunction >
        inline FlattenRange<SourceRange, DeflateFunction > make_flatten_range(const SourceRange& src, DeflateFunction && f)
        {
            return FlattenRange<SourceRange, DeflateFunction >(src, std::forward<DeflateFunction>(f), [](const auto& left, const auto& right) {
                if (left.key() < right.key())
                    return -1;
                if (right.key() < left.key())
                    return 1;
                else return 0;
            })
            iterator_comparator_t && iterator_comparator
        }
    } //ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_FLATTEN_RANGE__H_
