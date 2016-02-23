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
        template <class Container>
        struct IteratorsRange : public SuffixRange<typename Container::payload_t>
        {
            typedef typename Container::iterator iterator;
            IteratorsRange(Container& container, iterator prefix) 
                : _container(container)
                , _prefix(prefix) 
            {
            }
            iterator begin() const
            {
            }
            iterator end() const
            {
                return _container.end();
            }
            
            void next(iterator& iter) const
            {
                _container.next(iter);
                if (iter != end())
                {
                    if (_prefix.is_above(iter))
                        return;
                    iter = end();
                }
            }
        private:
            Container& _container;
            iterator _prefix;
        };        
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_ITERATORS_RANGE__H_
