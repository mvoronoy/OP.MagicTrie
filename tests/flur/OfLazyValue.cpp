#include <future>
#include <chrono>
#include <thread>

#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>

#include <op/common/ftraits.h>

#include <op/flur/flur.h>

namespace {
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace std::string_literals;


    void test_noarg(OP::utest::TestRuntime& tresult)
    {
        size_t invocked = 0;
        auto lazy_val_eval = [&]() {
            ++invocked;
            return 57;
            };
        for (const auto& r : OP::flur::src::of_lazy_value(lazy_val_eval))
        {
            tresult.assert_that<equals>(57, r);
        }
        tresult.assert_that<equals>(invocked, 1);
        invocked = 0;
        for (const auto& r : OP::flur::src::of_lazy_value(lazy_val_eval, 3))
        {
            tresult.assert_that<equals>(57, r);
        }
        tresult.assert_that<equals>(invocked, 3);
        invocked = 0;
        for (const auto& r : OP::flur::src::of_lazy_value(lazy_val_eval, 0))
        {
            tresult.fail("Must not be invoked");
        }
        tresult.assert_that<equals>(invocked, 0);
        //test correctnes of function argument-attributes
        size_t expected_generation = 0, step = 0;
        auto pipeline = OP::flur::src::of_lazy_value(
            [&](const OP::flur::PipelineAttrs& current, const size_t limit) {
                ++invocked;
                tresult.assert_that<equals>(3, limit);
                tresult.assert_that<equals>(step++, current.step().current());
                tresult.assert_that<equals>(expected_generation, current.generation().current());
                return 57;
                }, 3).compound();
        invocked = 0;
        for (const auto& r : pipeline)
        {
            tresult.assert_that<equals>(57, r);
        }
        tresult.assert_that<equals>(invocked, 3);
        ++expected_generation;
        step = 0;
        for (const auto& r : pipeline)
        {
            tresult.assert_that<equals>(57, r);
        }
    }
   

    static auto& module_suite = OP::utest::default_test_suite("flur.lazy_value")
        .declare("noarg", test_noarg)
        ;
} //ns:<anonymous>
