#ifndef _OP_TRIE_RANGES_ITERATORS_RANGE__H_
#define _OP_TRIE_RANGES_ITERATORS_RANGE__H_

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
            iterator end() const
            {
                return _end;
            }
            bool in_range(const iterator& check) const override
            {
                if ( check.is_end() )
                    return false;
                return check < _end && !(check < _begin);
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }
            
        private:
            iterator _begin, _end;
        };        
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_ITERATORS_RANGE__H_
