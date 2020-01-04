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

        namespace details {
            
            struct bool_result
            {
                explicit bool_result(bool r)
                    : test_result(r) {}
                bool_result() = delete;
                bool operator () () const
                {
                    return test_result;
                }

                bool operator ! () const
                {
                    return !test_result;
                }

                operator bool() const
                {
                    return test_result;
                }

                const bool test_result;
            };

        } //ns:details

        template <class Marker, class Left, class ... Right>
        inline bool that(Left&& left, Right&& ... right)
        {
            return Marker::impl<Left, Right...>(std::forward<Left>(left), std::forward<Right>(right)...);
        }
        
        //
        //  Markers
        //
        
        //
        //  Marker:equals
        //
        /** Marker specifies equality operation between two arguments */
        struct equals
        {
            template <class Left, class Right>
            struct impl : details::bool_result
            {
                impl(Left left, Right right)
                    : details::bool_result(left == right)
                {
                }

            };
        };
        /** Marker specifies strictly less operation between two arguments */
        struct less
        {
            template <class Left, class Right>
            struct impl : details::bool_result
            {
                impl(Left left, Right right)
                    : details::bool_result(left < right)
                {
                }

            };
        };
        /** Marker specifies strictly greater operation between two arguments */
        struct greater
        {
            template <class Left, class Right>
            struct impl : details::bool_result
            {
                impl(Left left, Right right)
                    : details::bool_result(right < left) //note less is used!
                {
                }

            };
        };
        /** Using other marker invert (negate) semantic, for example:
        * \li that<not<equals>>(1, 2) ; //negate equality
        * \li that<not<less>>(1, 2) ; //negate less
        */
        template <class Marker>
        struct not
        {
            template < class ... Ts>
            struct impl : details::bool_result
            {
                impl(Ts&& ... ts)
                    : details::bool_result(! typename Marker::impl<Ts...>(std::forward<Ts>(ts)...))
                {
                }
            };
        };

        /**Marker around container_equals that can be used with assert_that<> */
        struct string_equals
        {
            template <class Str1, class Str2>
            struct impl : details::bool_result
            {
                
                impl(const Str1& left, const Str2& right)
                    : details::bool_result(left.compare(right) == 0)
                {
                }

            };
        };
    } //utest
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
