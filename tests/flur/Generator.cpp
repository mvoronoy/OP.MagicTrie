#include <future>
#include <chrono>
#include <thread>

#include <map>
#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>

#include <op/common/ftraits.h>

#include <op/flur/flur.h>
#include <op/flur/Reducer.h>
namespace {
using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;


void test_noarg_gen(OP::utest::TestResult& tresult)
{
    size_t invocked = 0;
    OP::flur::make_lazy_range(OP::flur::src::generator([&]() {
        ++invocked;
        return std::optional<int>{};
        })).for_each([&](const auto& r) {
            tresult.fail("Empty generator must not produce any element");
            });
    tresult.assert_that<equals>(1, invocked, "wrong invocation number");

    invocked = 0;
    size_t control_seq = 1;
    OP::flur::make_lazy_range(OP::flur::src::generator([&]() {
        ++invocked;
        return invocked < 10 ? std::optional<size_t>{invocked} : std::optional<size_t>{};
        }))
    .for_each([&](const auto& r) {
        tresult.assert_that<equals>(r, control_seq++, "Sequence vioaltion");
     });
    tresult.assert_that<equals>(10, invocked, "wrong invocation number");
}
void test_boolarg_gen(OP::utest::TestResult& tresult)
{
    size_t invocked = 0;
    OP::flur::make_lazy_range(OP::flur::src::generator([&](bool first) {
        tresult.assert_true((first && (invocked == 0))
            || (!first && (invocked > 0)),
            "bool parameter wrong semantic");
        ++invocked;
        return std::optional<int>{};
        })).for_each([&](int r) -> void {
            tresult.fail("Empty generator must not produce any element");
            });
    tresult.assert_that<equals>(1, invocked, "wrong invocation number");

    invocked = 0;
    size_t control_seq = 1;
    OP::flur::make_lazy_range(OP::flur::src::generator([&](bool first) {
        tresult.assert_true((first && (invocked == 0))
            || (!first && (invocked > 0)),
            "bool parameter wrong semantic");

        ++invocked;
        return invocked < 10 ? std::optional<size_t>{invocked} : std::optional<size_t>{};
        }))
    .for_each([&](const auto& r) {
        tresult.assert_that<equals>(r, control_seq++, "Sequence vioaltion");
     });
    tresult.assert_that<equals>(10, invocked, "wrong invocation number");
}
void test_exception(OP::utest::TestResult& tresult)
{
    tresult.info() << "check exception during 'next'...\n";
    int i3 = 0;
    auto pipeline3 = src::generator([&]() {
        if (i3 > 0)
        {
            throw std::runtime_error("generation fail emulation");
        }
        //don't do optional of future in real code
        return (i3 > 3) 
            ? std::optional<int>{}
        : std::make_optional(++i3);
        });

    tresult.assert_exception<std::runtime_error>([&]() {
        pipeline3.count();
        });
}

static auto module_suite = OP::utest::default_test_suite("flur.generator")
->declare(test_noarg_gen, "noarg")
->declare(test_boolarg_gen, "bool-arg")
->declare(test_exception, "exceptions");
} //ns:<anonymous>
