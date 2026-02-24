#pragma once

#ifndef _OP_COMMON_BITBOXRANGECONTAINER__H_
#define _OP_COMMON_BITBOXRANGECONTAINER__H_

#include <array>
#include <vector>
#include <optional>

#include <op/common/Bitset.h>
#include <op/common/Range.h>

namespace OP::zones
{
/**
* Problem of effective range intersection  
* Any range[pos=uint64, size=uint32] (assuming range.right < 2^64-1) can be mapped to bit mask, where 1 corresponds
* to part of the range intersection with numeric region. For example:
    [0x2, size:0x3] masked by 0b0110;
    [0x2, size:0x4] masked by 0b1110;
    [0x0, size:0x10], masked by 0b11111;
General formula to evaluate mask: (size | ((1 << log2(size))-1)) << log2(pos).
Such index of allows easily find intersection with other ranges (by & operator).
Representable range allows operate on smaller boundary (depends on width of range, for example
count = 0x1000 means addressing 0..2^51)

It has near the same interface as SpanContainer and near the same performance. May be reviewed for MT tasks
*/
template <class TRange, class TPayload>
struct BitboxRangeContainer
{
    using key_t = TRange;
    using value_t = TPayload;

    BitboxRangeContainer()
    {
        //_box_items.reserve(10240);
    }

    void clear()
    {
        _index = 0;
        _box_items.clear();
        for (auto& ent : _index_map)
            ent.reset();
        _timeline.clear();
    }

    template <class ...TArgs>
    void emplace(TArgs&&... args)
    {
        auto ref_idx = _timeline.size();
        _timeline.emplace_back(std::forward<TArgs>(args)...);
        insert_bucket(ref_idx);
    }

    /** for each stored ranges evaluate if `query` parameter intersect any 
    of this. Complexity for search almost constant, so having M intersections
    we iterate exactly M times. 
    \param query range to search
    \param callback user provided callback with the 2 possible signatures:
        \li `bool(const TRange&, const TPayload&)` - when you need stop iteration, callback must return false;
        \li `void(const TRange&, const TPayload&)` - you iterate all items;

    \return number of processed items
    */
    template <class TCallback>
    size_t intersect_with(const TRange& query, TCallback callback) const
    {
        auto [r_start, r_cont] = mask(query);
        auto flags = r_cont << r_start;
        decltype(flags) old_flags = 0; //allows understand when range has already been reviewed
        std::uint_fast64_t count = 0;
        for (auto i = r_start; flags & _index; ++i)
        {
            const auto& slot = _index_map[i];
            assert(slot); //must not be empty
            for (auto range_i = std::get<0>(*slot); ; range_i = at_box(range_i)._prev)
            {
                const auto& entry = _timeline[at_box(range_i)._payload];
                auto [entry_start, entry_cont] = mask(entry.first);
                auto entry_flag = entry_cont << entry_start;
                if (!(entry_flag & old_flags) //check we don't review this range before
                    && entry.first.is_overlapped(query))
                {
                    ++count;
                    using callback_res_t = decltype(callback(entry.first, entry.second));
                    if constexpr (std::is_convertible_v<bool, callback_res_t>)
                    {//function returns bool
                        if (!callback(entry.first, entry.second))
                            return count;
                    }
                    else//don't care about return type
                        callback(entry.first, entry.second);
                }
                if (range_i == std::get<1>(*slot))
                    break;
            }
            old_flags = flags & (1ull << i);
            flags &= ~(1ull << i);
        }
        return count;
    }

private:
    using element_t = std::pair<TRange, TPayload>;
    using slot_element_t = size_t;
    using slot_box_t = std::pair<slot_element_t, slot_element_t>;
    /* Represent forward-list element */
    struct BoxItem
    {
        /** position inside _box_item vector */
        size_t _prev;
        /** position in _timeline vector */
        size_t _payload;
    };
    /** represent no-position for forward-list */
    constexpr static size_t npos = ~size_t{};
    /** Generic mask, that with help of bit operations check of slot availability */
    std::uint64_t _index = 0;
    std::vector<element_t> _timeline;
    /** Maps range and payload to one of the 64 slots addressed by log2 */
    std::array<std::optional<slot_box_t>, 64> _index_map;
    /** implement forward-list on top of vector to reduce number of
    * memory alloc/dealloc, connectivity supported by absolute index */
    std::vector<BoxItem> _box_items;
    
    /** Put element to the forward_lis */
    size_t put_box(size_t prev, size_t payload)
    {
        size_t result = _box_items.size();
        if (prev != npos)
            _box_items[prev]._prev = result;
        _box_items.emplace_back(BoxItem{ npos, payload });
        return result;
    }

    const BoxItem& at_box(size_t index) const
    {
        return _box_items[index];
    }

    void insert_bucket(size_t ref_idx)
    {
        auto [r_start, r_cont] = mask(_timeline[ref_idx].first);
        //[debug]std::cout << std::hex << "start=0x" << r_start << ", contin=0x" << r_cont 
        //     << ", fold=0x" << (flags)
        //     << "\n";

        _index |= (r_cont << r_start);
        for (; r_cont; r_cont >>= 1, ++r_start)
        {
            //[debug]std::cout << "proc-step:" << r_start << "\n";
            auto& bucket = _index_map[r_start];
            if (!bucket)
            {
                auto ins_pos = put_box(npos, ref_idx);
                _index_map[r_start].emplace(ins_pos, ins_pos);
            }
            else
            {
                bucket->second = put_box(bucket->second, ref_idx);
            }
        }
    }
    /** evaluate binary mask for specific range 
    * \return pair of numbers. First is number of bits to shift binary mask to intersect 
        with _index. The second is mask that matches continuation of TRange.
    */
    static constexpr auto mask(const TRange& r) noexcept
    {
        auto bit_width = OP::common::log2(r.count()) + 1;
        return std::make_pair(
            OP::common::log2(r.pos()),
            ((1ull << bit_width) - 1)
        );
    }

    

};

}//ns:OP::zones
#endif //_OP_COMMON_BITBOXRANGECONTAINER__H_
