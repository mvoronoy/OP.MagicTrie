#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

void test_ThenOrderedDistinct(OP::utest::TestResult& tresult)
{
    std::multiset<std::string> test_multi_seq {"aa", "aa", "bb", "bb", "bb", "c", "xx", "xx"};

    auto r_uniq = src::of_container(/*copy*/test_multi_seq)
        >> then::distinct()
        ;
    
    tresult.assert_that<eq_sets>(
        r_uniq, std::set<std::string>(test_multi_seq.begin(), test_multi_seq.end()));


    //check distinct by predicate

    constexpr auto n_uniq = src::of_iota(1, 7)
        >> then::distinct([](const auto& prev, const auto& next){
            return (prev / 2) == (next / 2); 
        })
        ;
    tresult.assert_that<eq_sets>(
        n_uniq, std::set<int>{1, 2, 4, 6});
    
    auto empt_uniq1 = src::of_optional<int>()
        >> then::distinct([](const auto& prev, const auto& next){
            return (prev / 2) == (next / 2); 
        })
        ;
    tresult.assert_true(
        std::empty(empt_uniq1));
    auto empt_uniq2 = src::of_container(std::set<int>())
        >> then::distinct([](const auto& prev, const auto& next){
            return (prev / 2) == (next / 2); 
        })
        ;
    tresult.assert_true(
        std::empty(empt_uniq2));

    
    std::multiset<int> single_set{ {1} };
    auto single_uniq = src::of_container(std::cref(single_set))
        >> then::distinct()
        ;
    tresult.assert_that<eq_sets>(
        single_uniq, single_set);

    single_set.emplace(101);
    tresult.assert_that<equals>(std::distance(single_uniq.begin(), single_uniq.end()), 2);
    tresult.assert_that<eq_sets>(
        single_uniq, single_set);
}

void test_ThenUnorderedDistinct(OP::utest::TestResult& tresult)
{
    tresult.assert_exception<std::runtime_error>([](){
        auto r_non_uniq = src::of_container(/*copy*/std::vector<int>{})
        >> then::distinct()
        ;
        for (auto& i : r_non_uniq) {}
        }
    );
}

static auto module_suite = OP::utest::default_test_suite("flur.then")
    ->declare(test_ThenOrderedDistinct, "ordered-distinct")
    ->declare(test_ThenUnorderedDistinct, "unordered-distinct")
;