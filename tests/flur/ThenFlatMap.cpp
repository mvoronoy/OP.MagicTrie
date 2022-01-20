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

namespace functional {
    template <typename Function> struct function_traits;

    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType(ClassType::*)(Args...) const> {
        using function = const std::function<ReturnType(Args...)>;
    };

    // Non-const version, to be used for function objects with a non-const operator()
    // a rare thing
    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType(ClassType::*)(Args...)> {
        using function = std::function<ReturnType(Args...)>;
    };

    template<typename T>
    auto make_function(T const& f) ->
        typename std::enable_if<std::is_function<T>::value && !std::is_bind_expression<T>::value, std::function<T>>::type
    { return f; }

    template<typename T>
    auto make_function(T const& f) ->
        typename std::enable_if<!std::is_function<T>::value && !std::is_bind_expression<T>::value, typename function_traits<decltype(&T::operator())>::function>::type
    { return static_cast<typename function_traits<decltype(&T::operator())>::function>(f); }

    // This overload is only used to display a clear error message in this case
    // A bind expression supports overloads so its impossible to determine
    // the corresponding std::function since several are viable
    template<typename T>
    auto make_function(T const& f) ->
        typename std::enable_if<std::is_bind_expression<T>::value, void>::type
    { static_assert(std::is_bind_expression<T>::value && false, "functional::make_function cannot be used with a bind expression."); }

}  // namespace functional

void test_FlatMapFromPipeline(OP::utest::TestResult& tresult)
{
    constexpr int N = 4;
    
    constexpr auto fm_lazy = src::of_iota(1, N + 1)
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
    for(auto i : fm_lazy)
    {
        tresult.debug() << i << "\n";
        ++cnt;
    }
    tresult.assert_that<equals>(cnt, 12, "Wrong times");
    tresult.assert_that<equals>(expected_sum, std::reduce(fm_lazy.begin(), fm_lazy.end(), 0), "invalid num");
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
    for(auto i : fm_lazy) 
    {
        tresult.debug() << i << ", ";
        ++cnt;
    }
    tresult.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";

    tresult.assert_that<equals>(cnt, 12, "Wrong times");
    tresult.assert_that<equals>(g_moved, 12, "Wrong times");
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
    auto fmap = users >> then::flat_mapping([](const auto& u) {
        return cref_container(u.roles);
    });

    size_t cnt = 0;
    for(const auto& i : fmap) 
    {
        tresult.debug() << i << ", ";
        ++cnt;
    }
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