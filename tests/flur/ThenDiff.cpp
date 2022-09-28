#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

#include <op/flur/Diff.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

namespace {
    void test_SmallDup(OP::utest::TestRuntime& tresult)
    {
        std::multiset<std::string> test_multi_seq{ "bb", "bb", "bb" };
        std::multiset<std::string> mul_sub = { "bb", "bb" };
        std::multiset<std::string> expected;
        std::set_difference(test_multi_seq.begin(), test_multi_seq.end(), mul_sub.begin(), mul_sub.end(),
            std::inserter(expected, expected.end()));

        auto r_seq = src::of_container(/*copy*/test_multi_seq);
        auto flur_sub = src::of_container(/*copy*/mul_sub);

        auto res1 = r_seq >> then::diff(flur_sub);

        tresult.assert_true(res1.compound().is_sequence_ordered());

        tresult.assert_that<eq_sets>(
            res1, expected);

    }

    void test_ThenOrderedDifference(OP::utest::TestRuntime& tresult)
    {
        std::multiset<std::string> test_multi_seq {"aa", "aa", "bb", "bb", "bb", "c", "xx", "xx"};
        std::multiset<std::string> mul_sub = {"aaa", "a", "bb", "bb", "d", "x", "z"};
        std::multiset<std::string> expected;
        std::set_difference(test_multi_seq.begin(), test_multi_seq.end(), mul_sub.begin(), mul_sub.end(),
            std::inserter(expected, expected.end()));

        auto r_seq = src::of_container(/*copy*/test_multi_seq);
        auto flur_sub = src::of_container(/*copy*/mul_sub);
    
        auto res1 = r_seq >> then::diff(flur_sub);

        tresult.assert_true(res1.compound().is_sequence_ordered());

        tresult.assert_that<eq_sets>(
            res1, expected);

    }


static auto& module_suite = OP::utest::default_test_suite("flur.then")
    .declare("small-dup", test_SmallDup)
    .declare("ordered-diff", test_ThenOrderedDifference)
;

}