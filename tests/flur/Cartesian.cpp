#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

namespace 
{
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
                logic_or, 
                src::of_value(false)
                >> then::filter([](bool i) {return i; }) //always empty
            ) 
            ;
        //check  keeps order
        tresult.assert_false(r_bool.compound().is_sequence_ordered(), 
            "bool x bool cartesian is not ordered sequence");
        tresult.assert_true(std::empty(r_bool), "wrong result number");
        
        constexpr auto r2 = src::of_value(false)
            >> then::filter([](bool i) {return i; }) //always empty
            >> then::cartesian(
                logic_or,
                src::of_value(false) /*non empty*/
            )
            ;
        
        tresult.assert_true(std::empty(r2), "wrong result number");
        std::vector<int> sample1{ 1, 3, 5, 7, 11 };
        auto r3 = src::of_container(sample1)
            >> then::cartesian(
                logic_or,
                src::of_value(false)
                >> then::filter([](bool i) {return i; }) //always empty
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
                cart_f,
                src::of_iota(-2, 0)
            )
            ;
        tresult.assert_that<equals>(-135, OP::flur::reduce(r4, 0), "wrong result number");
    }

    void test_Applicator(OP::utest::TestRuntime& tresult)
    {
        std::vector<int> vec1{1, 2, 3, 4},
        vec2 {10, 20, 30, 40, 50},
        vec3 {0, 100, 200};
        using namespace OP::flur;

        size_t count = 0;
        std::array<size_t, 3> hundreds{ 0, 0, 0 };

        auto crtsn = apply::cartesian(
            [&](auto n1, auto n2, auto n3)
            {
                count++;
                auto sum = (n1 + n2 + n3);
                tresult.debug() << sum << (count % 5 ? "," : "\n");
                ++hundreds[sum / 100];
            },
            src::of_container(vec2),
            src::of_container(vec3))
            ;
        src::of_container(vec1) >>= crtsn;
        
        tresult.assert_that<equals>(60, count);
        tresult.assert_true(hundreds[0] == hundreds[1] && hundreds[1] == hundreds[2]);

        auto crtsn4 = [&](auto n1, auto n2, auto n3, auto n4) {++count; };
        count = 0;
        src::of_container(vec1) >>= apply::cartesian(
            crtsn4,
            src::of_container(vec2),
            src::of_container(vec3), 
            src::of_container(std::vector<int>{}));
        tresult.assert_that<equals>(0, count);

        
        count = 0;
        src::of_container(vec1) >>= apply::cartesian(
            crtsn4,
            src::of_container(vec2),
            src::of_container(vec3), 
            src::of_container(std::vector<int>{1}));
        tresult.assert_that<equals>(60, count);
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.then.cartesian")
    .declare("cartesian", test_ThenCartesian)
    .declare("applicator", test_Applicator)
    ;
}//ns: