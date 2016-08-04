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

        template <class SourceRange1, class SourceRange2>
        struct UnionAllRange;

        namespace details {

            template<class T,
                class D = void>
                struct DiscoverIteratorKeyType
            {   // default definition
                typedef D key_t;
            };

            template<class T>
            struct DiscoverIteratorKeyType<T, std::void_t< decltype(std::declval<typename T::reference>().first)> >
            {   // defined if iterator declared as std::map????::iterator and key-type detected as decltype of reference::first
                typedef typename std::remove_reference< decltype( std::declval<typename T::reference>().first) >::type key_t;
            };

            template<class T>
            struct DiscoverIteratorKeyType<T, std::void_t<typename T::key_type> >
            {   // defined if iterator contains explicit definition of key_type
                typedef typename T::key_type key_t;
            };

        } //ns:details
        namespace policy
        {
            /**Policy for SuffixRange::map that always evaluate new key for single iterator position. Result always uses 
            copy by value (or if available rvalue optimization)*/
            struct no_cache {};
            /**Policy for SuffixRange::map that evaluate new key for single iterator position only once, all other calls
            return cached value. Result of iterator::key is const reference type*/
            struct cached {};

            namespace details
            {
                template <class Iter, class Func, class Policy>
                using effective_policy_t = typename std::conditional< 
                        std::is_same<Policy, policy::cached> ::value,
                        FunctionResultCachedPolicy<Iter, Func>,
                        typename std::conditional< 
                            std::is_same<Policy, policy::no_cache> ::value,
                            FunctionResultNoCachePolicy<Iter, Func>,
                            Policy
                        >::type >
                    ::type
                ;
            }
        } //ns:policy
        /**
        *
        */
        template <class Iterator>
        struct SuffixRange : std::enable_shared_from_this< SuffixRange<Iterator> >
        {
            typedef Iterator iterator;
            typedef SuffixRange<Iterator> this_t;

            virtual ~SuffixRange() = default;

            virtual iterator begin() const = 0;
            virtual bool in_range(const iterator& check) const = 0;
            virtual void next(iterator& pos) const = 0;

            template <class UnaryFunction, class KeyPolicy>
            using functional_range_t = FunctionalRange<this_t, UnaryFunction, policy::details::effective_policy_t<iterator, UnaryFunction, KeyPolicy> >;
            /**
            *   \code std::result_of<UnaryFunction(typename iterator::value_type)>::type >::type
            */
            template <class KeyPolicy, class UnaryFunction>
            inline std::shared_ptr< functional_range_t<UnaryFunction, KeyPolicy > > map(UnaryFunction && f) const
            {
                return std::make_shared<functional_range_t<UnaryFunction, KeyPolicy >>(*this, std::forward<UnaryFunction>(f));
            }
            
            template <class UnaryFunction>
            inline std::shared_ptr< functional_range_t<UnaryFunction, policy::no_cache> > map(UnaryFunction && f) const
            {
                return map<policy::no_cache>(std::forward<UnaryFunction>(f));
            }
            template <class UnaryPredicate>
            inline std::shared_ptr< FilteredRange<this_t, UnaryPredicate> > filter(UnaryPredicate && f) const;

            template <class OtherRange>
            inline std::shared_ptr< JoinRange<this_t, OtherRange> > join(std::shared_ptr< OtherRange > & range,
                typename JoinRange<this_t, OtherRange>::iterator_comparator_t && cmp) const;

            template <class OtherRange>
            inline std::shared_ptr< UnionAllRange<this_t, OtherRange> > merge_all(std::shared_ptr< OtherRange > & range,
                typename UnionAllRange<this_t, OtherRange>::iterator_comparator_t && cmp) const;

            template <class Operation>
            size_t for_each(Operation && f) const
            {
                size_t n = 0;
                for (auto i = begin(); in_range(i); next(i), ++n)
                {
                    f(i);
                }
                return n;
            }
        };

        
        template <class SourceRange, class UnaryPredicate>
        struct FilteredRange : public SuffixRange<typename SourceRange::iterator > 
        {
            FilteredRange(std::shared_ptr<const SourceRange > && source_range, UnaryPredicate && predicate)
                : _source_range(std::forward<std::shared_ptr<const SourceRange >>(source_range))
                , _predicate(std::forward<UnaryPredicate>(predicate))
            {}

            virtual iterator begin() const
            {
                auto i = _source_range->begin();
                seek(i);
                return i;
            }
            virtual bool in_range(const iterator& check) const
            {
                return _source_range->in_range(check);
            }
            virtual void next(iterator& pos) const
            {
                _source_range->next(pos);
                seek(pos);
            }
        private:
            void seek(iterator& i)const
            {
                for (; _source_range->in_range(i); _source_range->next(i))
                {
                    if (_predicate(i))
                    {
                        return;
                    }
                }
            }
            std::shared_ptr<const SourceRange> _source_range;
            UnaryPredicate _predicate;
        };

        template<class Iterator>
        template<class UnaryPredicate>
        inline std::shared_ptr< FilteredRange<SuffixRange<Iterator>, UnaryPredicate> > SuffixRange<Iterator>::filter(UnaryPredicate && f) const
        {
            return std::make_shared<FilteredRange<this_t, UnaryPredicate>>(
                shared_from_this(), std::forward<UnaryPredicate>(f));
        }
}//ns:trie
}//ns:OP
#include <op/trie/ranges/JoinRange.h>
#include <op/trie/ranges/UnionAllRange.h>
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
            return first_right == end_right ? 0 : -1;
        }
        template<class Iterator>
        template<class OtherRange>
        inline std::shared_ptr<JoinRange<SuffixRange<Iterator>, OtherRange>> SuffixRange<Iterator>::join(
            std::shared_ptr< OtherRange > & other, typename JoinRange<this_t, OtherRange>::iterator_comparator_t && cmp) const
        {
            typedef JoinRange<SuffixRange<typename Iterator>, OtherRange> join_t;
            return std::make_shared<join_t>(
                shared_from_this(),
                other,
                std::forward<typename join_t::iterator_comparator_t>(cmp)
                );
        }
        template<class Iterator>
        template<class OtherRange>
        inline std::shared_ptr<UnionAllRange<SuffixRange<Iterator>, OtherRange>> SuffixRange<Iterator>::merge_all(
            std::shared_ptr< OtherRange> & other, typename UnionAllRange<SuffixRange<Iterator>, OtherRange>::iterator_comparator_t && cmp) const
        {
            
            typedef UnionAllRange<SuffixRange<typename Iterator>, OtherRange> merge_all_t;
            return std::make_shared<merge_all_t>(
                shared_from_this(),
                std::forward<std::shared_ptr<OtherRange>>( other ),
                std::forward<typename merge_all_t::iterator_comparator_t>(cmp)
            );
        }
}//ns:trie
}//ns:OP
#endif //_OP_TRIE_RANGES_SUFFIX_RANGE__H_
