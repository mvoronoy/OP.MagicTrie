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
            
            /** Declare arity of operation */
            template <size_t N>
            struct marker_arity
            {
                constexpr static size_t args_c = N;
            };

            /**Takin tuple U applies arguments to functor Marker in range [from 0 to ... I)*/
            template <class Marker, class U, size_t ... I>
            bool apply_prefix(Marker& f, U& u, std::index_sequence<I...>)
            {
                return std::invoke(f, std::get<I>(u)...);
            }
            /**Takin tuple U applies argument to functor Marker starting from [I+1 to ... tuple_size(U))*/
            template <size_t prefix_size_c, class Marker, class U, size_t ... I>
            void apply_rest(Marker& f, U& u, std::index_sequence<I...>)
            {
                std::invoke(f, std::get<I + prefix_size_c>(u)...);
            }


        } //ns:details

        //
        //  Markers
        //
        
        /** Using other marker invert (negate) semantic, for example:
        * \li that<not<equals>>(1, 2) ; //negate equality eg. !=
        * \li that<not<less>>(1, 2) ; //negate less eg. >=
        */
        template <class Marker>
        struct negate : details::marker_arity<Marker::args_c>
        {
            Marker m;
            template <class ...Args>
            constexpr bool operator() (Args&& ... x)  const
            {
                return !m(std::forward<Args>(x)...);
            }
        };
        template <class Marker>
        using logical_not = negate<Marker>;

        /** Marker specifies equality operation between two arguments */
        struct equals : details::marker_arity<2>
        {
            template <class Left, class Right>
            constexpr bool operator()(Left left, Right right)  const
            {
                return left == right;
            }
        };

        /** Marker to compare 2 heterogenous container with items supported operator `==` */
        struct eq_sets
        {
            constexpr static size_t args_c = 2;
            using is_transparent = int;

            template <class Left, class Right>
            constexpr bool operator()(const Left& left, const Right& right) const
            {
                auto pr = std::mismatch(left.begin(), left.end(), right.begin(), right.end());
                return pr.first == left.end() && pr.second == right.end();
            }
        };

        /** alias for negate<equals> */
        using not_equals = negate<equals>;

        /** Marker specifies strictly less operation between two arguments */
        struct less : details::marker_arity<2>
        {
            template <class Left, class Right>
            constexpr bool operator()(Left left, Right right)  const
            {
                return left < right;
            }
        };
        /** Alias for negate<less> */
        using greater_or_equals = negate<less>;

        /** Marker specifies strictly greater operation between two arguments */
        struct greater : details::marker_arity<2>
        {
            template <class Left, class Right>
            constexpr bool operator()(Left left, Right right)  const
            {
                return right < left;
            }
        };
        /** Alias for negate<greater> */
        using less_or_equals = negate<greater>;

        /** Marker specifies strictly less operation between two arguments */
        struct is_null : details::marker_arity<1>
        {
            template <class Inst>
            constexpr bool operator()(Inst inst) const
            {
                return inst == nullptr;
            }
        };
        /** Alias for negate<is_null> */
        using is_not_null = negate<is_null>;

        /**
        *  Marker allows intercept exception of specified type. Result assert_that will fail if 
        * exception is not raised
        * 
        * \tparam ToCatch - type of exception to catch
        */
        template <class ToCatch>
        struct raise_exception : details::marker_arity<1>
        {
            /** \tparam F functor that supposed to raise the exception */
            template <class F>
            bool operator()(F f) const
            {
                try
                {
                    f();
                }
                catch (const ToCatch& ex)
                {
                    return true;
                }
                return false;
            }
        };
    } //utest
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
