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
    
        auto res1 = r_seq >> then::diff(src::of_container(/*copy*/mul_sub));

        tresult.assert_true(res1.compound().is_sequence_ordered());
        tresult.assert_that<eq_sets>(
            res1, expected);

        tresult.info() << "Test empties...\n";

        std::multiset<std::string> empty;
        tresult.assert_that<eq_sets>(
            r_seq >> then::diff(src::of_container(empty)), test_multi_seq);

        tresult.assert_that<eq_sets>(
            src::of_container(empty) >> then::diff(src::of_container(/*copy*/mul_sub)), 
            empty
            );
        //self difference must produce empty
        tresult.assert_that<eq_sets>(
            src::of_container(/*copy*/mul_sub) >> then::diff(src::of_container(/*copy*/mul_sub)),
            empty
            );

        tresult.info() << "Test custom comparator...\n";
        auto less_ignore_case = [](const std::string& left, const std::string& right) -> int {
            auto [lr, rr] = std::mismatch(left.begin(), left.end(), right.begin(), right.end(),
                [](auto l, auto r) { return std::tolower(l) == std::tolower(r); });
            if (rr == right.end())
                return lr == left.end() ? 0 : 1;
            if( lr != left.end() )
                return int(std::tolower(*lr)) - int(std::tolower(*rr));
            return -1;//
        };
        
        OP::flur::LessOverride custom_less_trait(less_ignore_case);

        auto ignore_case_1 = src::of(
            std::multiset<std::string, decltype(custom_less_trait)::less_t> (
                {"aa", "aa", "BB", "bb", "BB", "C", "xx", "XX"},
                custom_less_trait.less_factory())
        );

        auto r_ignore_case = ignore_case_1
         >> then::diff(
             src::of_container(/*copy*/mul_sub), custom_less_trait
            );

        // std::set_difference is not good in custom compare untill c++20
        //so add extra set inititalization
        tresult.assert_that<eq_sets>(
            r_ignore_case ,
            std::vector<std::string>{ "aa", "aa", "BB", "C", "xx", "XX" }
            );

        //self-diff must produce empty even for different letter-cases
        auto case2 = src::of_container(
            std::multiset<std::string, decltype(custom_less_trait)::less_t>(
                { "AAA", "A", "bb", "BB", "d", "x", "Z" },
                custom_less_trait.less_factory()));
        tresult.assert_that<eq_sets>(
            src::of_container(empty) >> then::diff(src::of_container(/*copy*/mul_sub)),
            empty
            );
    }

    void test_ThenUnorderedDifference(OP::utest::TestRuntime& tresult)
    {
    }

static auto& module_suite = OP::utest::default_test_suite("flur.then")
    .declare("diff-small-dup", test_SmallDup)
    .declare("ordered-diff", test_ThenOrderedDifference)
    .declare("unordered-diff", test_ThenUnorderedDifference)
;

}