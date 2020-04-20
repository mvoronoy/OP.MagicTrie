#ifndef _OP_RANGES_ITERATORS_RANGE__H_
#define _OP_RANGES_ITERATORS_RANGE__H_

#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <map>

namespace OP
{
    namespace ranges
    {

        /**
        *   Range is a kind of IteratorsRange that allows create ordered sequence from std::map container
        */
        template <class Map>
        struct SortedMapRange : 
            public OrderedRange<typename Map::key_type, typename Map::mapped_type>
        {
            using base_t = OrderedRange<typename Map::key_type, typename Map::mapped_type>;

            SortedMapRange(const Map& source)
                : base_t([this](const key_t&l, const key_t&r)->int{return _source.key_comp()(l, r)?-1: _source.key_comp()(r, l)?1:0;})
                , _source(source)
            {
            }
            iterator begin() const override
            {
                return iterator(shared_from_this(), std::make_unique<payload>(_source.begin()));
            }
            iterator end() const
            {
                return iterator(std::make_unique<payload>(_source.end()));
            }
            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                return check.impl<payload>()._i != _source.end();
            }
            void next(iterator& pos) const override
            {
                ++pos.impl<payload>()._i;
            }

            iterator lower_bound(const typename base_t::key_t& key) const override
            {
                return iterator(
                    shared_from_this(), std::make_unique<payload>(_source.lower_bound(key)));
            }
        private:
            const Map& _source;
            using map_iterator_t = typename Map::const_iterator;

            struct payload : public iterator::RangeIteratorImpl
            {
                payload(map_iterator_t i)
                    :_i(i)
                {}
                const typename Map::key_type & key() const
                {
                    return _i->first;
                }
                const typename Map::mapped_type& value() const
                {
                    return _i->second;
                }
                std::unique_ptr<typename iterator::RangeIteratorImpl> clone() const
                {
                    return std::make_unique<payload>(_i);
                }
                map_iterator_t _i;
            };
        };

        template <class T>
        inline std::shared_ptr< OrderedRange< typename T::key_type,  typename T::mapped_type > > make_range_of_map(const T& co)
        {
            using range_t = OrderedRange< typename T::key_type, typename T::mapped_type >;
            return std::shared_ptr<range_t>(new SortedMapRange<T>(co));
        }


        template <class T, typename... Ts>
        inline std::shared_ptr< OrderedRange< typename std::map<T, Ts...>::key_type, typename std::map<T, Ts...>::mapped_type > const >  make_range_of_map(const std::map<T, Ts...>& co)
        //template <class T>
        //inline std::shared_ptr< OrderedRange< typename T::key_type, typename T::mapped_type > > make_range_of_map(const T& co)
        {
            using map_t = std::map<T, Ts...>;
            using range_t = OrderedRange< typename map_t::key_type, typename map_t::mapped_type >;

            return std::shared_ptr<range_t const>(new SortedMapRange<map_t>(co));
        }
    }//ns:ranges
}//ns:OP
#endif //_OP_RANGES_ITERATORS_RANGE__H_
