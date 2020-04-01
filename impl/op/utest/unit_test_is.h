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
                constexpr explicit bool_result(bool r)
                    : test_result(r) 
                {
                }
                
                bool_result() = delete;

                constexpr bool operator () () const
                {
                    return test_result;
                }

                constexpr bool operator ! () const
                {
                    return !test_result;
                }

                constexpr operator bool() const
                {
                    return test_result;
                }

                const bool test_result;
            };

        } //ns:details

        template <class Marker, class ... T>
        inline bool that(T&& ... right)
        {
            return Marker(std::forward<T>(right)...);
        }
        
        //
        //  Markers
        //
        
        //
        //  Marker:equals
        //
        /** Marker specifies equality operation between two arguments */
        struct equals : public details::bool_result
        {
            template <class Left, class Right>
            constexpr equals(Left left, Right right)
                    : details::bool_result(left == right)
                {
                }
        };
        /** Marker specifies strictly less operation between two arguments */
        struct less : public details::bool_result
        {
            template <class Left, class Right>
            constexpr less(Left left, Right right)
                    : details::bool_result(left < right)
                {
                }
        };
        /** Marker specifies strictly greater operation between two arguments */
        struct greater : public details::bool_result
        {
            template <class Left, class Right>
            constexpr greater(Left left, Right right)
                    : details::bool_result(right < left) //note less is used!
                {
                }
        };
        /** Marker specifies strictly less operation between two arguments */
        struct is_null : public details::bool_result
        {
            template <class Inst>
            constexpr is_null(Inst inst)
                : details::bool_result(inst == nullptr)
            {
            }
        };

        //--------------------------------------
        /** Using other marker invert (negate) semantic, for example:
        * \li that<not<equals>>(1, 2) ; //negate equality
        * \li that<not<less>>(1, 2) ; //negate less
        */
        template <class Marker>
        struct negate : public details::bool_result
        {
            template < class ... Ts>
            constexpr negate(Ts&& ... ts)
                    : details::bool_result(!Marker(std::forward<Ts>(ts)...))
            {
            }
        };
        template <class Marker>
        using logical_not = negate<Marker>;

        /**Marker around container_equals that can be used with assert_that<> */
        struct string_equals : public details::bool_result
        {
            template <class Str1, class Str2>
            constexpr string_equals(const Str1& left, const Str2& right)
                : details::bool_result(left.compare(right) == 0)
            {
            }
        };
    } //utest
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
