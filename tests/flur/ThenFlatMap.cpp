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
#include <op/flur/Reducer.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

void test_FlatMapFromPipeline(OP::utest::TestResult& tresult)
{
    constexpr int N = 4;
    constexpr auto fm_lazy = src::of_iota(1, N+1)
        >> then::flat_mapping([](auto i) {
            return src::generator([step = 0, i]() mutable->std::optional<decltype(i)> {
                decltype(i) v = 1;
                for (auto x = 0; x < step; ++x)
                    v *= i;
                return step++ < 3 ? std::optional<decltype(i)>(v) : std::optional<decltype(i)>{};
            }
        );
            
        })
        ;
    
    constexpr int expected_sum = N + N * (N + 1) * (N + 2) / 3;
    size_t cnt = 0;
    fm_lazy.for_each([&](auto i) {
        tresult.debug() << i << "\n";
        ++cnt;
        });
    tresult.assert_that<equals>(cnt, 12, "Wrong times");
    tresult.assert_that<equals>(expected_sum, fm_lazy.OP_TEMPL_METH(reduce)<int>(reducer::sum<int>), "invalid num");
}

size_t g_copied = 0, g_moved = 0;

/**Emulate STL container to count how many times container been copied or moved.
Before use reset global variables: g_copied = 0, g_moved = 0
*/
template <class T>
struct ExploreVector
{
    ExploreVector(std::initializer_list<T> init)
        : _store(std::move(init))
    {

    }
    ExploreVector(const ExploreVector<T>& other)
        :  _store(other._store)
    {
        ++g_copied;
    }
    ExploreVector(ExploreVector<T>&& other) noexcept
        : _store(std::move(other._store))
    {
        ++g_moved;
    }
    
    auto begin() const
    {
        return _store.begin();
    }
    auto end() const
    {
        return _store.end();
    }
    
    std::vector<T> _store;
};
void test_FlatMapFromContainer(OP::utest::TestResult& tresult)
{
    constexpr int N = 4;
    constexpr auto fm_lazy = src::of_iota(1, N + 1)
        >> then::flat_mapping([](auto i) {

        return rref_container(
            ExploreVector<std::string>{
            "a" + std::to_string(i),
                "b" + std::to_string(i),
                "c" + std::to_string(i)});
            })
        ;

    size_t cnt = 0;
    g_copied = 0, g_moved = 0;
    fm_lazy.for_each([&](auto i) {
        tresult.debug() << i << ", ";
        ++cnt;
        });
    tresult.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";

    tresult.assert_that<equals>(cnt, 12, "Wrong times");
    tresult.assert_that<equals>(g_moved, 16, "Wrong times");
    tresult.assert_that<equals>(g_copied, 0, "Wrong times");
}

void test_FlatMapFromCref(OP::utest::TestResult& tresult)
{
    struct User
    {
        User(std::initializer_list<std::string> init)
            : roles(std::move(init))
        {}
        ExploreVector<std::string> roles;
    };
    std::vector<User> usr_lst{
        User{"a1"s, "a2"s, "a3"s},
        User{"b1"s, "b2"s, "b3"s},
    };
    g_copied = 0;
    g_moved = 0;
    auto users = src::of_container(std::cref(usr_lst));
    auto  fmap = users >> then::flat_mapping([](const auto& u) {
        return cref_container(u.roles);
    });

    size_t cnt = 0;
    fmap.for_each([&](const auto& i) {
        tresult.debug() << i << ", ";
        ++cnt;
        });
    tresult.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";

    tresult.assert_that<equals>(cnt, 6, "Wrong times");
    tresult.assert_that<equals>(g_copied, 0, "Wrong copy times");
    tresult.assert_that<equals>(g_moved, 0, "Wrong rref move times");
}


static auto module_suite = OP::utest::default_test_suite("flur.then")
->declare(test_FlatMapFromPipeline, "flatmap")
->declare(test_FlatMapFromContainer, "rref-flatmap")
->declare(test_FlatMapFromCref, "cref-flatmap")
;