#include <set>
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
    void test_Default(OP::utest::TestRuntime& tresult)
    {
        tresult.info()<<"Check basic ...\n";
        std::vector<int> vint{ 1, 2, 3, 4, 5, 6, 7 };
        auto r1 = src::of_iterators(std::begin(vint), std::end(vint));
        tresult.assert_that< eq_sets>(
            vint, r1
            );
        std::set<std::string> sstr{ 
            "abc", "a", "bcd", "xyz", "b", "x" };
        auto r2 = src::of_iterators(std::begin(sstr), std::end(sstr));
        tresult.assert_that<eq_sets>(
            sstr, r2
            );

        tresult.info()<<"Check flur iterators of iterators...\n";
        auto r3 = src::of_iterators(std::begin(r2), std::end(r2));
        tresult.assert_that<eq_sets>(
            sstr, r3
            );

    }
    static auto& module_suite = OP::utest::default_test_suite("flur.of_iter")
        .declare("default", test_Default)
        ;

} //ns:<>