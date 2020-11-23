#ifndef _OP_RANGES_FLATTEN_RANGE__H_
#define _OP_RANGES_FLATTEN_RANGE__H_
#pragma once

#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <op/common/Utils.h>
#include <queue>

#if (__cplusplus > 201402L) || (_MSVC_LANG >= 201703L)
#define OP_FNC_RESULT(expr, arg) std::invoke_result_t<expr, arg>
#else 
#define OP_FNC_RESULT(expr, arg) std::result_of_t<expr(arg)>
#endif // cpp-ver

namespace OP
{
    namespace ranges
    {
        namespace storage_policy
        {
            /** Abstract class that allows FlattenRange sort contained ranges.
            * Partial implementation in fact should sort produced by DeflateFunction ranges by smallest item
            */
            template <class Range>
            struct StoragePolicy : public Range::iterator::RangeIteratorImpl
            {
                using range_ptr = std::shared_ptr<const Range> ;
                using flat_item_t = std::pair<range_ptr, typename Range::iterator> ;
                using flat_item_ptr = std::shared_ptr<flat_item_t>;
                using key_t = typename Range::key_t;
                using value_t = typename Range::value_t;

                virtual ~StoragePolicy() = default;
                /** Place range to sorted container */
                virtual void push(flat_item_ptr& ) = 0;
                /** remove smallest range from conatiner */
                virtual flat_item_ptr pop() = 0;
                /** Get container that contains smallest element */
                virtual flat_item_ptr smallest() const = 0;
                /** Check if no inner ranges in container */
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

            /** Implementation of StoragePolicy with std::priority_queue. With range size grow  
            * memory consumption may became critical. Some external storage may required instead of this implmentation */
            template <class Range, class KeyComparator>
            struct PriorityQueueSetStorage : public StoragePolicy<Range>
            {
                using base_t = StoragePolicy<Range>;
                using flat_item_ptr = typename base_t::flat_item_ptr;
                using iterartor_impl_t = typename Range::iterator::RangeIteratorImpl;

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
                std::unique_ptr<iterartor_impl_t > clone() const override
                {
                    return std::unique_ptr<iterartor_impl_t >(new PriorityQueueSetStorage(*this));
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
                using pre_applicator_result_t = OP_FNC_RESULT(DeflateFunction, const source_iterator_t&);

                using applicator_result_t = typename pre_applicator_result_t::element_type; 
                using key_type = typename applicator_result_t::key_t ;
                using key_t = typename applicator_result_t::key_t;

                using value_type = typename applicator_result_t::iterator::value_type ;
                using key_comparator_t = typename range_t::key_comparator_t;
                using store_t = typename storage_policy::PriorityQueueSetStorage< applicator_result_t, key_comparator_t > ;
            };

        } //ns:details

        /**
        * FlattenRange allows map (project) one unordered range to another by flatten generated ranges into result ordered range. For example
        * assume DeflateFunction produces range of 2 items by concatenation 'aa' an 'bb' to input `key`. Source code may look:
        * \code
            auto flatten_range = source_range->flatten([](const auto& i) {
                std::map<std::string, double> result_map = {
                {i.key() + "aa", i.value() + 0.1},
                {i.key() + "bb", i.value() + 0.2},
            };
            return OP::ranges::make_range_of_map(std::move(result_map));
            });
        * \endcode 
        * Running this code against source range 
        * ~~~~
        *   `{{"a", 1}, {"b", 2}, {"c", 3}}` 
        * ~~~~~
        * will produce:
        * ~~~~
        *   `{{"aaa", 1.1}, {"abb", 1.2}, {"baa", 2.1}, {"bbb", 2.2}, , {"caa", 3.1}, {"cbb", 3.2}}`
        * ~~~~
        * \tparam DeflateFunction functor that has spec: `shared_ptr<OrderedRange>(const iterator& )` - return new range from current item
        */
        template <class SourceRange, class DeflateFunction >
        struct FlattenRange : public 
            OrderedRange< flatten_details::DeflateResultType<DeflateFunction, typename SourceRange::iterator>, typename SourceRange::value_t >
        {
            using this_t = FlattenRange<SourceRange, DeflateFunction>;
            using ordered_range_t = OrderedRange< flatten_details::DeflateResultType<DeflateFunction, typename SourceRange::iterator>, typename SourceRange::value_t >;
            using base_t = ordered_range_t ;
            using traits_t = details::FlattenTraits<SourceRange, DeflateFunction>;

            using applicator_result_t = typename traits_t::applicator_result_t;
            static_assert(applicator_result_t::is_ordered_c, "DeflateFunction must produce range that support ordering");

            using value_type = typename traits_t::value_type;
            using iterator = typename base_t::iterator;
            using key_comparator_t = typename base_t::key_comparator_t;
            using store_t = storage_policy::PriorityQueueSetStorage< applicator_result_t, key_comparator_t > ;

            /** 
            * \param source input unordered range
            * \param deflate functor that produces ordered range from one item of source range
            * \param key_comparator functor that compares 2 keys ( &lt;0, == 0, &gt;0 for less, equal and great corresponding )
            */
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
                auto store = std::make_unique< store_t >(this->key_comp());
                _source_range->for_each([&](const auto& i) {
                    auto range = _deflate(i);
                    if( !range ) return; //don't need add empty range
                    auto range_beg = range->begin();
                    if (!range->in_range(range_beg)) 
                    {//don't need add empty range
                        return;
                    }
                    auto new_itm = std::make_shared<typename store_t::flat_item_t> (
                        std::move(range), std::move(range_beg)
                    );
                    store->push(new_itm);
                });
                return iterator(this->shared_from_this(), std::move(store));
            }
            
            iterator lower_bound(const typename traits_t::key_type& key) const override
            {
                auto store = std::make_unique< store_t >(this->key_comp());
                _source_range->for_each([&](const auto& i) {
                    auto range = _deflate(i);
                    if( !range ) return; //don't need add empty range
                    auto range_beg = range->lower_bound(key);
                    if (!range->in_range(range_beg)) //don't need add empty range
                    {
                        return;
                    }
                    auto new_itm = std::make_shared<typename store_t::flat_item_t> (
                        std::move(range), std::move(range_beg)
                    );
                    store->push(new_itm);
                });
                return iterator(this->shared_from_this(), std::move(store));
            }

            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                return !check.OP_TEMPL_METH(impl)< store_t>().is_empty();
            }
            void next(iterator& pos) const override
            {
                if(!pos)
                    return;
                auto &impl = pos.OP_TEMPL_METH(impl)< store_t>();
                auto const dupli_key = impl.key();
                do{ //skip key dupplicates
                    auto smallest = impl.pop();
                    smallest->first->next( smallest->second );
                    if (smallest->first->in_range(smallest->second))
                    { //if iter is not exhausted put it back
                        impl.push(smallest);
                    }
                }while(!impl.is_empty() && 0 == this->key_comp()(dupli_key, impl.key()));
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
        }
    } //ns:trie
}//ns:OP
#endif //_OP_RANGES_FLATTEN_RANGE__H_
