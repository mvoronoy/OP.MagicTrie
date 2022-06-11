#ifndef _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
#define _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1

#include <string>
#include <typeinfo>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>

namespace OP::utest
{

    namespace details {

        /** Declare arity of operation */
        template <size_t N>
        struct marker_arity
        {
            constexpr static size_t args_c = N;
        };

        /** Taking tuple U applies arguments to functor Marker in range [from 0 to ... I)*/
        template <class Marker, class U, size_t ... I>
        bool apply_prefix(Marker& f, U& u, std::index_sequence<I...>)
        {
            return std::invoke(f, std::get<I>(u)...);
        }
        /** Taking tuple U applies argument to functor Marker starting from [I+1 to ... tuple_size(U))*/
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

    /**
    * Marker to compare 2 heterogenous container with items supported operator `==`.
    * Method assumes strict order checking of both sequences.
    * Complexity is about O(min(N, M))
    */
    struct eq_sets
    {
        constexpr static size_t args_c = 2;

        template <class Left, class Right>
        constexpr bool operator()(const Left& left, const Right& right) const
        {
            auto end_left = std::end(left);
            auto end_right = std::end(right);

            auto pr = std::mismatch(std::begin(left), end_left, std::begin(right), end_right);
            return pr.first == end_left && pr.second == end_right;
        }
    };
    namespace details
    {
        //from https://stackoverflow.com/questions/12753997/check-if-type-is-hashable
        template <typename T, typename = std::void_t<>>
        struct is_std_hashable : std::false_type { };

        template <typename T>
        struct is_std_hashable<T, std::void_t<decltype(std::declval<std::hash<T>>()(std::declval<T>()))>> : std::true_type { };

        template <typename T>
        constexpr bool is_std_hashable_v = is_std_hashable<T>::value;

    }
    /**
    * Marker to compare 2 heterogeneous container with items supported operator `==` and
    * at least one must support std::hash.
    * Strict order checking is not needed
    * Complexity is about O(N) + O(M)
    */
    struct eq_unordered_sets
    {
        constexpr static size_t args_c = 2;

        template <class Left, class Right>
        constexpr bool operator()(const Left& left, const Right& right) const
        {
            auto end_left = std::end(left);
            auto end_right = std::end(right);
            using element_left_t = std::decay_t<decltype(*end_left)>;
            using element_right_t = std::decay_t<decltype(*end_right)>;

            static_assert(
                details::is_std_hashable_v<element_left_t> ||
                details::is_std_hashable_v<element_right_t>,
                "At least one container must contain hash-able item");

            auto drain_all_items = [](auto& hashed, const auto& seq) {
                for (const auto& sequence_item : seq)
                {
                    if (hashed.erase(sequence_item) != 1)
                        return false;
                }
                return hashed.empty();
            };
            if constexpr (details::is_std_hashable_v<element_left_t>)
            {
                std::unordered_multiset< element_left_t> hashed(
                    std::begin(left), end_left);
                return drain_all_items(hashed, right);
            }
            else
            {
                std::unordered_multiset< element_right_t> hashed(
                    std::begin(right), end_right);
                return drain_all_items(hashed, left);
            }
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
}//OP
#endif //_UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
