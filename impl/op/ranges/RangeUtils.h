#ifndef _OP_RANGES_RANGE_UTILS__H_
#define _OP_RANGES_RANGE_UTILS__H_
#include <op/ranges/PrefixRange.h>
#include <op/ranges/OrderedRange.h>
#include <op/ranges/UnionAllRange.h>


namespace OP
{
    namespace ranges {
        namespace utils {

            template <class TRange1, class TRange2>
            inline bool key_equals(const TRange1& range1, const TRange2& range2)
            {
                auto from1 = range1.begin();
                auto from2 = range2.begin();
                for (; range1.in_range(from1) && range2.in_range(from2); range1.next(from1), range2.next(from2))
                {
                    if (from1.key() != from2.key())
                        return false;
                }
                return !range1.in_range(from1) && !range2.in_range(from2);
            }

            //template <class TRange, class K, class V>
            template <class TRange, class Map>
            inline bool range_map_equals(const TRange& range1, const Map& range2)
            {
                auto from1 = range1.begin();
                auto from2 = std::begin(range2);
                auto to2 = std::end(range2);
                for (; range1.in_range(from1) && from2 != to2; range1.next(from1), ++from2)
                {
                    if (OP::ranges::key_discovery::key(from1) != from2->first)
                        return false;
                }
                return !range1.in_range(from1) && (from2 == to2);
            }
            template <class Iterator1, class Container>
            inline bool map_equals(const PrefixRange<Iterator1, true>& range1, const Container& range2)
            {
                auto from1 = range1.begin();
                auto from2 = std::begin(range2);
                auto to2 = std::end(range2);
                for (; range1.in_range(from1) && from2 != to2; range1.next(from1), ++from2)
                {
                    if (from1.key() != from2->first)
                        return false;
                    if (*from1 != from2->second)
                        return false;
                
                }
                return !range1.in_range(from1) && (from2 == to2);
            }

        } //ns:utils
    } //ns:ranges
}//ns:op
#endif //_OP_RANGES_RANGE_UTILS__H_

