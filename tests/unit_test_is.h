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
                    : test_that(r) {}
                const bool test_that;
            };

            template <class Left, class Right, class Rule>
            struct that : bool_result
            {
                that(Left&& left, Right&& right)
                    : bool_result(false)
                {
                }

            };
            template <class Left, class Right>
            struct that<Left, Right, equals> : bool_result
            {
                that(Left&& left, Right&& right)
                    : bool_result(left == right)
                {
                }
            };
        }

        template <class Marker, class Left, class Right>
        inline bool that(Left&& left, Right&& right)
        {
            return details::that<Left, Right, Marker>(std::forward<Left>(left), std::forward<Right>(right));
        }

        inline std::function< bool() > equals(
                const char* t1, 
                const char* t2)
            {
                return [t1, t2](){
                    return strcmp(t1, t2) == 0;
                };
            }
            inline std::function< bool() > equals(
                const std::uint8_t* t1, 
                const std::uint8_t* t2,
                size_t size)
            {
                auto f = [t1, t2, size](){
                    return memcmp(t1, t2, size) == 0;
                };
                return f;
            }
            template <class T1, class T2>
            inline std::function< bool() > less(
                const T1& t1, 
                const T2& t2)
            {
                return [t1, t2](){
                    return t1 < t2;
                };
            }

            inline std::function< bool() > less(
                const char* t1, 
                const char* t2)
            {
                return [t1, t2](){
                    return strcmp(t1, t2) < 0;
                };
            }
        }
        
    } //utest
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
