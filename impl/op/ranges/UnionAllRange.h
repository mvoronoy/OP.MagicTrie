#ifndef _OP_RANGES_UNION_ALL_RANGE__H_
#define _OP_RANGES_UNION_ALL_RANGE__H_
#include <iterator>
#include <functional>
#include <op/ranges/PrefixRange.h>

#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

namespace OP
{
    namespace ranges
    {
        
        template <class OwnerRange>
        struct UnionAllRangeIteratorPayload : public OwnerRange::iterator::RangeIteratorImpl
        {
            friend OwnerRange;
            using range_iterator = typename OwnerRange::iterator;
            using base_t = typename range_iterator::RangeIteratorImpl;
            using key_t = typename OwnerRange::key_t;
            using value_t = typename OwnerRange::value_t;
            using united_ranges_t = std::vector<
                std::shared_ptr< RangeBase<key_t, value_t> const >
            >;
            UnionAllRangeIteratorPayload(united_ranges_t ranges,
                range_iterator actual)
                : _ranges(std::move(ranges))
                , _actual(actual)
            {}
            virtual const key_t& key() const
            {
                return _actual.key();
            }
            virtual const value_t& value() const
            {
                return _actual.value();
            }
            virtual std::unique_ptr<base_t> clone() const
            {
                return std::unique_ptr<base_t>(new UnionAllRangeIteratorPayload(_ranges, _actual));
            }
        protected:
            void seek_next()
            {
                if(_ranges.empty())
                    return;
                _ranges.back()->next(_actual);
                if (!_ranges.back()->in_range(_actual))
                {
                    _ranges.pop_back();
                    if(!_ranges.empty())
                        _actual = std::move(_ranges.back()->begin());
                }
            }
        private:
            united_ranges_t _ranges;
            range_iterator _actual;
        };

        template <class SourceRange>
        struct UnionAllRange : public RangeBase<typename SourceRange::key_t, typename SourceRange::value_t>
        {
            using this_t = UnionAllRange<SourceRange>;
            using base_t = RangeBase<typename SourceRange::key_t, typename SourceRange::value_t>;
            using range_ptr = typename base_t::range_ptr;
            using iterator = typename base_t::iterator;
            using key_t = typename base_t::key_t;
            using value_t = typename base_t::value_t;

            using united_ranges_t = std::vector<range_ptr>;
            using payload_t = UnionAllRangeIteratorPayload< this_t >;

            UnionAllRange(united_ranges_t ranges)
                : _ranges(std::move(ranges))
            {
            }
            UnionAllRange() = delete;
            iterator begin() const override
            {
                united_ranges_t actual_ranges; 
                actual_ranges.reserve(_ranges.size());
                //start copy from non-empty range
                std::copy_if(std::begin(_ranges), std::end(_ranges), std::back_inserter(actual_ranges),
                    [](const auto& i) { return !i->empty(); });
                
                std::unique_ptr<payload_t> payload;
                if(!actual_ranges.empty())
                {
                    auto beg = actual_ranges.back()->begin();
                    payload = std::unique_ptr<payload_t> (
                            new payload_t(std::move(actual_ranges), std::move(beg)));
                };
                iterator result(
                    std::const_pointer_cast<range_t const>(shared_from_this()), 
                    std::move(payload));
                return result;
            }
            bool in_range(const iterator& check) const override
            {
                if(!check)
                    return false;
                const auto& pload = check.impl<payload_t>();
                return !pload._ranges.empty();
            }
            void next(iterator& pos) const override
            {
                if(!pos)
                    return;
                auto& pload = pos.impl<payload_t>();
                pload.seek_next();
            }
        private:
            united_ranges_t _ranges;
        };

    } //ns: ranges
} //ns: OP
#endif //_OP_RANGES_UNION_ALL_RANGE__H_

