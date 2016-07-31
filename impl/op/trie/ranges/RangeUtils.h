#ifndef _OP_TRIE_RANGES_RANGE_UTILS__H_
#define _OP_TRIE_RANGES_RANGE_UTILS__H_
#include <op/trie/ranges/SuffixRange.h>

namespace OP
{
    namespace trie {
        namespace utils {


            template <class Iterator1, class Iterator2>
            inline bool key_equals(const SuffixRange<Iterator1>& range1, const SuffixRange<Iterator2>& range2)
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

            template <class Iterator1, class Container>
            inline bool key_equals(const SuffixRange<Iterator1>& range1, const Container& range2)
            {
                auto from1 = range1.begin();
                auto from2 = std::begin(range2);
                auto to2 = std::end(range2);
                for (; range1.in_range(from1) && from2 != to2; range1.next(from1), ++from2)
                {
                    if (from1.key() != from2->first)
                        return false;
                }
                return !range1.in_range(from1) && (from2 == to2);
            }
            template <class Iterator1, class Container>
            inline bool map_equals(const SuffixRange<Iterator1>& range1, const Container& range2)
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
    } //ns:trie
}//ns:op
#endif //_OP_TRIE_RANGES_RANGE_UTILS__H_

