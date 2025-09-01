#include <map>
#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

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


void test_Vector(OP::utest::TestRuntime& tresult)
{
    using sens_vector_t = std::vector<TestCopySensetive>;
    auto r = src::of_container(sens_vector_t(3));

    tresult.info() << "Test no redundant copies\n";
    for (auto const& i : r)
    {
        tresult.assert_that<equals>(
            i._tracks[ConstructorType::copy], 0, "You don't control how many times value has been copied");
    }
    
    tresult.info() << "Test no redundant copies for&&\n";
    for (auto const& i : src::of_container(sens_vector_t(3)).compound())//enforce `compound()&&` notation
    {
        tresult.assert_that<equals>(
            i._tracks[ConstructorType::copy], 0, "You don't control how many times value has been copied");
        tresult.assert_that<equals>(
            i._tracks[ConstructorType::move], 0, "You don't control how many times value has been moved");
    }

    tresult.info() << "Test processing by reference std::cref\n";
    sens_vector_t local(3);
    auto r1 = src::of_container(std::cref(local));
    for (auto const& i : r1)
    {
        tresult.assert_that<equals>(
            i._tracks[ConstructorType::copy], 0, "You don't control how many times value has been copied");
    }

    tresult.info() << "Test forcibly processing by value (Intrinsic::result_by_value)\n";
    auto by_val = src::of_container<Intrinsic::result_by_value>(sens_vector_t(3));
    for (auto const& i : by_val)
    {
        tresult.assert_that<equals>(
            i._tracks[ConstructorType::copy], 1, "You don't control how many times value has been copied (Intrinsic::result_by_value)");
    }

}

void test_Iota(OP::utest::TestRuntime& tresult)
{
    constexpr auto pipeline = src::of_iota(1, 3);
    tresult.assert_true(pipeline.compound().is_sequence_ordered(), "iota must produce ordered sequence");

    auto r = std::reduce(pipeline.begin(), pipeline.end(), 0);
    tresult.assert_that<equals>(3, r, "Wrong reduce");

    tresult.assert_that<equals>(100500, OP::flur::reduce(src::of_iota(11, 11), 100500),
        "default with reduce failed");

    tresult.assert_that<equals>(-45, src::of_iota(0, -10, -1) >>= apply::sum(),
        "negative range failed");

}

void test_Map(OP::utest::TestRuntime& tresult)
{
    using tst_map_t = std::map<char, float>;
    using element_t = typename tst_map_t::value_type;
    tst_map_t source1{ {'a', 1.f}, {'b', 1.2f} };
    auto r = src::of_container(std::cref(source1));
    tresult.assert_true(
        r.compound().is_sequence_ordered(), 
        "Map must produce ordered sequence");
    //just check generic form works
    tresult.assert_true(
        src::of_container(std::cref(source1)).compound().is_sequence_ordered(), 
        "Set must support simple construction");

    tresult.assert_that<equals>(2.2f,
        OP::flur::reduce(r >> then::mapping([](const element_t& i) -> float {return i.second; }), 0.f),
        "Wrong sum");
        
}

void test_Set(OP::utest::TestRuntime& tresult)
{
    using tst_set_t = std::set<std::string>;
    auto r = src::of_container(tst_set_t{ "a"s, "ab"s, ""s });
    tresult.assert_true(r.compound().is_sequence_ordered(), "Set must produce ordered sequence");
    //just check generic form works
    tresult.assert_true(
        src::of_container(tst_set_t{ ""s }).compound().is_sequence_ordered(), 
        "Set must support simple construction");
    //count summary str length
    tresult.assert_that<equals>(3,
        OP::flur::reduce(r >> then::mapping([](const auto& s) { return s.size(); }), size_t{}),
        "Wrong sum of length"
        );
    std::string prev;
    for(const auto& i : r)  //usage of compound there is mandatory otherwise prev keep current only
    {
        tresult.assert_true(prev.empty() || prev < i, "order broken");
        prev = i;
    }
}

void test_Optional(OP::utest::TestRuntime& tresult)
{   
    constexpr auto r = src::of_optional(std::optional(57.f));
    tresult.assert_true(r.compound().is_sequence_ordered(), 
        "Optional must produce ordered sequence");

    tresult.assert_that<equals>(2*57.f,
        OP::flur::reduce(r, 57.f),
        "Wrong sum of 1 item sequence"
        );

    constexpr auto r_empty = src::of_optional(std::optional<float>());
    tresult.assert_true(r_empty.compound().is_sequence_ordered(), 
        "Empty optional must produce ordered sequence");

    tresult.assert_that<equals>(57.f,
        OP::flur::reduce(r_empty, 57.f),
        "Wrong sum of 0 item sequence"
        );

    
}

void test_OptionalPtr(OP::utest::TestRuntime& tresult)
{   

    auto r = src::of_optional(std::make_shared<float>(57.f));
    tresult.assert_true(r.compound().is_sequence_ordered(), 
        "Optional must produce ordered sequence");

    tresult.assert_that<equals>(2*57.f,
        OP::flur::reduce(r, 57.f),
        "Wrong sum of 1 item sequence"
        );


    tresult.assert_that<equals>(0.f,
        src::of_optional(std::unique_ptr<float>()) >>= OP::flur::apply::fsum(),
        "Wrong sum of 0 item sequence"
    );


    tresult.assert_that<equals>(57.f,
        src::of_optional(std::make_unique<float>(57.f)) >>= OP::flur::apply::fsum(),
        "Wrong sum of 1 item sequence"
        );
}

void test_Value(OP::utest::TestRuntime& tresult)
{
    constexpr auto r_bool = src::of_value(false);
    tresult.assert_true(
        r_bool.compound().is_sequence_ordered(), 
        "bool-singleton must produce ordered sequence");

    tresult.assert_that<equals>(1, std::distance(r_bool.begin(), r_bool.end()), "Wrong count of single value");

    tresult.assert_that<equals>(42, OP::flur::reduce(src::of_value(42), 0.f),
        "Wrong count of single value");
}
void test_Iterate(OP::utest::TestRuntime& tresult)
{
    using tst_map_t = std::map<char, float>;
    using element_t = typename tst_map_t::value_type;
    tst_map_t source1{ {'a', 1.f}, {'b', 1.2f} };
    auto r = src::of_container(std::cref(source1));
    float sum = 0.f;
    for (const auto& pair : r)
    {
        sum += pair.second;
    }
    tresult.assert_that<equals>(2.2f, sum, "Wrong sum");
    std::vector<std::string> vec{"one", "two", "three"};
    auto check_ref = src::of_container(std::cref(vec));
    auto term = [&](const std::string& k)->bool
    {
        return  check_ref.end() != 
            std::find_if(check_ref.begin(), check_ref.end(), 
                [&](const auto& l) {
            return &l == &k;
            });
    };
    
    std::for_each(
        check_ref >> then::mapping([&](const auto& i)->const std::string& {
            tresult.debug() << i << ", is same:" << term(i) << "\n";
            return i; 
            }),
        [&](const auto& t)
    {
        tresult.debug() << "\t"  << t << ", is same:" << term(t) << "\n";
    });
}

static auto& module_suite = OP::utest::default_test_suite("flur.stl")
.declare("vector", test_Vector)
.declare("map", test_Map)
.declare("set", test_Set)
.declare("optional", test_Optional)
.declare("optional_ptr", test_OptionalPtr)
.declare("value", test_Value)
.declare("iota", test_Iota)
.declare("iterate", test_Iterate)
;