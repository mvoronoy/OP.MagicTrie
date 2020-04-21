#pragma once
#ifndef _OP_RANGES_SINGLETONRANGE__H_
#define _OP_RANGES_SINGLETONRANGE__H_

#include <op/ranges/PrefixRange.h>
#include <op/ranges/IteratorsRange.h>
#include <map>

namespace OP
{
    namespace ranges
    {
        /**
        *   Singleton range allows emulate range of excatly one item of key-value pair
        */
        template <class K, class V, class Compare = std::less<K> >
        struct SingletonRange : public OrderedRange< K, V >
        {
            using key_t = K;
            using base_t = OrderedRange< K, V >;
            using iterator = typename base_t::iterator;

            SingletonRange(K k, V v, Compare cmp = Compare())
                : base_t([this](const key_t& l, const key_t& r)->int
                    {
                        return _compare(l, r) ? -1 : _compare(r, l) ? 1 : 0;
                    })
                , _kv(std::move(k), std::move(v)) , _compare(std::move(cmp))
            {
            }

            iterator begin() const override
            {
                return iterator(shared_from_this(), std::make_unique<payload_t>(&_kv));
            }

            bool in_range(const iterator& check) const override
            {
                if (!check)
                    return false;
                return check.impl<payload_t>()._data_ptr != nullptr;
            }

            void next(iterator& pos) const override
            {
                if (!pos)
                    return;
                pos = end();
            }

            iterator lower_bound(const typename base_t::key_t& key) const override
            {
                int cmp = key_comp()(_kv.first, key);
                return cmp < 0
                    ? end()
                    : iterator(
                        shared_from_this(), std::make_unique<payload_t>(&_kv));
            }

        private:
            struct payload_t : public iterator::RangeIteratorImpl
            {
                payload_t(const std::pair<K, V>* data_ptr)
                    :_data_ptr(data_ptr)
                {}

                const K& key() const override
                {
                    if (!_data_ptr)
                        throw std::out_of_range("iterator run-out");
                    return _data_ptr->first;
                }
                const V& value() const override
                {
                    if (!_data_ptr)
                        throw std::out_of_range("iterator run-out");
                    return _data_ptr->second;
                }
                std::unique_ptr<typename iterator::RangeIteratorImpl> clone() const override
                {
                    return std::unique_ptr<typename iterator::RangeIteratorImpl>(new payload_t(*this));
                }
                const std::pair<K, V>* _data_ptr;
            };
            const std::pair<K, V> _kv;
            Compare _compare;
        };
        template <class K, class V>
        std::shared_ptr< OrderedRange<K, V> const > make_singleton_range(K k, V v)
        {
            return std::shared_ptr< OrderedRange<K, V> const >(new SingletonRange<K, V>(std::move(k), std::move(v)));
        }
        template <class K, class V, class Compare>
        std::shared_ptr< OrderedRange<K, V> const > make_singleton_range(K k, V v, Compare cmp)
        {
            return std::shared_ptr< OrderedRange<K, V> const >(new SingletonRange<K, V>(std::move(k), std::move(v), std::move(cmp)));
        }
    }//ns:ranges
}//ns:OP

#endif //_OP_RANGES_SINGLETONRANGE__H_
