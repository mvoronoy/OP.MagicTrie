#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>
#include <op/flur/Reducer.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

void test_OrDefault(OP::utest::TestResult& tresult)
{
    constexpr auto r_bool = src::of_value(false)
        >> then::filter([](bool i) {return i; })
        >> then::or_default(false)
        ;

    //check or_default keeps order
    static_assert(r_bool.compound().ordered_c, "bool-singleton must produce ordered sequence");
    tresult.assert_that<equals>(r_bool.count(), 1, "wrong result number");
    r_bool.for_each([&](const auto& i) {
        tresult.assert_false(i, "Wrong value stored");
        });

    //check or_default may be unordered order
    constexpr std::array<int, 3> some_arr{ 2, 1, 3 };
    static_assert(!(
        src::of_container(some_arr)
        >> then::or_default(src::of_value(5))
        ).compound().ordered_c, "Unordered seq expected");
    //check or_default may produce multi-step sequence
    constexpr auto multi_seq = (src::of_container(some_arr)
        >> then::filter([](const auto& i) { return i > 10; }) //filter out all
        >> then::or_default(src::of_container(some_arr))
        );
    multi_seq.for_each([&](const auto& i) {
            tresult.assert_that<less>(i, 10, "Wrong alt value");
            });
    tresult.assert_that<equals>(some_arr.size(), multi_seq.count(), "Wrong items number");

}

void test_SortedOrDefault(OP::utest::TestResult& tresult)
{
    std::set<std::string> sample{ "a"s, "b"s, "c"s,  };
    const std::string sylable_c{ 'a', 'e', 'i', 'o', 'u', 'y' };
    auto r0 = src::of_container(std::cref(sample))
        >> then::filter([](const auto& i) {return i.size() > 1; }) //every element is filtered, so result empty
        >> then::or_default(
            //ordered container + filter must produce ordered sequence
            src::of_container(std::cref(sample))
            >> then::filter([&](const auto& i) {return i.find_first_of(sylable_c) != std::string::npos; }))
        ;
    auto compound = r0.compound();
    tresult.assert_true(compound.ordered_c, "or_default must produce ordered sequence when both source and alternative are sorted");


}

static auto module_suite = OP::utest::default_test_suite("flur.then")
->declare(test_OrDefault, "or_default")
->declare(test_SortedOrDefault, "or_default_sorted")
;