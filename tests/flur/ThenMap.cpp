#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <string>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

namespace
{
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace std::string_literals;

    void test_Map(OP::utest::TestRuntime& tresult)
    {
        auto ints = 
            src::of_container(std::array{"11"s, "12"s, "13"s})
            >> then::mapping([](const std::string& s, const SequenceState& state) -> int{
                return std::stoi(s) * state.step().current();
            });
        tresult.assert_that<eq_sets>(ints, std::array{0, 12, 26});

        tresult.assert_that<eq_sets>(
            src::null<int>() 
            >> then::mapping([](int x, const SequenceState& state) -> int {
                return (x+1) * static_cast<int>(state.step().current());
                }), std::array<int, 0>{});

        tresult.assert_that<eq_sets>(
            src::null<int>() 
            >> then::mapping([](int x) -> int {
                return (x+1) ;
                }), std::array<int, 0>{});
    }


    static auto& module_suite = OP::utest::default_test_suite("flur.then.map")
        .declare("map", test_Map)
    ;
} //ns: