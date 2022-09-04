#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

bool logic_or(bool a, bool b)
{
    return a || b;
}
void test_ThenCartesian(OP::utest::TestRuntime& tresult)
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
    tresult.assert_true(std::empty(r_bool), "wrong result number");
    
    constexpr auto r2 = src::of_value(false)
        >> then::filter([](bool i) {return i; }) //always empty
        >> then::cartesian(src::of_value(false) /*non empty*/
            , logic_or
        )
        ;
    
    tresult.assert_true(std::empty(r2), "wrong result number");
    std::vector<int> sample1{ 1, 3, 5, 7, 11 };
    auto r3 = src::of_container(sample1)
        >> then::cartesian(
            src::of_value(false)
            >> then::filter([](bool i) {return i; }) //always empty
            , logic_or
        )
        ;
    tresult.assert_true(std::empty(r3), "wrong result number");


    std::function<int(int, int)> cart_f =
        [&](int x, int y) ->int {
        tresult.assert_that<negate<less>>(x, 0, OP_CODE_DETAILS("pair must produce positive-negative pair"));
        tresult.assert_that<less>(y, 0, OP_CODE_DETAILS("pair must produce positive-negative pair"));
        return x * y;
    };
    auto r4 = src::of_iota(0, 10)
        >> then::cartesian(
            src::of_iota(-2, 0)
            , cart_f
        )
        ;
    tresult.assert_that<equals>(-135, OP::flur::reduce(r4, 0), "wrong result number");
}

void test_Simple(OP::utest::TestRuntime& tresult)
{
    std::vector<int> vec1{1, 2, 3, 4},
    vec2 {10, 20, 30, 40, 50},
    vec3 {0, 100, 200};
    using namespace OP::flur;
    auto crtsn = apply::cartesian(
        src::of(vec1),
        src::of(vec2),
        src::of(vec3));
    size_t count = 0;
    std::array<size_t, 3> hundreds{ 0, 0, 0 };
    crtsn.collect([&](auto n1, auto n2, auto n3)
        { 
            count++;
            auto sum = (n1 + n2 + n3);
            tresult.debug() << sum << (count % 5 ? "," : "\n");
            ++hundreds[sum / 100];
        });
    tresult.assert_that<equals>(60, count);
    tresult.assert_true(hundreds[0] == hundreds[1] && hundreds[1] == hundreds[2]);
    auto crtsn2 = crtsn.extend(src::of(std::vector<int>{}));
    count = 0;
    crtsn2.collect([&](auto n1, auto n2, auto n3, auto n4) {++count; });
    tresult.assert_that<equals>(0, count);

    using namespace OP::flur;
    auto crtsn3 = crtsn.extend(src::of(std::vector<int>{1}));
    count = 0;
    crtsn3.collect([&](auto n1, auto n2, auto n3, auto n4) {++count; });
    tresult.assert_that<equals>(60, count);
}

static auto& module_suite = OP::utest::default_test_suite("flur.then")
.declare("cartesian", test_ThenCartesian)
.declare("cartesian_app", test_Simple)
;
