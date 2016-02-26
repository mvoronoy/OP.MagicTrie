#ifndef _OP_TRIE_RANGES_ITERATORS_RANGE__H_
#define _OP_TRIE_RANGES_ITERATORS_RANGE__H_
#include <op/trie/TrieIterator.h>
#include <op/trie/ranges/SuffixRange.h>

namespace OP
{
    namespace trie
    {
        /**
        *
        */
        template <class Iterator>
        struct IteratorsRange : public SuffixRange<Iterator>
        {
            typedef Iterator iterator;
            IteratorsRange(iterator begin, iterator end) 
                : _begin(begin) 
                , _end(end) 
            {
            }
            iterator begin() const override
            {
                return _begin;
            }
            iterator end() const override
            {
                return _end;
            }
            virtual void next(iterator& pos) const override
            {
                return ++pos;
            }
            
        private:
            iterator _begin, _end;
        };        
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_ITERATORS_RANGE__H_
