#ifndef _OP_RANGES_ITERATORS_RANGE__H_
#define _OP_RANGES_ITERATORS_RANGE__H_

#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <map>

namespace OP
{
    namespace ranges
    {
        namespace details
        {
            template <class Map>
            struct map_reference_keeper
            {                                
                map_reference_keeper(const Map& map)
                    :_reference_keeper(map){}
                const Map& operator()() const
                {
                    return _reference_keeper;
                }
                const Map& _reference_keeper;
            };
            template <class Map>
            struct map_copying
            {
                map_copying(Map&& map)
                    :_map_copy(std::move(map)) {}
                const Map& operator()() const
                {
                    return _map_copy;
                }
                Map _map_copy;
            };
        }

        /**
        *   Range is a kind of IteratorsRange that allows create ordered sequence from std::map container
        */
        template <class Map>
        struct SortedMapRange : 
            public OrderedRange<typename Map::key_type, typename Map::mapped_type>
        {
            using base_t = OrderedRange<typename Map::key_type, typename Map::mapped_type>;

            using iterator = typename base_t::iterator;
            using key_t = typename base_t::key_t;
            using value_t = typename base_t::value_t;

            SortedMapRange(const Map& source)
                : base_t([this](const key_t&l, const key_t&r)->int{
                    const auto &komp = _source_resolver().key_comp();
                    return komp(l, r)?-1: komp(r, l)?1:0;})
                , _source_resolver(details::map_reference_keeper<Map>(source))
            {
            }
            SortedMapRange(Map&& source)
                : base_t([this](const key_t& l, const key_t& r)->int {
                        const auto& komp = _source_resolver().key_comp();

                        return komp(l, r) ? -1 : komp(r, l) ? 1 : 0;
                    })
                , _source_resolver(details::map_copying<Map>(std::move(source)))
            {
            }
            iterator begin() const override
            {
                return iterator(shared_from_this(), std::make_unique<payload>(_source_resolver().begin()));
            }
            iterator end() const
            {
                return iterator(std::make_unique<payload>(_source_resolver().end()));
            }
            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                return check.impl<payload>()._i != _source_resolver().end();
            }
            void next(iterator& pos) const override
            {
                ++pos.impl<payload>()._i;
            }

            iterator lower_bound(const typename base_t::key_t& key) const override
            {
                return iterator(
                    shared_from_this(), std::make_unique<payload>(_source_resolver().lower_bound(key)));
            }
        private:
            std::function< const Map& ()> _source_resolver;
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

        template <class T, typename... Ts>
        inline std::shared_ptr< OrderedRange< typename std::map<T, Ts...>::key_type, typename std::map<T, Ts...>::mapped_type > const >  make_range_of_map(std::map<T, Ts...>&& co)
        {
            using map_t = std::map<T, Ts...>;
            using range_t = OrderedRange< typename map_t::key_type, typename map_t::mapped_type >;

            return std::shared_ptr<range_t const>(new SortedMapRange<map_t>(std::move(co)));
        }
    }//ns:ranges
}//ns:OP
#endif //_OP_RANGES_ITERATORS_RANGE__H_
