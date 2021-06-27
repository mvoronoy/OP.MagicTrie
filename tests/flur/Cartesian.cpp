#include <map>
#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>
#include <op/flur/Reducer.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

bool logic_or(bool a, bool b)
{
    return a || b;
}
void test_ThenCartesian(OP::utest::TestResult& tresult)
{
    tresult.info() << "Cartesian with empty source\n";
    constexpr auto r_bool = src::of_value(false)
        >> then::filter([](bool i) {return i; }) //always empty
        >> then::cartesian( //empty as well
            src::of_value(false)
            >> then::filter([](bool i) {return i; }) //always empty
            , logic_or
        ) 
        ;
    //check or_default keeps order
    static_assert(!r_bool.compound().ordered_c, "bool x bool cartesian is not ordered sequence");
    tresult.assert_true(r_bool.empty(), "wrong result number");
    
    constexpr auto r2 = src::of_value(false)
        >> then::filter([](bool i) {return i; }) //always empty
        >> then::cartesian(src::of_value(false) /*non empty*/
            , logic_or
        )
        ;
    
    tresult.assert_true(r2.empty(), "wrong result number");
    std::vector<int> sample1{ 1, 3, 5, 7, 11 };
    auto r3 = src::of_container(sample1)
        >> then::cartesian(
            src::of_value(false)
            >> then::filter([](bool i) {return i; }) //always empty
            , logic_or
        )
        ;
    tresult.assert_true(r3.empty(), "wrong result number");


    auto r4 = src::of_iota(0, 10)
        >> then::cartesian(
            src::of_iota(-2, 0)
            , [&](auto x, auto y) { 
                tresult.assert_that<negate<less>>(x, 0, OP_CODE_DETAILS("pair must produce positive-negative pair"));
                tresult.assert_that<less>(y, 0, OP_CODE_DETAILS("pair must produce positive-negative pair"));
                return x * y;
            }
        )
        ;
    tresult.assert_that<equals>(-135, r4.reduce<int>(OP::flur::reducer::sum<int>), "wrong result number");


}

static auto module_suite = OP::utest::default_test_suite("flur.then")
->declare(test_ThenCartesian, "cartesian")
;