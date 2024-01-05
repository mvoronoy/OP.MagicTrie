#ifndef _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
#define _UNIT_TEST_IS__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1

#include <string>
#include <typeinfo>
#include <iterator>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>
#include <op/utest/details.h>
#include <op/common/has_member_def.h>

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
        auto apply_prefix(Marker& f, U& u, std::index_sequence<I...>)
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

    namespace hop = OP::has_operators;
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
        constexpr auto operator() (Args&& ... x)  const
        {
            auto res = m(std::forward<Args>(x)...);
            if constexpr (std::is_convertible_v<std::decay_t<decltype(res)>, bool>)
            {//no additional info provided
                return !res;
            }
            else
            { //previous check provides some details
                std::get<bool>(res) = !std::get<bool>(res);
                return res;
            }
        }
    };

    template <class Marker>
    using logical_not = negate<Marker>;

    /** Marker specifies equality operation between two arguments */
    struct equals : details::marker_arity<2>
    {
        template <class Left, class Right>
        auto operator()(Left&& left, Right&& right) const
        {
            bool result = left == right;
            if constexpr (hop::ostream_out_v<Left> && hop::ostream_out_v<Right>)
            {
                if (result)
                    return std::make_pair( true, OP::utest::Details{} );
                OP::utest::Details fail;
                fail << "Assertion of equality check: [" << left << "] vs [" << right << "]\n";
                return std::make_pair( false, std::move(fail));
            }
            else
            {
                return result;
            }
        }
    };

    /** Marker specifies equality operation between two numeric with some platform specific precision (see 
    *   std::numeric_limits::epsilon() for details)
    */
    struct almost_eq : details::marker_arity<2>
    {
        /** 
        * \tparam ulp_c units in the last place
        */ 
        template <class Left, class Right>
        auto operator()(Left&& left, Right&& right) const
        {
            using wide_num_t = decltype(left - right);
            wide_num_t abs_diff = std::abs(left - right);
            constexpr wide_num_t eps = std::numeric_limits<wide_num_t>::epsilon();
            bool result = abs_diff <= eps
                || abs_diff <= eps * std::max( wide_num_t{1}, std::max(std::abs(left), std::abs(right) ))
                //result is subnormal
                || abs_diff < std::numeric_limits<wide_num_t>::min()
                ;

            if (result)
                return std::make_pair( true, OP::utest::Details{} );
            OP::utest::Details fail;
            fail << "Assertion of equality check: [" << left << "] vs [" << right << "]\n";
            return std::make_pair( false, std::move(fail));
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

        template <class T>
        using el_t = decltype(*std::begin(std::declval<const T&>()));

        template <class Left, class Right>
        static bool constexpr can_print_details_c = hop::ostream_out_v<el_t<Left>> && hop::ostream_out_v<el_t<Right>>;
        /** Implementation finds mismatch in 2 sets of the same order for data types
        * that has no ostream operator `<<`
        */ 
        template <class Left, class Right>
        constexpr auto operator()(const Left& left, const Right& right) const
        {
            auto end_left = std::end(left);
            auto end_right = std::end(right);
            using left_elt_t = std::decay_t<decltype(*end_left)>;
            using right_elt_t = std::decay_t<decltype(*end_right)>;

            if constexpr (hop::ostream_out_v<left_elt_t> && hop::ostream_out_v<right_elt_t>)
            { // can improve output by adding Fault explanation
                return with_details(left, right);
            }
            else
            {
                auto [left_msm, right_msm] = std::mismatch(
                    std::begin(left), end_left,
                    std::begin(right), end_right);

                return left_msm == end_left && right_msm == end_right;
            }
        }
    private:
        /** Implementation finds mismatch in 2 sets of the same order for data types
        * with ostream operator `<<` support.
        */
        template <class Left, class Right >
        std::pair<bool, OP::utest::Details> with_details(const Left& left, const Right& right) const
        {
            auto end_left = std::end(left);
            auto end_right = std::end(right);

            auto [left_msm, right_msm] = std::mismatch(
                std::begin(left), end_left,
                std::begin(right), end_right);

            if (left_msm == end_left && right_msm == end_right)
                return std::make_pair(true, OP::utest::Details{});

            OP::utest::Details fail;
            fail << "Assertion of set-equality check: Left still contains: [";
            for (size_t i = 0; left_msm != end_left; ++left_msm, ++i)
                fail << (i ? ", " : "") << *left_msm;
            fail << "] vs Right still contains[";
            for (size_t i = 0; right_msm != end_right; ++right_msm, ++i)
                fail << (i ? ", " : "") << *right_msm;
            fail << "]\n";
            return std::make_pair(false, std::move(fail));
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
                    auto found = hashed.find(sequence_item);
                    if (found == hashed.end() )
                        return false;
                    hashed.erase(found);
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
    * Marker specifies matching (in sense of `std::regex_match`) string to the regular 
    * expression, to use it one of the `assert_that` must be std::regex
    */
    struct regex_match : details::marker_arity<2>
    {
        template <class Left, class Right>
        auto operator()(Left&& left, Right&& right) const
        {
            static_assert(
                std::is_same_v<std::regex, std::decay_t<Left>>
                || std::is_same_v<std::regex, std::decay_t<Right>>,
                "one of the `assert_that<regex_match>` must be std::regex");

            if constexpr (std::is_same_v<std::regex, std::decay_t<Left>>)
            {
                return std::regex_match(right, left);
            }
            else //if constexpr (std::is_same_v<std::regex, std::decay_t<Left>>)
            {
                return std::regex_match(left, right);
            }
            
        }
    };
    /**
    * Marker specifies matching (in sense of `std::regex_search`) string to the regular
    * expression, to use it one of the `assert_that` must be std::regex
    */
    struct regex_search : details::marker_arity<2>
    {
        template <class Left, class Right>
        auto operator()(Left&& left, Right&& right) const
        {
            static_assert(
                std::is_same_v<std::regex, std::decay_t<Left>>
                || std::is_same_v<std::regex, std::decay_t<Right>>,
                "one of the `assert_that<regex_match>` must be std::regex");

            if constexpr (std::is_same_v<std::regex, std::decay_t<Left>>)
            {
                return std::regex_search(right, left);
            }
            else //if constexpr (std::is_same_v<std::regex, std::decay_t<Left>>)
            {
                return std::regex_search(left, right);
            }

        }
    };

    /**
    *  Marker allows intercept exception of specified type. Result assert_that will fail if
    * exception is not raised. It is simplified approach similar to TestResul::assert_exception
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
