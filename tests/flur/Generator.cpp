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


    void test_simple_arg(OP::utest::TestRuntime& tresult)
    {
        size_t invocked = 0;
        for (const auto& r : OP::flur::src::generator([&]() -> std::optional<int> {
            ++invocked;
            return std::nullopt;
            }))
        {
            tresult.fail("Empty generator must not produce any element");
        }
        tresult.assert_that<equals>(1, invocked, "wrong invocation number");

        invocked = 0;
        size_t control_seq = 1;
        for (const auto& r : OP::flur::src::generator([&]() -> std::optional<size_t> {
            ++invocked;
            return invocked < 10 
                ? std::optional<size_t>{invocked} 
                : std::nullopt;
            }))
        {
            tresult.assert_that<equals>(r, control_seq++, "Sequence violation");
        }

        tresult.assert_that<equals>(10, invocked, "wrong invocation number");
    }

    void test_boolarg_gen(OP::utest::TestRuntime& tresult)
    {
        size_t invocked = 0;
        for (auto r : OP::flur::src::generator([&](const SequenceState& at) -> std::optional<int> {
            tresult.assert_true((at.step() == 0 && (invocked == 0))
                || (at.step() > 0 && (invocked > 0)),
                "bool parameter wrong semantic");
            ++invocked;
            return std::nullopt;
            }))
        {
            tresult.fail("Empty generator must not produce any element");
        }
        tresult.assert_that<equals>(1, invocked, "wrong invocation number");

        invocked = 0;
        size_t control_seq = 1;
        for (const auto& r : OP::flur::src::generator([&](const SequenceState at) ->std::optional<size_t> {
            tresult.assert_true(
                (at.step() == 0 && (invocked == 0))
                || (at.step() > 0 && (invocked > 0)),
                "bool parameter wrong semantic");

            ++invocked;
            return invocked < 10 ? std::optional<size_t>{invocked} : std::nullopt;
            }))
        {
            tresult.assert_that<equals>(r, control_seq++, "Sequence violation");
        }
        tresult.assert_that<equals>(10, invocked, "wrong invocation number");
    }

    void test_exception(OP::utest::TestRuntime& tresult)
    {
        tresult.info() << "check exception during 'next'...\n";
        int i3 = 0;

        auto pipeline3 = src::generator([&]()->std::optional<int> {
            if (i3 > 0)
            {
                throw std::runtime_error("generation fail emulation");
            }
            return ++i3; //next call will raise an exception
        });

        tresult.assert_exception<std::runtime_error>([&]() {
            for (auto r : pipeline3) {}
            });
    }

    void test_ptr_gen(OP::utest::TestRuntime& tresult)
    {
        using set_t = std::set <std::string >;
        using target_set_t = std::set<std::string_view >;
        set_t subset{ "aaa", "bbb", "ccc" };
        target_set_t result;
        using cstr_ptr = const std::string*;

        typename set_t::iterator current{};
        for (auto r : OP::flur::src::generator(
            [&](const SequenceState& attrs) -> std::optional<std::string_view>{
                current = attrs.step() == 0 ? subset.begin() : std::next(current);
                return current == subset.end() ? std::nullopt : std::optional{*current};
            }))
        {
            //tresult.debug() << "gen:" << r << "\n";
            result.insert(r);
        }
        tresult.assert_that<eq_sets>(subset, result, "generator doesn't produce expected result");
        tresult.assert_that<equals>(result.size(), 3);
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.generator")
        .declare("noarg", test_simple_arg)
        .declare("bool-arg", test_boolarg_gen)
        .declare("exceptions", test_exception)
        .declare("ptr", test_ptr_gen);
} //ns:<anonymous>
