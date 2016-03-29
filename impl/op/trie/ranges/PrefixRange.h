#ifndef _OP_TRIE_RANGES_PREFIX_RANGE__H_
#define _OP_TRIE_RANGES_PREFIX_RANGE__H_
#include <op/trie/ranges/SuffixRange.h>

namespace OP
{
    namespace trie
    {
        template <class Iterator>
        struct JoinRange : public SuffixRange<Iterator>
        {
            typedef Iterator iterator;
            typedef SuffixRange< iterator > base_range_t;
            JoinRange(base_range_t& r1, base_range_t& r2)
                : _r1(r1) 
                , _r2(r2)
            {
            }
            
            iterator begin() const override
            {
                return _begin;
            }
            bool is_end(const iterator& check) const override
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
            base_range_t _r1, _r2;
        };
    } //ns: trie
} //ns: OP
#endif //_OP_TRIE_RANGES_PREFIX_RANGE__H_
