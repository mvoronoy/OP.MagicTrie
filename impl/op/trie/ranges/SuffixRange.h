#ifndef _OP_TRIE_RANGES_SUFFIX_RANGE__H_
#define _OP_TRIE_RANGES_SUFFIX_RANGE__H_

#include <op/trie/TrieIterator.h>
#include <op/common/func_iter.h>

namespace OP
{
    namespace trie
    {
        template <class SourceRange, class UnaryFunction>
        struct MappedRange;

        template <class SourceRange, class UnaryPredicate>
        struct FilteredRange;

        template <class SourceRange1, class SourceRange2>
        struct JoinRange;
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
            inline MappedRange<this_t, UnaryFunction> map(UnaryFunction && f) const;
            
            template <class UnaryPredicate>
            inline FilteredRange<this_t, UnaryPredicate> filter(UnaryPredicate && f) const;

            template <class OtherRange>
            inline JoinRange<this_t, OtherRange> join(const OtherRange & f) const;
        };

        template <class SourceRange, class UnaryFunction>
        struct MappedRange : public SuffixRange<func_iterator<typename SourceRange::iterator, UnaryFunction> > /*Surjection?*/
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
        
        template <class SourceRange, class UnaryPredicate>
        struct FilteredRange : public SuffixRange<func_iterator<typename SourceRange::iterator, UnaryPredicate> > 
        {
            FilteredRange(const SourceRange & source_range, UnaryPredicate && predicate)
                : _source_range(source_range)
                , _predicate(std::forward<UnaryPredicate>(predicate))
            {}

            virtual iterator begin() const
            {
                auto i = _source_range.begin();
                seek(i);
                return i;
            }
            virtual bool in_range(const iterator& check) const
            {
                return _source_range.in_range(check.origin());
            }
            virtual void next(iterator& pos) const
            {
                _source_range.next(pos);
                seek(pos);
            }
        private:
            void seek(iterator& i)const
            {
                for (; _source_range.in_range(i); _source_range.next(i))
                {
                    if (_predicate(i))
                    {
                        return;
                    }
                }
            }
            const SourceRange & _source_range;
            UnaryPredicate _predicate;
        };

        template<class Iterator>
        template<class UnaryFunction>
        inline MappedRange<typename SuffixRange<Iterator>, UnaryFunction> SuffixRange<Iterator>::map(UnaryFunction && f) const
        {
            return MappedRange<this_t, UnaryFunction>(*this, std::forward<UnaryFunction>(f));
        }
        template<class Iterator>
        template<class UnaryPredicate>
        inline FilteredRange<SuffixRange<Iterator>, UnaryPredicate> SuffixRange<Iterator>::filter(UnaryPredicate && f) const
        {
            return FilteredRange<this_t, UnaryPredicate>(*this, std::forward<UnaryPredicate>(f));
        }
}//ns:trie
}//ns:OP
#include <op/trie/ranges/JoinRange.h>
namespace OP{
    namespace trie{
        template<class Iterator>
        template<class OtherRange>
        inline JoinRange<SuffixRange<typename Iterator>, OtherRange> SuffixRange<Iterator>::join(const OtherRange & other) const
        {
            return JoinRange<this_t, OtherRange>(
                *this, 
                other, 
                [](const iterator& left, const iterator& right)->bool {
                    return std::lexicographical_compare(left.prefix().begin(), left.prefix().end(), right.prefix().begin(), right.prefix().end());
            });
        }

}//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_SUFFIX_RANGE__H_
