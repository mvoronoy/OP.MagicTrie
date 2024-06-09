#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <numeric>
#include <vector>
#include <string>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

OP::utils::ThreadPool& my_global_pool()
{
    static OP::utils::ThreadPool p;
    return p;
}

void test_OrderingFlatBasic(OP::utest::TestRuntime& tresult)
{
    constexpr int N = 4;

    //create top-unordered 
    std::vector<int> unord_data(1000);
    std::iota(unord_data.begin(), unord_data.end(), 1);
    std::shuffle(
        unord_data.begin(), unord_data.end(), 
        tools::RandomGenerator::instance().generator());
 
    auto ofm_lazy = 
        src::of_container(std::cref(unord_data))
        >> then::ordering_flat_mapping([](int i) {
            return src::of_value(i);
        })
    ;
    
    int start = 1;
    for(auto check : ofm_lazy)
    {
        tresult.assert_that<equals>(start++, check, 
            OP_CODE_DETAILS("order has been violated"));
    }
    auto lazy_with_gap = src::of_container(std::cref(unord_data))
        >> then::ordering_flat_mapping([](int i) {
            std::set<int> result; //overhead, but simplest way to indicate ordering
            if(i & 1)  //populate only odds
            {
                result.emplace(i);
                result.emplace(i+1);
            }
            return src::of_container(std::move(result));
        });
    start = 1;
    for (auto check : lazy_with_gap)
    {
        tresult.assert_that<equals>(start++, check,
            OP_CODE_DETAILS("order has been violated"));
    }
    tresult.info() << "Test on empty sequence...\n";
    for (auto _ : src::null<int>() 
        >> then::ordering_flat_mapping([](int i) {
            std::set<int> result{ 1, 2, 3 }; //the way to indicate ordering seq
            return src::of_container(std::move(result));
        }))
    {
        tresult.fail("`null` must not generate any item");
    }
    tresult.info() << "Test all mappings empty...\n";
    for (auto _ : src::of_value(1)
        >> then::ordering_flat_mapping([](int i) {
            //std::set - the way to indicate ordering seq
            return src::of_container(std::set<int>{});
        }))
    {
        tresult.fail("empty set must not generate any item");
    }
}

void test_ExcptionOnNoOrd(TestRuntime& tresult)
{
    auto exception_seq = src::of_value(1)
        >> then::ordering_flat_mapping(
            [](auto _){
                return src::of_container(std::vector<int>{2, 1, 3});
            });
    for(auto i : exception_seq)
    {}
    tresult.fail("Exception must be raised when no-ordered sequence evaluated");
}

static auto& module_suite = OP::utest::default_test_suite("flur.then")
    .declare("ord-flatmap", test_OrderingFlatBasic)
    .declare_exceptional("ord-flatmap-no-ord", test_ExcptionOnNoOrd)
 ;