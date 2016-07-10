#ifndef _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
#define _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1

#include <string>
#include <typeinfo>
#include <functional>
#include <map>
#include <set>

namespace OP
{
    namespace utest{
        /** Marker specifies equality operation between two arguments */
        struct equals {};
        namespace details {
            
            struct bool_result
            {
                bool_result(bool r)
                    : test_result(r) {}
                const bool test_result;
            };

            template <class Left, class Right, class Rule>
            struct that : bool_result
            {

            };
            template <class Left, class Right>
            struct that<Left, Right, equals> : bool_result
            {
                that(Left&& left, Right&& right)
                    : bool_result(left == right)
                {
                }
            };
            template <>
            struct that<const char*, const char*, equals> : bool_result
            {
                that(const char* left, const char* right)
                    : bool_result(strcmp(left, right) == 0)
                {
                }
            };
        } //ns:details

        template <class Marker, class Left, class Right>
        inline bool that(Left&& left, Right&& right)
        {
            return details::that<Left, Right, Marker>(std::forward<Left>(left), std::forward<Right>(right)).test_result;
        }

        template <class Left, class Right, class Comparator>
        inline bool that(Left&& left, Right&& right, Comparator & cmp)
        {
            return cmp(std::forward<Left>(left), std::forward<Right>(right));
        }

        
    } //utest
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
