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

namespace {
using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;


void test_noarg_gen(OP::utest::TestResult& tresult)
{
    size_t invocked = 0;
    for(const auto& r: OP::flur::src::generator([&]() {
        ++invocked;
        return std::optional<int>{};
        }))
    {
            tresult.fail("Empty generator must not produce any element");
    }
    tresult.assert_that<equals>(1, invocked, "wrong invocation number");

    invocked = 0;
    size_t control_seq = 1;
    for(const auto& r: OP::flur::src::generator([&]() {
        ++invocked;
        return invocked < 10 ? std::optional<size_t>{invocked} : std::optional<size_t>{};
        }))
    {
        tresult.assert_that<equals>(r, control_seq++, "Sequence vioaltion");
    }

    tresult.assert_that<equals>(10, invocked, "wrong invocation number");
}
void test_boolarg_gen(OP::utest::TestResult& tresult)
{
    size_t invocked = 0;
    for (auto r : OP::flur::src::generator([&](bool first) {
        tresult.assert_true((first && (invocked == 0))
            || (!first && (invocked > 0)),
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
    for(const auto& r: OP::flur::src::generator([&](bool first) {
        tresult.assert_true((first && (invocked == 0))
            || (!first && (invocked > 0)),
            "bool parameter wrong semantic");

        ++invocked;
        return invocked < 10 ? std::optional<size_t>{invocked} : std::optional<size_t>{};
        }))
    {
        tresult.assert_that<equals>(r, control_seq++, "Sequence vioaltion");
    }
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
        for (auto r : pipeline3) {}
        });
}

/** Marker for compare 2 sets with string/string_view*/
struct eq_sets
{
    constexpr static size_t args_c = 2;
    using is_transparent = int;

    template <class Left, class Right>
    constexpr bool operator()(const Left& left, const Right& right)  const
    {
        auto pr = std::mismatch(left.begin(), left.end(), right.begin(), right.end());
        return pr.first == left.end() && pr.second == right.end();
    }
};
void test_ptr_gen(OP::utest::TestResult& tresult)
{
    using set_t = std::set <std::string >;
    using target_set_t = std::set <std::string_view >;
    set_t subset{ "aaa", "bbb", "ccc" }; 
    target_set_t result;
    using cstr_ptr = const std::string*;
    size_t invocked = 0;
    typename set_t::iterator current;
    for(const auto& r: OP::flur::src::generator([&](bool first) -> cstr_ptr {
        current = first ? subset.begin() : std::next(current);
        return current == subset.end()?nullptr : &*current;
    }))
    {
        //tresult.debug() << "gen:" << r << "\n";
        result.insert(r);
    }
    tresult.assert_that<eq_sets>(subset, result, "generator doesn't produce expected result");
}

static auto module_suite = OP::utest::default_test_suite("flur.generator")
->declare(test_noarg_gen, "noarg")
->declare(test_boolarg_gen, "bool-arg")
->declare(test_exception, "exceptions")
->declare(test_ptr_gen, "ptr");
} //ns:<anonymous>
