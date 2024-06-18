#ifndef _OP_COMMON_LEXCOMPARATOR__H_
#define _OP_COMMON_LEXCOMPARATOR__H_

namespace OP::common
{
    /** 
    * \brief Implements lexical comparison of two string-like sequences represented by pairs of iterators.
    *
    * \param begin_left Begin of the first sequence; this iterator at exit points to the entry of mismatch or equals to `end_left`.
    * \param end_left End of the first sequence.
    * \param begin_right Begin of the second sequence; this iterator at exit points to the entry of mismatch or equals to `end_right`.
    * \param end_right End of the second sequence.
    *
    * \return:
    * - `== 0` if the left range equals the right range;
    * - `< 0` if the left range is lexicographically less than the right range;
    * - `> 0` if the left range is lexicographically greater than the right range.
    * 
    * \tparam I1 iterator type of the first sequence. Must support operators `++`(prefix), `!=` and `*` (dereference).
    * \tparam I2 iterator type of the second sequence. Must support operators `++`(prefix), `!=` and `*` (dereference).
    */
    template <class I1, class I2>
    inline int str_lexico_comparator(
        I1& begin_left, const I1& end_left,
        I2& begin_right, const I2& end_right)
    {
        for (; begin_left != end_left && begin_right != end_right; ++begin_left, ++begin_right)
        {
            int r = static_cast<int>(static_cast<unsigned>(*begin_left)) -
                static_cast<int>(static_cast<unsigned>(*begin_right));
            if (r != 0)
                return r;
        }
        if (begin_left != end_left)
        { //left is longer
            return 1;
        }
        return begin_right == end_right ? 0 : -1;
    }

} //ns: OP::ranges
#endif //_OP_COMMON_LEXCOMPARATOR__H_
