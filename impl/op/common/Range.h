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
        constexpr inline auto pos(const R& r)  noexcept -> decltype(r.pos())
        {
            return r.pos();
        }
        
        template <class R>
        constexpr inline auto count(const R& r) noexcept -> decltype(r.count())
        {
            return r.count();
        }

        template <class R>
        constexpr inline auto right(const R& r) noexcept -> decltype(pos(r))
        {
            return pos(r) + count(r);
        }

        /** Check that [ a ) & [ b ) is not empty */
        template <class R1, class R2>
        constexpr inline bool is_overlapping(const R1& a, const R2& b) noexcept
        {
            return (right(a) > pos(b)) && (pos(a) < right(b));
        }

        /**Check if the `point` is inside the range [ a ) */
        template <class R>
        constexpr bool in(const R& a, decltype(pos(a)) check) noexcept
        {
            return check >= pos(a) && check < right(a);
        }

        template <class R>
        constexpr bool empty(const R& a) noexcept
        {
            return !count(a);
        }

        /**Check if 'other' fully inside [ a ) */
        template <class R1, class R2>
        constexpr bool is_included(const R1& a, const R2& other) noexcept
        {
            return pos(a) <= pos(other) && right(other) <= right(a);
        }

        /**Operation is true if second range follows first without gap*/
        template <class R1, class R2>
        constexpr bool is_left_adjacent(const R2& a, const R2& other) noexcept
        {
            return right(other) == pos(a);
        }

        /**Operation is true if first range follows second without gap*/
        template <class R1, class R2>
        constexpr bool is_right_adjacent(const R1& a, const R2& other) noexcept 
        {
            return right(a) == pos(other);
        }

        /**Operation is true if either right or left adjusted */
        template <class R1, class R2>
        constexpr bool is_adjacent(const R1& a, const R2& other) noexcept
        {
            return is_left_adjacent(a, other) || is_right_adjacent(a, other);
        }

        template <class R1, class R2>
        constexpr bool less(const R1& a, const R2& b) noexcept
        {
            return (pos(a) < pos(b)) && !(pos(b) < right(a));
        }

        template <class R1, class R2>
        constexpr bool equal (const R1& a, const R2& b) noexcept
        {
            return (pos(a) == pos(b)) && (count(a) == count(b));
        }

        template <class TZone>
        constexpr TZone unite_zones(const TZone& lar, const TZone& rar) noexcept
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
        constexpr TZone join_zones(const TZone& lar, const TZone& rar) noexcept
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

    struct Abs {};

    template <class T, class Distance = unsigned int>
    struct Range 
    {
        typedef T pos_t;
        typedef Distance distance_t;
        typedef Range<T, Distance> this_t;
        friend struct RangeContainer<T>;

        constexpr Range() noexcept
            : _pos{}
            , _count(0)
        {}

        constexpr Range(pos_t pos, Distance count) noexcept
            : _pos(pos)
            , _count(count)
        {}

        constexpr Range(Abs, pos_t pos, pos_t right) noexcept
            : _pos(pos)
            , _count(right - pos)
        {
            assert(right >= pos);
        }
        
        constexpr Range(this_t && other) noexcept = default;

        constexpr Range(const this_t & other) noexcept = default;

        constexpr this_t& operator = (this_t&& right) noexcept = default;

        ~Range() = default;

        constexpr bool is_overlapped(const Range& other) const noexcept
        {
            return zones::is_overlapping(*this, other);
        }
        
        constexpr bool in(pos_t check) const noexcept
        {
            return zones::in(_pos, check);
        }

        constexpr bool empty() const noexcept
        {
            return zones::empty(*this);
        }

        /**Check if 'other' fully inside this*/
        constexpr bool is_included(const Range& other) const noexcept
        {
            return zones::is_included(*this, other);
        }

        constexpr bool is_adjacent(const Range& other) const noexcept
        {
            return zones::is_adjacent(*this, other);
        }

        constexpr bool is_left_adjacent(const Range& other) const noexcept
        {
            return zones::is_left_adjacent(*this, other);
        }

        constexpr bool is_right_adjacent(const Range& other) const noexcept
        {
            return zones::is_right_adjacent(*this, other);
        }

        constexpr T right() const noexcept
        {
            return zones::right(*this);
        }

        template <class R>
        constexpr bool operator <(const R& right) const noexcept
        {
            return zones::less(*this, right);
        }

        template <class R>
        constexpr bool operator == (const R& right) const noexcept
        {
            return zones::equal(*this, right);
        }

        template <class R>
        constexpr bool operator != (const R& right) const noexcept
        {
            return !zones::equal(*this, right);
        }

        constexpr T pos() const noexcept
        {
            return _pos;
        }

        constexpr Distance count() const noexcept
        {
            return _count;
        }

    private:
        pos_t _pos;
        Distance _count;
    };

} //end of namespace OP
namespace std
{
    template <class T, class Distance>
    struct hash<OP::Range<T, Distance>>
    {
        constexpr std::size_t operator()(OP::Range<T, Distance> const& r) const noexcept
        {
            return r.pos() * 101 + r.count();
        }
    };
}//ns:std
#endif //_OP_RANGE__H_
