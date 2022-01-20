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

namespace {
    void test_ThenFilter(OP::utest::TestResult& tresult)
    {
        tresult.info() << "Filter empty source\n";
        const std::set<std::string> empty_set;
        auto r_empty = src::of(empty_set)
            >> then::filter([&](const std::string& a) {
            tresult.fail("Empty must not invoke filter");
            return true; })
            ;
            static_assert(r_empty.compound().ordered_c, "filter must keep ordering");
            tresult.assert_true(r_empty.empty(), "wrong result number");

            const std::set<std::string> single_set = { "a" };
            size_t invocations = 0;
            auto r_empty2 = src::of(single_set)
                >> then::filter([&](const std::string& a) {
                ++invocations;
                return false;
                    });
            tresult.assert_true(r_empty2.empty(), "wrong result number");
            tresult.assert_that<equals>(1, invocations, "wrong result number");
            
            std::set<std::string> tri_set = { "a", "bb", "ccc" };
            invocations = 0;
            auto r3 = OP::flur::make_unique(
                src::of_container(std::move(tri_set)),
                then::filter([&](const std::string& a) {
                return !a.empty() && a[0] == 'b';
            });
            tresult.assert_true(r_empty2.empty(), "wrong result number");
            tresult.assert_that<equals>(1, invocations, "wrong result number");

    }

    static auto module_suite = OP::utest::default_test_suite("flur.then")
        ->declare(test_ThenFilter, "filter")
        ;
}//ns: empty