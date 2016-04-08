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
            IteratorsRange(iterator prefix, iterator begin) 
                : _begin(begin) 
                , _prefix(prefix)
            {
            }
            iterator begin() const override
            {
                return _begin;
            }
            iterator prefix() const
            {
                return _prefix;
            }
            bool in_range(const iterator& check) const override
            {
                if (check.is_end() ||
                    check.prefix().length() < _prefix.prefix().length())
                    return false;
                const atom_string_t& prefix_str = _prefix.prefix();
                auto m_pos = std::mismatch(
                    prefix_str.begin(), prefix_str.end(), check.prefix().begin());
                return (m_pos.first == _prefix.prefix().end());
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }
            
        private:
            iterator _begin, _prefix;
        };        
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_ITERATORS_RANGE__H_
