#ifndef _OP_TRIE_RANGES_SUFFIX_RANGE__H_
#define _OP_TRIE_RANGES_SUFFIX_RANGE__H_
#include <op/trie/TrieIterator.h>

namespace OP
{
    namespace trie
    {
        /**
        *
        */
        template <class Iterator>
        struct SuffixRange
        {
            typedef Iterator iterator;
            /**start lexicographical ascending iteration over trie content. Following is a sequence of iteration:
            *   - a
            *   - aaaaaaaaaa
            *   - abcdef
            *   - b
            *   - ...
            */
            //virtual std::unique_ptr<this_t> subtree(PrefixQuery& query) const = 0;
            virtual iterator begin() const = 0;
            virtual iterator end() const = 0;
            virtual void next(iterator& pos) const = 0;
        };
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_SUFFIX_RANGE__H_
