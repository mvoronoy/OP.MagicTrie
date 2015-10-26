#ifndef _OP_RANGE__H_
#define _OP_RANGE__H_
#include <set>
#include <op/trie/Utils.h>
namespace OP
{
    template <class T>
    struct RangeContainer;
    template <class T, class Distance = unsigned int>
    struct Range
    {
        typedef T pos_t;
        typedef Range<T, Distance> this_t;
        friend struct RangeContainer<T>;

        Range() :_pos{}, _count(0){}

        Range(pos_t pos, Distance count) :
            _pos(pos), _count(count){}
        
        Range(this_t && other) OP_NOEXCEPT :
            _pos(other._pos),
            _count(other._count)
        {
        }
        Range(const this_t & other) OP_NOEXCEPT :
            _pos(other._pos),
            _count(other._count)
        {
        }

        ~Range() = default;

        bool is_overlapped(const Range& other) const
        {
            return (right() > other._pos) && (_pos < other.right());
        }
        /**Check if 'other' fully inside this*/
        bool is_included(const Range& other) const
        {
            return this->_pos <= other._pos && other.right() <= this->right();
        }
        bool is_adjacent(const Range& other) const
        {
            return is_left_adjacent(other) || is_right_adjacent(other);
        }
        bool is_left_adjacent(const Range& other) const
        {
            return other.right() == _pos;
        }
        bool is_right_adjacent(const Range& other) const
        {
            return right() == other._pos;
        }
        T right() const
        {
            return _pos + _count;
        }
        bool operator <(const Range<T>& right) const
        {
            return (this->_pos < right._pos) && !(right._pos < this->right());
        }
        bool operator == (const Range<T>& right) const
        {
            return (this->_pos == right._pos) && (this->_count == right._count);
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
            range_set_t::iterator right = result_pair.first;
            ++right;
            if (right == _free_ranges.end())
                right = result_pair.first;
            range_set_t::iterator begin = result_pair.first;
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
