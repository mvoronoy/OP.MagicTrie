#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <algorithm>
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

        auto res1 = r_seq >> then::ordered_diff(flur_sub);

        tresult.assert_true(res1.compound().is_sequence_ordered());

        tresult.assert_that<eq_sets>(
            res1, expected);

        std::set<std::string> ord1_all{ {
            "gAAAAAAAAAM",
            "gAAAAAAAAAQ",
            "gAAAAAAAAAU",
            "gAAAAAAAAAY",
            "gAAAAAAAAAc",
            "gAAAAAAAAAg",
            "gAAAAAAAAAk",
            "gAAAAAAAAAo",
            "gAAAAAAAAAs"} };

        std::set <std::string> sub = { {"gAAAAAAAAAI",
                "gAAAAAAAAAQ",
                "gAAAAAAAAAY",
                "gAAAAAAAAAg",
                "gAAAAAAAAAo"} };
        std::set<std::string> expected2{ {"gAAAAAAAAAM", "gAAAAAAAAAU", "gAAAAAAAAAc", "gAAAAAAAAAk", "gAAAAAAAAAs"}};

        auto res2 = src::of(ord1_all) >> then::ordered_diff(src::of(sub));
        tresult.assert_that<eq_sets>(res1, src::of(expected2));
    }
    
    int less_ignore_case(const std::string& left, const std::string& right) 
    {
        auto [lr, rr] = std::mismatch(left.begin(), left.end(), right.begin(), right.end(),
            [](auto l, auto r) { return std::tolower(l) == std::tolower(r); });
        if (rr == right.end())
            return lr == left.end() ? 0 : 1;
        if (lr != left.end())
            return int(std::tolower(*lr)) - int(std::tolower(*rr));
        return -1;//
    };

    void test_ThenOrderedDifference(OP::utest::TestRuntime& tresult)
    {
        std::multiset<std::string> test_multi_seq {"aa", "aa", "bb", "bb", "bb", "c", "xx", "xx"};
        std::multiset<std::string> mul_sub = {"aaa", "a", "bb", "bb", "d", "x", "z"};
        std::multiset<std::string> expected;
        std::set_difference(test_multi_seq.begin(), test_multi_seq.end(), mul_sub.begin(), mul_sub.end(),
            std::inserter(expected, expected.end()));

        auto r_seq = src::of_container(/*copy*/test_multi_seq);
    
        auto res1 = r_seq >> then::ordered_diff(src::of_container(/*copy*/mul_sub));

        tresult.assert_true(res1.compound().is_sequence_ordered());
        tresult.assert_that<eq_sets>(
            res1, expected);

        tresult.info() << "Test empties...\n";

        std::multiset<std::string> empty;
        tresult.assert_that<eq_sets>(
            r_seq >> then::ordered_diff(src::of_container(empty)), test_multi_seq);

        tresult.assert_that<eq_sets>(
            src::of_container(empty) >> then::ordered_diff(src::of_container(/*copy*/mul_sub)), 
            empty
            );
        //self difference must produce empty
        tresult.assert_that<eq_sets>(
            src::of_container(/*copy*/mul_sub) >> then::ordered_diff(src::of_container(/*copy*/mul_sub)),
            empty
            );

        tresult.info() << "Test custom comparator...\n";
        
        OP::flur::OverrideComparisonTraits custom_less_trait(less_ignore_case);

        auto ignore_case_1 = src::of(
            std::multiset<std::string, decltype(custom_less_trait)::less_t> (
                {"aa", "aa", "BB", "bb", "BB", "C", "xx", "XX"},
                custom_less_trait.less_factory())
        );

        auto r_ignore_case = ignore_case_1
         >> then::ordered_diff(
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
            src::of_container(empty) >> then::ordered_diff(src::of_container(/*copy*/mul_sub)),
            empty
            );
    }

    size_t cust_hash(const std::string& s)
    {
        size_t r = 11;
        for (auto c : s)
            r = (r * 101) ^ std::tolower(c);
        return r;
    }
    void test_ThenUnorderedDifference(OP::utest::TestRuntime& tresult)
    {
        std::multiset<std::string> test_multi_seq{ "aa", "aa", "bb", "bb", "bb", "c", "xx", "xx", "i", "jjjj", "klm" };
        std::multiset<std::string> mul_sub = { "aaa", "a", "bb", "bb", "d", "x", "z" };
        std::multiset<std::string> expected;
        std::set_difference(test_multi_seq.begin(), test_multi_seq.end(), mul_sub.begin(), mul_sub.end(),
            std::inserter(expected, expected.end()));

        // yes, start checking with ordered provided "expectation" 
        // but apply as unordered
        auto r_seq = src::of_container(/*copy*/test_multi_seq);

        auto res1 = r_seq >> then::unordered_diff(src::of_container(/*copy*/mul_sub));
        tresult.assert_true(res1.compound().is_sequence_ordered(), 
            OP_CODE_DETAILS("unordered_diff must not change ordering feature"));
        tresult.assert_that<eq_sets>(
            res1, expected);
        //
        std::vector<std::string> unord_dif{ test_multi_seq.begin(), test_multi_seq.end() };
        std::shuffle(unord_dif.begin(), unord_dif.end(), 
            tools::RandomGenerator::instance().generator() );
        tresult.info() << "Test empty...\n";
        auto res_empty1 = src::of_container(/*copy*/unord_dif) >>
            then::unordered_diff(src::of_container(/*copy*/test_multi_seq));

        std::vector<std::string> empty;
        tresult.assert_that<eq_sets>(
            res_empty1, empty);
        tresult.assert_false(res_empty1.compound().is_sequence_ordered(),
            OP_CODE_DETAILS("unordered_diff must not change ordering trait"));

        tresult.assert_that<eq_sets>(src::of_container(/*copy*/empty) >>
            then::unordered_diff(src::of_container(/*copy*/unord_dif)),
            empty
            );

        tresult.assert_that<greater>(unord_dif.size(), 0);
        tresult.assert_that<eq_sets>(src::of_container(/*copy*/unord_dif) >>
            then::unordered_diff(src::of_container(/*copy*/empty)),
            unord_dif, 
            OP_CODE_DETAILS("subtracting empty must not change source sequence"));

        tresult.assert_that<eq_sets>(src::of_container(/*copy*/unord_dif) >>
            then::unordered_diff(src::of_container(std::vector<std::string>{"1", "2", "3"})),
            unord_dif);
        tresult.info() << "Custom comparator...\n";

        auto cmprt = OP::flur::custom_compare(less_ignore_case, cust_hash);
        std::unordered_set<std::string, decltype(cmprt)::hash_t> hm(16, cmprt.hash_factory());

        auto ignore_case_1 = src::of(unord_dif)
            //randomly make some entries upper-case
            >> then::mapping([&](auto str/*copy*/) {
                if (tools::random<std::uint16_t>() & 1)
                {
                    std::transform(str.begin(), str.end(), str.begin(), 
                        [](auto c) { return std::toupper(c); });
                }
                return str;
            });

        tresult.assert_that<eq_sets>(
            src::of(unord_dif) 
            >> then::unordered_diff(ignore_case_1, cmprt),
            empty,
            OP_CODE_DETAILS("subtracting itself must produce empty"));

        auto res_ignore_case2 = ignore_case_1 
                >> then::unordered_diff(
                    src::of_container(/*copy*/mul_sub), cmprt)
                >> then::mapping([](auto str) {//need extra tolower since `eq_unordered_sets` has no custom comparator
                    std::transform(str.begin(), str.end(), str.begin(), 
                        [](auto c) { return std::tolower(c); });
                    return str;
                });
        tresult.assert_that<eq_unordered_sets>(res_ignore_case2, expected);
    }

static auto& module_suite = OP::utest::default_test_suite("flur.then")
    .declare("diff-small-dup", test_SmallDup)
    .declare("ordered-diff", test_ThenOrderedDifference)
    .declare("unordered-diff", test_ThenUnorderedDifference)
;

}