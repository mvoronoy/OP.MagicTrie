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


    void test_noarg_gen(OP::utest::TestRuntime& tresult)
    {
        size_t invocked = 0;
        for (const auto& r : OP::flur::src::generator([&]() {
            ++invocked;
            return std::optional<int>{};
            }))
        {
            tresult.fail("Empty generator must not produce any element");
        }
        tresult.assert_that<equals>(1, invocked, "wrong invocation number");

        invocked = 0;
        size_t control_seq = 1;
        for (const auto& r : OP::flur::src::generator([&]() {
            ++invocked;
            return invocked < 10 ? std::optional<size_t>{invocked} : std::optional<size_t>{};
            }))
        {
            tresult.assert_that<equals>(r, control_seq++, "Sequence vioaltion");
        }

        tresult.assert_that<equals>(10, invocked, "wrong invocation number");
    }
    void test_boolarg_gen(OP::utest::TestRuntime& tresult)
    {
        size_t invocked = 0;
        for (auto r : OP::flur::src::generator([&](const SequenceState& at) {
            tresult.assert_true((at.step() == 0 && (invocked == 0))
                || (at.step() > 0 && (invocked > 0)),
                "bool parameter wrong semantic");
            ++invocked;
            return std::optional<int>{};
            }))
        {
            tresult.fail("Empty generator must not produce any element");
        }
        tresult.assert_that<equals>(1, invocked, "wrong invocation number");

        invocked = 0;
        size_t control_seq = 1;
        for (const auto& r : OP::flur::src::generator([&](const SequenceState at) {
            tresult.assert_true(
                (at.step() == 0 && (invocked == 0))
                || (at.step() > 0 && (invocked > 0)),
                "bool parameter wrong semantic");

            ++invocked;
            return invocked < 10 ? std::optional<size_t>{invocked} : std::optional<size_t>{};
            }))
        {
            tresult.assert_that<equals>(r, control_seq++, "Sequence vioaltion");
        }
        tresult.assert_that<equals>(10, invocked, "wrong invocation number");
    }
    void test_exception(OP::utest::TestRuntime& tresult)
    {
        tresult.info() << "check exception during 'next'...\n";
        int i3 = 0;
        auto pipeline3 = src::generator([&]() {
            if (i3 > 0)
            {
                throw std::runtime_error("generation fail emulation");
            }
            return std::optional<int>{};
            });

        tresult.assert_exception<std::runtime_error>([&]() {
            for (auto r : pipeline3) {}
            });
    }

    void test_ptr_gen(OP::utest::TestRuntime& tresult)
    {
        using set_t = std::set <std::string >;
        using target_set_t = std::set <std::string_view >;
        set_t subset{ "aaa", "bbb", "ccc" };
        target_set_t result;
        using cstr_ptr = const std::string*;
        size_t invocked = 0;
        typename set_t::iterator current{};
        for (const auto& r : OP::flur::src::generator([&](const SequenceState& attrs) -> cstr_ptr {
            current = attrs.step() == 0 ? subset.begin() : std::next(current);
            ++invocked;
            return current == subset.end() ? nullptr : &*current;
            }))
        {
            //tresult.debug() << "gen:" << r << "\n";
            result.insert(r);
        }
        tresult.assert_that<eq_sets>(subset, result, "generator doesn't produce expected result");
        tresult.assert_that<equals>(invocked, 3);
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.generator")
        .declare("noarg", test_noarg_gen)
        .declare("bool-arg", test_boolarg_gen)
        .declare("exceptions", test_exception)
        .declare("ptr", test_ptr_gen);
} //ns:<anonymous>
