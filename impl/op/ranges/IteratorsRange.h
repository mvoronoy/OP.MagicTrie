#ifndef _OP_RANGES_ITERATORS_RANGE__H_
#define _OP_RANGES_ITERATORS_RANGE__H_

#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <map>

namespace OP
{
    namespace ranges
    {
   
        template <class OriginIterator>
        struct IteratorsRange : 
            public PrefixRange< OriginIterator, false>
        {
            using iterator = OriginIterator;
            using origin_iter_t = OriginIterator;
            using base_t = PrefixRange< origin_iter_t, false>;
            //using base_t::base_t;

            IteratorsRange(const origin_iter_t& begin, const origin_iter_t& end) noexcept
                : _begin(begin)
                , _end(end)
            {
            }
            /**Extract iterators from arbitrary containers that supports operations: std::begin, std::end*/
            template <class Container>
            IteratorsRange(const Container& source)
                : IteratorsRangeBase(std::begin(source), std::end(source))
            {
            }
            iterator begin() const override
            {
                return _begin;
            }
            iterator end() const
            {
                return _end;
            }
            bool in_range(const iterator& check) const override
            {
                return check != _end;
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }

        private:
            origin_iter_t _begin, _end;
        };

        /**
        *   Range is a kind of IteratorsRange that allows create ordered sequence from std::map container
        */
        template <class Map>
        struct SortedMapRange : 
            public OrderedRange<typename Map::const_iterator>
        {
            using iterator = typename Map::const_iterator;
            using origin_iter_t = typename Map::const_iterator;
            using base_t = OrderedRange<iterator>;

            SortedMapRange(const Map& source)
                : base_t(source.key_comp())
                , _source(source)
            {
            }
            iterator begin() const override
            {
                return _source.begin();
            }
            iterator end() const
            {
                return _source.end();
            }
            bool in_range(const iterator& check) const override
            {
                return check != _source.end();
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }

            iterator lower_bound(const typename base_t::key_type& key) const override
            {
                return _source.lower_bound(key);
            }
        private:
            const Map& _source;
        };

        template <class Container>
        inline std::shared_ptr< IteratorsRange<typename Container::const_iterator> > make_iterators_range(const Container& co)
        {
            return std::make_shared<IteratorsRange<typename Container::const_iterator>>(co);
        }

        template <class T, typename... Ts>
        inline std::shared_ptr< SortedMapRange<std::map<T, Ts...> > const > make_iterators_range(const std::map<T, Ts...>& co)
        {
            using map_t = std::map<T, Ts...>;
            return std::make_shared< SortedMapRange<map_t> >(co);
        }

    }//ns:ranges
}//ns:OP
#endif //_OP_RANGES_ITERATORS_RANGE__H_
