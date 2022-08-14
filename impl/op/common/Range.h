#ifndef _OP_RANGE__H_
#define _OP_RANGE__H_

#ifdef _MSC_VER

#ifndef NOMINMAX
#define NOMINMAX 1
#endif //NOMINMAX
#endif //_MSC_VER

#include <set>
#include <op/common/Utils.h>
#include <algorithm>

#ifdef min
#undef min
#endif //min

#ifdef max
#undef max
#endif //max

namespace OP
{
    template <class T>
    struct RangeContainer;

    namespace zones
    {
        template <class R>
        inline auto pos(const R& r) -> decltype(r.pos())
        {
            return r.pos();
        }
        template <class R>
        inline auto count(const R& r) -> decltype(r.count())
        {
            return r.count();
        }
        template <class R>
        inline auto right(const R& r) -> decltype(pos(r))
        {
            return pos(r) + count(r);
        }
        /** Check that [ a ) & [ b ) is not empty */
        template <class R1, class R2>
        inline bool is_overlapping(const R1& a, const R2& b) 
        {
            return (right(a) > pos(b)) && (pos(a) < right(b));
        }
        /**Check if the `point` is inside the range [ a ) */
        template <class R>
        bool in(const R& a, decltype(pos(a)) check) 
        {
            return check >= pos(a) && check < right(a);
        }
        template <class R>
        bool empty(const R& a)
        {
            return !count(a);
        }
        /**Check if 'other' fully inside [ a ) */
        template <class R1, class R2>
        bool is_included(const R1& a, const R2& other)
        {
            return pos(a) <= pos(other) && right(other) <= right(a);
        }
        /**Operation is true if second range follows first without gap*/
        template <class R1, class R2>
        bool is_left_adjacent(const R2& a, const R2& other) 
        {
            return right(other) == pos(a);
        }
        /**Operation is true if first range follows second without gap*/
        template <class R1, class R2>
        bool is_right_adjacent(const R1& a, const R2& other) 
        {
            return right(a) == pos(other);
        }
        /**Operation is true if eaither right or left ajusted*/
        template <class R1, class R2>
        bool is_adjacent(const R1& a, const R2& other) 
        {
            return is_left_adjacent(a, other) || is_right_adjacent(a, other);
        }
        template <class R1, class R2>
        bool less(const R1& a, const R2& b)
        {
            return (pos(a) < pos(b)) && !(pos(b) < right(a));
        }
        template <class R1, class R2>
        bool equal (const R1& a, const R2& b)
        {
            return (pos(a) == pos(b)) && (count(a) == count(b));
        }
        template <class TZone>
        TZone unite_zones(const TZone& lar, const TZone& rar)
        {
            if(!lar.count())
                return rar;
            if(!rar.count())
                return lar;
            auto leftmost = std::min(zones::pos(lar), zones::pos(rar));
            return TZone(leftmost, 
                static_cast<typename TZone::distance_t>( 
                    std::max(zones::right(lar), zones::right(rar)) - leftmost));
        }
        template <class TZone>
        TZone join_zones(const TZone& lar, const TZone& rar)
        {
            auto leftmost_right = std::min(
                zones::right(lar), zones::right(rar));
            auto rightmost_left = std::max(
                zones::pos(lar), zones::pos(rar));
            return rightmost_left < leftmost_right 
                ? TZone(rightmost_left, static_cast<typename TZone::distance_t>(leftmost_right - rightmost_left))
                : TZone(rightmost_left, 0);
        }


    } //ns:zones

    template <class T, class Distance = unsigned int>
    struct Range 
    {
        typedef T pos_t;
        typedef Distance distance_t;
        typedef Range<T, Distance> this_t;
        friend struct RangeContainer<T>;

        Range() :_pos{}, _count(0){}

        Range(pos_t pos, Distance count) :
            _pos(pos), _count(count){}
        
        Range(this_t && other) OP_NOEXCEPT  = default;

        Range(const this_t & other) OP_NOEXCEPT = default;

        this_t& operator = (this_t&& right) OP_NOEXCEPT = default;

        ~Range() = default;

        bool is_overlapped(const Range& other) const
        {
            return zones::is_overlapping(*this, other);
        }
        
        bool in(pos_t check) const
        {
            return zones::in(_pos, check);
        }
        bool empty() const
        {
            return zones::empty(*this);
        }
        /**Check if 'other' fully inside this*/
        bool is_included(const Range& other) const
        {
            return zones::is_included(*this, other);
        }
        bool is_adjacent(const Range& other) const
        {
            return zones::is_adjacent(*this, other);
        }
        bool is_left_adjacent(const Range& other) const
        {
            return zones::is_left_adjacent(*this, other);
        }
        bool is_right_adjacent(const Range& other) const
        {
            return zones::is_right_adjacent(*this, other);
        }
        T right() const
        {
            return zones::right(*this);
        }
        template <class R>
        bool operator <(const R& right) const
        {
            return zones::less(*this, right);
        }
        template <class R>
        bool operator == (const R& right) const
        {
            return zones::equal(*this, right);
        }
        template <class R>
        bool operator != (const R& right) const
        {
            return !zones::equal(*this, right);
        }
        T pos() const
        {
            return _pos;
        }
        Distance count() const
        {
            return _count;
        }

    private:
        pos_t _pos;
        mutable Distance _count;
    };

    template <class T>
    struct RangeContainer
    {
        RangeContainer()
        {
        }
        /**Take smallest range from available*/
        T pull_range()
        {
            if (_free_ranges.empty())
                throw std::out_of_range("No ranges to allocate");
            //Range<T>& r = _free_ranges.begin();
            range_iter riter = _free_ranges.begin();
            
            auto result = riter->_pos + -- riter->_count;
            if (!riter->_count)
                _free_ranges.erase(_free_ranges.begin());
            return result;
        }
        /**Add range and if there is adjacent one then all ranges are combined together*/
        void add_range(T index)
        {
            Range<T> search(index, 1);
            auto result_pair = _free_ranges.insert(search);
            auto right = result_pair.first;
            ++right;
            if (right == _free_ranges.end())
                right = result_pair.first;
            auto begin = result_pair.first;
            if (begin != _free_ranges.begin()) //make step left
                --begin;
            optimize_left(begin, right);
        }
        size_t size() const
        {
            return _free_ranges.size();
        }
    private:
        typedef std::set< Range<T> > range_set_t;   
        typedef typename range_set_t::iterator range_iter;
        range_set_t _free_ranges;

        /**If possible merge together left & right and modifies */
        void optimize_left(range_iter& begin, range_iter& right)
        {
            range_iter left = right;
            --left;
            for (; right != begin; --left)
            {
                if (left->is_right_adjacent(*right))
                {
                    left->_count += right->_count;
                    _free_ranges.erase(right);
                }
                right = left;
            }
        }

    };
} //end of namespace OP
#endif //_OP_RANGE__H_
