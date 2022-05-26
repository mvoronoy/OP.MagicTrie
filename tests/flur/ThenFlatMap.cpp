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
    {
        return f;
    }

    template<typename T>
    auto make_function(T const& f) ->
        typename std::enable_if<!std::is_function<T>::value && !std::is_bind_expression<T>::value, typename function_traits<decltype(&T::operator())>::function>::type
    {
        return static_cast<typename function_traits<decltype(&T::operator())>::function>(f);
    }

    // This overload is only used to display a clear error message in this case
    // A bind expression supports overloads so its impossible to determine
    // the corresponding std::function since several are viable
    template<typename T>
    auto make_function(T const& f) ->
        typename std::enable_if<std::is_bind_expression<T>::value, void>::type
    {
        static_assert(std::is_bind_expression<T>::value && false, "functional::make_function cannot be used with a bind expression.");
    }

}  // namespace functional
struct GenEmu
{
    int i, step;
    GenEmu(int arg) :step(0), i(arg) { std::cout << "I'm plain ctor(" << arg << ")\n"; }
    GenEmu(const GenEmu& other) :step(other.step), i(other.i) { std::cout << "I'm copy ctor(" << step << ", " << i << ")\n"; }
    GenEmu(GenEmu&& other) :step(other.step), i(other.i) { std::cout << "I'm rref ctor(" << step << ", " << i << ")\n"; }

    GenEmu& operator=(const GenEmu& other) {
        step = other.step; i = (other.i);
        std::cout << "I'm copy =(" << step << ", " << i << ")\n"; 
        return *this;
    }
    GenEmu& operator = (GenEmu&&other) 
    { 
        step = other.step; i = (other.i);

        std::cout << "I'm rref =(" << step << ", " << i << ")\n"; 
        return *this;
    }
    
    std::optional<int> operator()()
    {
        decltype(i) v = 1;
        for (auto x = 0; x < step; ++x)
            v *= i;
        return step++ < 3 ? std::optional<decltype(i)>(v) : std::optional<decltype(i)>{};
    }
};
void test_FlatMapFromPipeline(OP::utest::TestRuntime& tresult)
{
    constexpr int N = 4;

    constexpr auto fm_lazy = src::of_iota(1, N + 1)
        >> then::flat_mapping([](auto i) {
            return src::generator([step = 0, i]() mutable->std::optional<decltype(i)> 
            {
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
    for (auto i : fm_lazy)
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
        : _store(other._store)
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
void test_FlatMapFromContainer(OP::utest::TestRuntime& tresult)
{
    constexpr int N = 4;
    constexpr auto fm_lazy = src::of_iota(1, N + 1)
        >> then::flat_mapping([](auto i) {
        return src::of(
            ExploreVector<std::string>{
            "a" + std::to_string(i),
                "b" + std::to_string(i),
                "c" + std::to_string(i)});
            }
        );

    size_t cnt = 0;
    g_copied = 0, g_moved = 0;
    for (auto i : fm_lazy.compound())
    {
        tresult.debug() << i << ", ";
        ++cnt;
    }
    tresult.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";

    tresult.assert_that<equals>(cnt, 12, "Wrong times");
    tresult.assert_that<equals>(g_moved, 16, "Wrong times");
    tresult.assert_that<equals>(g_copied, 0, "Wrong times");

    tresult.info() << "test flat-map works with sequence\n";
    struct Some
    {
        OfContainerFactory<ExploreVector<std::string>> relates;
        Some() :relates{ ExploreVector{ "a"s, "b"s, "c"s} } {};
    };
    const auto super_factory = src::of_container(std::vector{ Some{}, Some{} })
        >> then::flat_mapping([](const auto& some_entry) {
                return some_entry.relates.compound();
            });
    cnt = 0, g_copied = 0, g_moved = 0;
    for (auto i : super_factory.compound())
    {
        tresult.debug() << i << ", ";
        ++cnt;
    }
    tresult.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";

    tresult.assert_that<equals>(cnt, 6, "Wrong times");
    tresult.assert_that<equals>(g_moved, 2, "Wrong times");
    tresult.assert_that<equals>(g_copied, 4, "Wrong times");
}

void test_FlatMapFromCref(OP::utest::TestRuntime& tresult)
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
        return src::of_container(std::cref(u.roles));
        });

    size_t cnt = 0;
    for (const auto& i : fmap)
    {
        tresult.debug() << i << ", ";
        ++cnt;
    }
    tresult.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";

    tresult.assert_that<equals>(cnt, 6, "Wrong times");
    tresult.assert_that<equals>(g_copied, 0, "Wrong copy times");
    tresult.assert_that<equals>(g_moved, 0, "Wrong rref move times");
}

void test_FlatMapWithEmpty(OP::utest::TestRuntime& tresult)
{
    struct User
    {
        User(std::initializer_list<std::string> init)
            : roles(std::move(init))
        {}
        ExploreVector<std::string> roles;
    };
    ;
    ExploreVector<std::string> expected{ "a1"s, "a2"s, "a3"s, "b1"s, "b2"s, "b3"s };

    auto lst1 =
        src::of_container(std::vector<User>{
            User{},
            User{ "a1"s, "a2"s, "a3"s },
            User{ "b1"s, "b2"s, "b3"s },
        })
        >> then::flat_mapping([](const auto& u) {
            return src::of_container(std::cref(u.roles));
            });

    tresult.assert_that<eq_sets>(expected, lst1, "result sequene broken by empty-first");

    auto lst2 =
        src::of_container(std::vector<User>{
            User{ "a1"s, "a2"s, "a3"s },
            User{},
            User{ "b1"s, "b2"s, "b3"s },
        })
        >> then::flat_mapping([](const auto& u) {
            return src::of_container(std::cref(u.roles));
        })
    ;
    tresult.assert_that<eq_sets>(expected, lst2, "result sequene broken by empty-moddle");

    auto lst3 =
        src::of_container(std::vector<User>{
            User{ "a1"s, "a2"s, "a3"s },
            User{ "b1"s, "b2"s, "b3"s },
            User{},
        })
        >> then::flat_mapping([](const auto& u) {
            return src::of_container(std::cref(u.roles));
        })
    ;
    tresult.assert_that<eq_sets>(expected, lst3, "result sequene broken by empty-moddle");
}

void test_FlatMapShared(OP::utest::TestRuntime& rt)
{
    using namespace OP::flur;
    auto shared_seq = make_shared(
        src::of_container(std::vector{ 1, 3, 5, 7 })
        >> then::flat_mapping([](auto odd) {
            ExploreVector<int> even{2, 4, 6};
            return make_shared(src::of_container(std::move(even)));
            })
    );
    g_copied = 0;
    g_moved = 0;
    for (auto n : *shared_seq)
        rt.debug() << n << ", ";
    rt.debug() << "\ncopied:" << g_copied << ", moved:" << g_moved << "\n";
    rt.assert_that<equals>(4, g_copied);
    rt.assert_that<equals>(16, g_moved);
}

static auto& module_suite = OP::utest::default_test_suite("flur.then")
.declare("flatmap", test_FlatMapFromPipeline)
.declare("rref-flatmap", test_FlatMapFromContainer)
.declare("cref-flatmap", test_FlatMapFromCref)
.declare("flatmap-with-empty", test_FlatMapWithEmpty)
.declare("flatmap-shared", test_FlatMapShared)
 ;