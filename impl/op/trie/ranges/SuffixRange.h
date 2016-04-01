#ifndef _OP_TRIE_RANGES_SUFFIX_RANGE__H_
#define _OP_TRIE_RANGES_SUFFIX_RANGE__H_
#include <op/trie/TrieIterator.h>
#include <op/common/func_iter.h>

namespace OP
{
    namespace trie
    {
        template <class UnaryFunction>
        struct MappedRange;
        /**
        *
        */
        template <class Iterator>
        struct SuffixRange
        {
            typedef Iterator iterator;
            typedef SuffixRange<Iterator> this_t;

            virtual ~SuffixRange() = default;
            /**start lexicographical ascending iteration over trie content. Following is a sequence of iteration:
            *   - a
            *   - aaaaaaaaaa
            *   - abcdef
            *   - b
            *   - ...
            */
            //virtual std::unique_ptr<this_t> subtree(PrefixQuery& query) const = 0;
            virtual iterator begin() const = 0;
            virtual bool in_range(const iterator& check) const = 0;
            virtual void next(iterator& pos) const = 0;

            /**
            *   \code std::result_of<UnaryFunction(typename iterator::value_type)>::type >::type
            */
            template <class UnaryFunction>
            inline MappedRange<this_t, UnaryFunction> map(UnaryFunction f) const;
        };

        template <class SourceRange, class UnaryFunction>
        struct MappedRange : public SuffixRange<func_iterator<SourceRange::iterator, UnaryFunction> > /*Surjection?*/
        {
            MappedRange(const SourceRange & source_range, UnaryFunction && func)
                : _source_range(source_range)
                , _func(std::forward<UnaryFunction>(func))
            {}

            virtual iterator begin() const
            {
                return make_func_iterator(_source_range.begin(), _func);
            }
            virtual bool in_range(const iterator& check) const
            {
                return _source_range.in_range(check.origin());
            }
            virtual void next(iterator& pos) const
            {
                return _source_range.next(pos.origin());
            }
        private:
            const SourceRange & _source_range;
            UnaryFunction _func;
        };
        
        template<class Iterator>
        template<class UnaryFunction>
        inline MappedRange<SuffixRange<Iterator>, UnaryFunction> SuffixRange<Iterator>::map(UnaryFunction && f) const
        {
            return MappedRange<this_t, UnaryFunction>(*this, std::forward<UnaryFunction>(f));
        }
    }//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_SUFFIX_RANGE__H_
