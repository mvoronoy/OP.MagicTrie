#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

namespace 
{
    void test_ThenMaf(OP::utest::TestRuntime& tresult)
    {
        tresult.info() << "Filter empty source\n";
        const std::set<std::string> empty_set;
        auto r_empty = src::of(empty_set)
            >> then::maf_cv([&](const std::string& a, std::string& r) {
            tresult.fail("Empty must not invoke filter");
            return true; })
        ;
        tresult.assert_true(std::empty(r_empty), "wrong result number");

        const std::set<std::string> single_set = { "a" };
        size_t invocations = 0;
        auto r_empty2 = src::of(single_set)
            >> then::maf_cv([&](const std::string& a, std::string& ) {
            ++invocations;
            return false;
        });
        tresult.assert_true(std::empty(r_empty2), "wrong result number");
        tresult.assert_that<equals>(1, invocations, "wrong result number");
        
        std::set<std::string> tri_set = { "a", "bb", "ccc" };
        invocations = 0;
        auto r3 = 
            src::of_container(std::move(tri_set))
            >> then::keep_order_maf([&](const std::string& a, size_t& r) {
                if (!a.empty() && a[0] == 'b')
                {
                    r = a.size();
                    return true;
                }
                return false;
        });
        tresult.assert_that<eq_sets>(r3, std::set<size_t>{2}, "wrong content");

        const std::vector<std::uint64_t> all_num = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        auto r4 = src::of(all_num)
            >> then::maf_cv([&](std::uint64_t n, double& r) {
            //find 2^(prime-number) only
            for (decltype(n) i = 2; i <= (n / 2); ++i)
            {
                if ((n % i) == 0)
                    return false;//not a prime
            }
            r = 1ull << n;
            return true;
        });

        tresult.assert_that<eq_sets>(r4, std::vector<double>{2, 4, 8, 32, 128}, 
            OP_CODE_DETAILS());
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.then")
        .declare("maf", test_ThenMaf)
        ;
}//ns: empty
