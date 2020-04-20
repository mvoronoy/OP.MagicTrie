#ifndef _OP_RANGES_PREDICATE_RANGE__H_
#define _OP_RANGES_PREDICATE_RANGE__H_

#include <op/ranges/PrefixRange.h>
#include <op/ranges/IteratorsRange.h>
#include <functional>

namespace OP
{
    namespace ranges
    {

        /**
        *
        */
        template <class SourceRange >
        struct TakeWhileRange : public SourceRange
        {
            using in_range_predicate_t = std::function<bool(const iterator& check)> ;
            TakeWhileRange(iterator begin, in_range_predicate_t in_range_predicate ) noexcept
                : _begin(begin) 
                , _in_range_predicate(in_range_predicate)
                , _key_discover(key_discover)
            {
            }

            iterator begin() const override
            {
                return _begin;
            }

            bool in_range(const iterator& check) const override
            {
                return _in_range_predicate(check);
            }
            void next(iterator& pos) const override
            {
                ++pos;
            }

        private:
            iterator _begin;
            in_range_predicate_t _in_range_predicate;
        }; 
        /**Implement always false predicate for TakeWhileRange. Used to create empty TakeWhileRange */
        template <class Iterator>
        struct AlwaysFalseRangePredicate
        {
            bool operator()(const Iterator&) const
            {
                return false;
            }
        };
    }//ns:ranges
}//ns:OP
#endif //_OP_RANGES_PREDICATE_RANGE__H_
