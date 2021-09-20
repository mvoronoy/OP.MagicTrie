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

struct ConstructorType 
{
    enum {
        def = 0,
        copy = 0x1,
        move = 0x2,
        _last_
    };
};
struct TestCopySensetive
{
    std::array<size_t, ConstructorType::_last_> _tracks{ 0, 0, 0 };
    constexpr TestCopySensetive() noexcept
    {
        ++_tracks[ConstructorType::def];
    }
    constexpr TestCopySensetive(const TestCopySensetive& r)
        :_tracks(r._tracks)
    {
        ++_tracks[ConstructorType::copy];
    }
    constexpr TestCopySensetive(TestCopySensetive&& r) noexcept
        :_tracks(std::move(r._tracks))
    {
        ++_tracks[ConstructorType::move];
    }

};
void test_Vector(OP::utest::TestResult& tresult)
{
    using sens_vectror_t = std::vector<TestCopySensetive>;
    auto r = src::of_container(sens_vectror_t(3));
    tresult.info() << "Test no redundant copies\n";
    r.for_each([&](auto const& i) {
        tresult.assert_that<equals>(i._tracks[ConstructorType::copy], 1, "You don't control how many times value has been copied");
        });

    tresult.info() << "Test processing by refernce std::cref\n";
    sens_vectror_t local(3);
    auto r1 = src::of_container(std::cref(local));
    r1.for_each([&](auto const& i) {
        tresult.assert_that<equals>(i._tracks[ConstructorType::copy], 0, "You don't control how many times value has been copied");
        });
    
}
void test_Iota(OP::utest::TestResult& tresult)
{
    constexpr auto pipeline = src::of_iota(1, 3);
    static_assert(pipeline.compound().ordered_c, "iota must produce ordered sequence");

    auto r = pipeline.reduce<size_t>( [](auto&& r, auto current){return r + current; });
    tresult.assert_that<equals>(3, r, "Wrong reduce");

    tresult.assert_that<equals>(100500, src::of_iota(11, 11).reduce<size_t>(
        [](auto&& r, auto current) {return r + current; }, 100500),
        "default with reduce failed");
}

void test_Map(OP::utest::TestResult& tresult)
{
    using tst_map_t = std::map<char, float>;
    using element_t = typename tst_map_t::value_type;
    tst_map_t source1{ {'a', 1.f}, {'b', 1.2f} };
    auto r = src::of_container(std::cref(source1));
    tresult.assert_true(r.compound().ordered_c, "Map must produce ordered sequence");
    //just check generic form works
    tresult.assert_true(src::of(std::cref(source1)).compound().ordered_c, "Set must support simple construction");

    std::function<float(float, float)> freduce = reducer::sum<float>;
    tresult.assert_that<equals>(2.2f, 
        (
            r >> then::mapping([](const element_t& i) -> float {return i.second; })
        ).OP_TEMPL_METH(reduce)<float>(freduce),
        "Wrong sum");
}
void test_Set(OP::utest::TestResult& tresult)
{
    using tst_set_t = std::set<std::string>;
    auto r = src::of_container(tst_set_t{ "a"s, "ab"s, ""s });
    tresult.assert_true(r.compound().ordered_c, "Set must produce ordered sequence");
    //just check generic form works
    tresult.assert_true(src::of(tst_set_t{ ""s}).compound().ordered_c, "Set must support simple construction");
    //count summary str length
    tresult.assert_that<equals>(3,
        r.reduce<size_t>([](size_t ini, const auto& i) { return ini + i.size();  }),
        "Wrong sum of length"
        );
    const std::string* prev = nullptr;
    r.for_each([&](const auto& i) {
        tresult.assert_true(!prev || (*prev) < i, "order broken");
        prev = &i;
        });
}
void test_Optional(OP::utest::TestResult& tresult)
{
    constexpr auto r = src::of_optional(57.f);
    static_assert(r.compound().ordered_c, "Optional must produce ordered sequence");

    constexpr auto r_empty = src::of_optional(std::optional<float>());
    static_assert(r_empty.compound().ordered_c, "Empty optional must produce ordered sequence");

    tresult.assert_that<equals>(57.f,
        r.reduce<float>([](float ini, const auto& i) { return ini + i;  }),
        "Wrong sum of 1 item sequence"
        );

    tresult.assert_that<equals>(100500.f,
        r_empty.reduce([](float ini, const auto& i) { return ini + 11 + i;  }, 100500.f),
        "Wrong sum of empty sequence"
        );
}

void test_Value(OP::utest::TestResult& tresult)
{
    constexpr auto r_bool = src::of_value(false);
    static_assert(r_bool.compound().ordered_c, "bool-singleton must produce ordered sequence");

    tresult.assert_that<equals>(1, r_bool.count(), "Wrong count of single value");

    tresult.assert_that<equals>(42, src::of_value(42).reduce<int>(OP::flur::reducer::sum<int>),
        "Wrong count of single value");
}
void test_Iterate(OP::utest::TestResult& tresult)
{
    using tst_map_t = std::map<char, float>;
    using element_t = typename tst_map_t::value_type;
    tst_map_t source1{ {'a', 1.f}, {'b', 1.2f} };
    auto r = src::of(std::cref(source1));
    float sum = 0.f;
    for (const auto& pair : r)
    {
        sum += pair.second;
    }
    tresult.assert_that<equals>(2.2f, sum, "Wrong sum");
}

static auto module_suite = OP::utest::default_test_suite("flur.stl")
->declare(test_Vector, "vector")
->declare(test_Map, "map")
->declare(test_Set, "set")
->declare(test_Optional, "optional")
->declare(test_Value, "value")
->declare(test_Iota, "iota")
->declare(test_Iterate, "iterate")
;