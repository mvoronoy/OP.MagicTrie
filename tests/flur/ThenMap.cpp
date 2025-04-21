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

    struct MapOp
    {
        int operator()(int x) //no const there
        {
            return x * 2;
        }
    };

    void test_OperatorMutable(OP::utest::TestRuntime& tresult)
    {
        std::array inp{ 1, 2, 3 };
        tresult.assert_that<eq_sets>(
            src::of_container(inp)
            >> then::mapping([](auto x) mutable -> int {
                return (x + 1);
                }), std::array{ 2, 3, 4 });

        tresult.assert_that<eq_sets>(
            src::of_container(inp)
            >> then::mapping(MapOp{}), std::array{ 2, 4, 6 });
    }
    
    void test_Multichain(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;

        auto some_stupid_action = [](int a, int b, int c, const std::string& tail){
            std::ostringstream os;
            os << a << ',' << b << ',' << c << ':' << tail;
            return os.str();
            };
        std::array inp{ 1, 2, 3 };
        auto pipeline = src::of_container(inp)
            >> then::mapping(ReusableMapBuffer([](int i, std::string& str){
                    str = std::to_string(i);
                }))
            >> then::mapping([&](const auto& str, const SequenceState& current){
                    return some_stupid_action(current.step(), str.size(), current.generation(), str);
                })
            ;
        pipeline >>= apply::drain(std::ostream_iterator<std::string>(
            tresult.debug(), "\n"));
        tresult.assert_that<eq_sets>(
            pipeline, 
            std::array{
                "0,1,0:1"s,
                "1,1,0:2"s,
                "2,1,0:3"s
            });
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.then.map")
        .declare("map", test_Map)
        .declare("mutable", test_OperatorMutable)
        .declare("multichain", test_Multichain)
    ;
} //ns:
