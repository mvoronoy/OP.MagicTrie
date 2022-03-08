#include <future>
#include <chrono>
#include <thread>

#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>

#include <op/common/ftraits.h>
#include <op/common/ThreadPool.h>

#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

namespace {

    template <size_t N>
    void apply_minitest(OP::utest::TestResult & tresult)
    {
        OP::utils::ThreadPool tp(2);

        for(const auto& r: (src::of_iota(10, 10)
            >> then::minibatch<N>(tp)
            ))
        {
            tresult.fail("no invocation allowed");
        }

        size_t order_check = 10;
        for(const auto& r : src::of_iota(10, 20)
            >> then::minibatch<N>(tp)
            ) 
        {
            tresult.assert_that<equals>(r, order_check, OP_CODE_DETAILS("sequence violation (1), expected:" 
                << order_check << ", while:" << r << "\n"));
            ++order_check;
        }
        tresult.assert_that<equals>(order_check, 20, "wrong count of items in sequence");
    }
    void test_minibatch(OP::utest::TestResult& tresult)
    {
        apply_minitest<2>(tresult);
        apply_minitest<3>(tresult);
    }

    static auto module_suite = OP::utest::default_test_suite("flur.minibatch")
        ->declare(test_minibatch, "base")
        ;
}