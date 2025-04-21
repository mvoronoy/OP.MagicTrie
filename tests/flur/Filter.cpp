#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

namespace {
    void test_ThenFilter(OP::utest::TestRuntime& tresult)
    {
        tresult.info() << "Filter empty source\n";
        const std::set<std::string> empty_set;
        auto r_empty = src::of(empty_set)
            >> then::filter([&](const std::string& a) {
            tresult.fail("Empty must not invoke filter");
            return true; })
        ;

        tresult.assert_true(std::empty(r_empty), "wrong result number");

        const std::set<std::string> single_set = { "a" };
        size_t invocations = 0;
        auto r_empty2 = src::of(single_set)
            >> then::filter([&](const std::string& a) {
            ++invocations;
            return false;
        });
        tresult.assert_true(std::empty(r_empty2), "wrong result number");
        tresult.assert_that<equals>(1, invocations, "wrong result number");
        
        invocations = 0;
        auto r3 = 
            src::of_container(std::set<std::string>{ "a", "bb", "ccc" })
            >>
            then::filter([&](const std::string& a) {
                return !a.empty() && a[0] == 'b';
            });
        tresult.assert_that<eq_sets>(r3, std::set<std::string>{ "bb" }, "wrong result number");
    }

    void test_FilterEdge(OP::utest::TestRuntime& tresult)
    {
        const std::vector<int> all_num = {1, 2, 3, 4, 5};
        auto r1 = src::of(all_num)
            >> then::filter([&](const auto& n) {
            return (n & 1) == 0;
        });
        tresult.assert_that<eq_sets>(r1, std::vector<int>{2, 4}, OP_CODE_DETAILS());
    }

    void test_sequence_state()
    {                                                      
        int r = 0;
        
        src::generator([](const SequenceState& state) -> std::optional<int>{
            if(state.step().current() < 100)
                return static_cast<int>(state.step().current());
            return {};
            })
        >> then::filter([](auto i, const OP::flur::SequenceState& state) -> bool { 
            return (i ^ state.step().current()) & 1;  //just random formula
            })
        >>= apply::sum(r);
        std::cout << r << "\n";
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.then.filter")
        .declare("filter", test_ThenFilter)
        .declare("filter-edge", test_FilterEdge)
        .declare("with-state", test_sequence_state)
        ;
}//ns: empty
