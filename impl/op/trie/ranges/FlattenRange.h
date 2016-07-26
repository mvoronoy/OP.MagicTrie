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
                flat_item_ptr smallest() override override
                {
                    return *_item_set.begin();
                }
                virtual bool is_empty()override const
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
        }

        template <class TStoragePolicy, class OwnerRange>
        struct FlattenRangeIterator :
            public std::iterator<std::forward_iterator_tag, typename SourceIterator::value_type>
        {
            typedef SourceIterator source_iterator_t;
            typedef FlattenRangeIterator<SourceIterator, OwnerRange> this_t;
            typedef typename SourceIterator::value_type value_type;

            friend OwnerRange;
            FlattenRangeIterator(const OwnerRange& owner_range, source_iterator_t source)
                : _owner_range(owner_range)
                , _source(source)
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
                return *_source;
            }
            applicator_result_t key() const
            {
                return _key_eval_policy.get(_source, _owner_range.transform());
            }

        private:
            const OwnerRange& _owner_range;
            source_iterator_t _source;
            key_eval_policy_t _key_eval_policy;
        };
        /**
        * \tparam DeflateFunction functor that has spec: SuffixRange(const OriginIterator& )
        */
        template <class SourceRange, class DeflateFunction >
        struct FlattenRange : public SuffixRange<
            FlattenRangeIterator<
            typename SourceRange::iterator,
            FlattenRange<OriginIterator, DeflateFunction>
        > >
        {
            typedef FlattenRange<OriginIterator, DeflateFunction> this_t;
            typedef FlattenRangeIterator<
                typename SourceRange::iterator,
                typename this_t
            > iterator;
            //need ensure that applicator_result_t is kind of SuffixRange
            typedef typename std::result_of<DeflateFunction(const typename SourceRange::iterator&)>::type applicator_result_t;
            typedef std::function<int(const left_iterator&, const right_iterator&)> iterator_comparator_t;

            FlattenRange(const SourceRange & source, DeflateFunction && deflate, iterator_comparator_t && iterator_comparator)
                : _source_range(source)
                , _transform(std::forward<UnaryFunction >(deflate))
                , _iterator_comparator(std::forward<iterator_comparator_t>(iterator_comparator))
            {
            }
            iterator begin() const override
            {
                using store_type = TreeSetStorage< applicator_result_t, iterator_comparator_t >;
                auto store = std::make_unique< store_type >();
                _source_range.for_each([&store](const auto& i) {
                    //store->push(std::make_shared(i->key()));
                });
                return iterator(*this, _source_range.begin(), key_eval_policy_t());
            }
            bool in_range(const iterator& check) const override
            {
                return _source_range.in_range(check._source);
            }
            void next(iterator& pos) const override
            {
                _source_range.next(pos._source);

                if (in_range(pos))
                {//notify policy that key was changed
                    pos._key_eval_policy.on_after_change(pos._source, _transform);
                }
            }

        private:
            const SourceRange& _source_range;
            UnaryFunction _transform;
            const iterator_comparator_t _iterator_comparator;

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
