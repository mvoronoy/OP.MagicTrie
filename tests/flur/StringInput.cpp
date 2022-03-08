#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;
using namespace std::string_view_literals;
namespace {

    void test_Split(OP::utest::TestResult& tresult)
    {
        tresult.assert_true(
            std::empty(src::of_string_split(""s))
        );

        tresult.assert_that<eq_sets>(
            src::of_string_split("a"s), 
            std::vector<std::string> {"a"s}, "single item failed");

        tresult.assert_that<eq_sets>(
            src::of_string_split(" a"s), 
            std::vector<std::string> {"a"s}, "single item failed");

        tresult.assert_that<eq_sets>(
            src::of_string_split("a "s), 
            std::vector<std::string> {"a"s}, "single item failed");

        tresult.assert_that<eq_sets>(
            src::of_string_split(" a "s), 
            std::vector<std::string> {"a"s}, "single item failed");

        tresult.assert_that<eq_sets>(
            src::of_string_split("a bb"s), 
            std::vector<std::string> {"a"s, "bb"s}, "pair items failed");

        tresult.assert_that<eq_sets>(
            src::of_string_split("a  bb"s), 
            std::vector<std::string> {"a"s, "bb"s}, "pair items failed");

        tresult.assert_that<eq_sets>(
            src::of_string_split("aaa  bb c"s), 
            std::vector<std::string> {"aaa"s, "bb"s, "c"s}, "tripplet items failed");
    }
    void test_SplitSep(OP::utest::TestResult& tresult)
    {
        tresult.assert_that<eq_sets>(
            src::of_string_split("a b c"s, ""s), 
            std::vector<std::string> {"a b c"s}, "empty separator must produce origin string");
        tresult.assert_that<eq_sets>(
            src::of_string_split("a:b:c"s, ";"s), 
            std::vector<std::string> {"a:b:c"s}, "non-matching separator");
        tresult.assert_that<eq_sets>(
            src::of_string_split("a:b:c"s, ";:"s), 
            std::vector<std::string> {"a"s, "b"s, "c"s}, "complex separator");
        tresult.assert_that<eq_sets>(
            src::of_string_split("aa:;bbb:cccc;ddd dd:"s, ";:"s), 
            std::vector<std::string> {"aa"s, "bbb"s, "cccc"s, "ddd dd"s}, "complex separator");
        std::string back_str("aa:;bbb:cccc;ddd dd:");
        tresult.assert_that<eq_sets>(
            src::of_string_split(std::string_view(back_str), ";:"s), 
            std::vector<std::string> {"aa"s, "bbb"s, "cccc"s, "ddd dd"s}, "complex separator over string_view");
        tresult.assert_that<eq_sets>(
            src::of_string_split("a\0a\0bbb\0cccc\0ddd dd\0"s, "\0"s), 
            std::vector<std::string> {"a"s, "a"s, "bbb"s, "cccc"s, "ddd dd"s}, "zero - char separator");
    }
    static auto module_suite = OP::utest::default_test_suite("flur.string")
        ->declare(test_Split, "split")
        ->declare(test_SplitSep, "split-separator")
        ;

} //ns:<>