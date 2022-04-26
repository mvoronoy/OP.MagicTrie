#ifndef _OP_RANGES_LEXCOMPARATOR__H_
#define _OP_RANGES_LEXCOMPARATOR__H_

namespace OP::ranges
{
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

} //ns: OP::ranges
#endif //_OP_RANGES_LEXCOMPARATOR__H_
