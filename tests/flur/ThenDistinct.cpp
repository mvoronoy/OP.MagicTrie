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

void test_ThenOrderedDistinct(OP::utest::TestRuntime& tresult)
{
    std::multiset<std::string> test_multi_seq {"aa", "aa", "bb", "bb", "bb", "c", "xx", "xx"};

    auto r_uniq = src::of_container(/*copy*/test_multi_seq)
        >> then::ordered_distinct()
        ;
    
    tresult.assert_that<eq_sets>(
        r_uniq, std::set<std::string>(test_multi_seq.begin(), test_multi_seq.end()));
    tresult.assert_true(r_uniq.compound().is_sequence_ordered());

    //check distinct by predicate

    constexpr auto n_uniq = src::of_iota(1, 7)
        >> then::ordered_distinct([](const auto& prev, const auto& next){
            return (prev / 2) == (next / 2); 
        })
        ;
    tresult.assert_that<eq_sets>(
        n_uniq, std::set<int>{1, 2, 4, 6});
    
    auto empt_uniq1 = src::of_optional<int>()
        >> then::ordered_distinct([](const auto& prev, const auto& next){
            return (prev / 2) == (next / 2); 
        })
        ;
    tresult.assert_true(
        std::empty(empt_uniq1));
    auto empt_uniq2 = src::of_container(std::set<int>())
        >> then::ordered_distinct([](const auto& prev, const auto& next){
            return (prev / 2) == (next / 2); 
        })
        ;
    tresult.assert_true(
        std::empty(empt_uniq2));

    
    std::multiset<int> single_set( {1} );
    auto single_uniq = src::of_container(std::cref(single_set))
        >> then::ordered_distinct()
        ;
    tresult.assert_that<eq_sets>(
        single_uniq, single_set);

    single_set.emplace(101);
    tresult.assert_that<equals>(std::distance(single_uniq.begin(), single_uniq.end()), 2);
    tresult.assert_that<eq_sets>(
        single_uniq, single_set);
    tresult.assert_exception<std::runtime_error>([&]() {
        for (auto i : src::of_container(std::vector<int>{2, 1})
            >> then::ordered_distinct())
        {
            tresult.fail("Exception must be already raised");
        }
    });
}

struct TestPair 
{
    std::pair<int, int> _v;
    constexpr TestPair()
        :_v(0, 0)
    {
    }
    constexpr TestPair(int v)
        :_v(v, v / 2)
    {
    }
    constexpr operator int() const
    {
        return _v.second;
    }
};

namespace std
{
    template<> struct hash<TestPair >
    {
        std::size_t operator()(TestPair  const& n) const noexcept
        {
            return static_cast<std::size_t>(n._v.second );
        }
    };
}

void test_ThenUnorderedDistinct(OP::utest::TestRuntime& tresult)
{
    auto r_empt = src::of_container(/*copy*/std::vector<int>{})
        >> then::unordered_distinct()
        ;
    tresult.assert_true(std::empty(r_empt));

    auto r_sing = src::of_container(std::vector<int>{1})
        >> then::unordered_distinct()
        ;
    tresult.assert_that<eq_sets>(r_sing, std::vector{1});

    auto r_sing2 = src::of_container(std::vector<int>{1, 1, 1, 1, 1, 1})
        >> then::unordered_distinct()
        ;
    tresult.assert_that<eq_sets>(r_sing2, std::vector{ 1 });

    //test all-uniq set
    auto r_ord = src::of_container(std::set<int>{1, 2, 3, 4, 5, 6})
        >> then::unordered_distinct()
        ;
    tresult.assert_true(r_ord.compound().is_sequence_ordered());
    tresult.assert_that<eq_sets>(r_ord, std::vector{ 1, 2, 3, 4, 5, 6 });

    constexpr size_t N = 57;
    std::vector<int> rand_vec;
    rand_vec.reserve(N * 2);
    for (int i = 0; i < N; ++i)
    {
        rand_vec.emplace_back(i);
        rand_vec.emplace_back(i);
    }
    std::random_device rd;
    std::mt19937 g(rd());

    std::shuffle(rand_vec.begin(), rand_vec.end(), g);

    //make unique exemplar set
    std::set<int> exemplar(rand_vec.begin(), rand_vec.end());

    tresult.assert_that<eq_unordered_sets>(
        src::of_container(std::cref(rand_vec))
        >> then::unordered_distinct(), 
        exemplar);

    // test the same with custom eq 

    auto unordered_ppline = src::of_iota(1, 11)
        >> then::mapping([](const auto& n) {return TestPair(n); })
        >> then::unordered_distinct(
            [](const auto& prev, const auto& next) {
                return prev._v.second == next._v.second;
            }
    );

    tresult.assert_that<eq_unordered_sets>(
        unordered_ppline >> then::mapping([](const auto& p) {return p._v.first; }),
        std::vector{ {1, 2, 4, 6, 8, 10} });

}

void test_ThenAutoDistinct(OP::utest::TestRuntime& tresult)
{
    tresult.assert_that<eq_sets>(
        src::of_container(std::vector{1, 2, 2, 5, 3, 3, 2}) >> then::auto_distinct(),
        std::vector{ {1, 2, 5, 3} });

    tresult.assert_that<eq_sets>(
        src::of_container(std::multiset{ 1, 2, 2, 5, 3, 3, 2 }) >> then::auto_distinct(),
        std::vector{ {1, 2, 3, 5} });
}

static auto& module_suite = OP::utest::default_test_suite("flur.then")
    .declare("ordered-distinct", test_ThenOrderedDistinct)
    .declare("unordered-distinct", test_ThenUnorderedDistinct)
    .declare("auto-distinct", test_ThenAutoDistinct)
;