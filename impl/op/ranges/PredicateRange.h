#ifndef _OP_RANGES_PREDICATE_RANGE__H_
#define _OP_RANGES_PREDICATE_RANGE__H_

#include <op/ranges/SuffixRange.h>
#include <op/ranges/IteratorsRange.h>
#include <functional>

namespace OP
{
    namespace ranges
    {

        /**
        *
        */
        template <class Iterator, class KeyDiscover = details::DiscoverIteratorKey<Iterator> >
        struct PredicateRange : public SuffixRange< IteratorWrap< Iterator, KeyDiscover> >
        {
            typedef typename KeyDiscover::key_t key_type;
            typedef Iterator origin_iter_t;
            typedef std::function<bool(const iterator& check)> in_range_predicate_t;
            PredicateRange(origin_iter_t begin, in_range_predicate_t in_range_predicate, KeyDiscover key_discover = KeyDiscover() ) noexcept
                : _begin(begin) 
                , _in_range_predicate(in_range_predicate)
                , _key_discover(key_discover)
            {
            }

            iterator begin() const override
            {
                return iterator(_begin, _key_discover);
            }
            iterator end() const
            {
                return iterator(_end, _key_discover);
            }
            bool in_range(const iterator& check) const override
            {
                return _in_range_predicate(check);
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }
        private:
            origin_iter_t _begin;
            in_range_predicate_t _in_range_predicate;
            KeyDiscover _key_discover;
        }; 
        /**Implement always false predicate for PredicateRange. Used to create empty PredicateRange */
        template <class Iterator>
        struct AlwaysFalseRangePredicate
        {
            bool operator()(const Iterator&) const
            {
                return false;
            }
        };
    }//ns:ranges
}//ns:OP
#endif //_OP_RANGES_PREDICATE_RANGE__H_
