#ifndef _TEST_COMPARATORS__H_
#define _TEST_COMPARATORS__H_

#include <string>
template <class Left, class Right>
inline typename std::enable_if<!std::is_same<Left, Right>::value, bool>::type 
operator == (const std::basic_string<Left>& left,const std::basic_string<Right>& right)
{
    auto li = left.begin();
    auto ri = right.begin();
    for(; li != left.end() && ri != right.end(); ++li, ++ri)
    {
        if( *li != *ri )
            return false;
    }
    return li == left.end() && ri == right.end();
}



#endif //_TEST_COMPARATORS__H_
