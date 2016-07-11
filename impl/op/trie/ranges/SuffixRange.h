#ifndef _OP_TRIE_RANGES_SUFFIX_RANGE__H_
#define _OP_TRIE_RANGES_SUFFIX_RANGE__H_

#include <op/trie/TrieIterator.h>
#include <op/trie/ranges/FunctionalRange.h>

#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif

namespace OP
{
    namespace trie
    {

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
            inline FunctionalRange<this_t, UnaryFunction> map(UnaryFunction && f) const;
            
            template <class UnaryPredicate>
            inline FilteredRange<this_t, UnaryPredicate> filter(UnaryPredicate && f) const;

            template <class OtherRange>
            inline JoinRange<this_t, OtherRange> join(const OtherRange & f) const;
        };

        
        template <class SourceRange, class UnaryPredicate>
        struct FilteredRange : public SuffixRange<typename SourceRange::iterator > 
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
        inline FunctionalRange<typename SuffixRange<Iterator>, UnaryFunction> SuffixRange<Iterator>::map(UnaryFunction && f) const
        {
            return FunctionalRange<this_t, UnaryFunction>(*this, std::forward<UnaryFunction>(f));
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
        /**@return \li 0 if left range equals to right;
        \li < 0 - if left range is lexicographical less than right range;
        \li > 0 - if left range is lexicographical greater than right range. */
        template <class I1, class I2>
            inline int str_lexico_comparator(I1& first_left, const I1& end_left, 
                I2& first_right, const I2& end_right)
        {
            for (; first_left != end_left && first_right != end_right; ++first_left, ++first_right)
            {
                int r = static_cast<int>(static_cast<unsigned>(*first_left)) - 
                    static_cast<int>(static_cast<unsigned>(*first_right));
                if (r != 0)
                    return r;
            }
            if (first_left != end_left)
            { //left is longer
                return 1;
            }
            return first_right == end_right ? 0 : 1;
        }
        template<class Iterator>
        template<class OtherRange>
        inline JoinRange<SuffixRange<typename Iterator>, OtherRange> SuffixRange<Iterator>::join(const OtherRange & other) const
        {
            return JoinRange<this_t, OtherRange>(
                *this, 
                other, 
                [](const this_t::iterator& left, const OtherRange::iterator& right)->int {
                    auto&&left_prefix = left.prefix(); //may be return by const-ref or by value
                    auto&&right_prefix = right.prefix();//may be return by const-ref or by value
                    return str_lexico_comparator(left_prefix.begin(), left_prefix.end(), 
                        right_prefix.begin(), right_prefix.end());
            });
        }

}//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_SUFFIX_RANGE__H_
