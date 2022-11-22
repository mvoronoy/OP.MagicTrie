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

void test_FlatMapArbitraryArgs(TestRuntime& tresult)
{
    //auto shared_seq = make_shared(
    //    src::of_container(std::vector{ 1, 3, 5, 7 })
    //    >> then::flat_mapping([](const int& n, const PipelineAttrs& attrs) {
    //        ExploreVector<int> even{ 2, 4, 6 };
    //        return make_shared(src::of_container(std::move(even)));
    //        })
    //);

}


namespace fldet = OP::flur::details;

template <class R, class T>
auto sh_new(T&& t)
{
    return std::shared_ptr<R>(new T(std::move(t)));
}
template <class T>
auto* mv_new(T&& t)
{
    return new T(std::move(t));
}

template <class T, class F1, class F2>
/*result_t*/constexpr auto ppln_factory(T&& elem, F1 f1, F2& f2)
{
    //std::cout << "\n" << std::setw(4 - _lev) << ' ';
    auto val = src::of_value(elem);
    auto fl = src::of_value(std::forward<T>(elem))
        >> then::flat_mapping(std::move(f1))
        >> then::flat_mapping(std::move(f2))
        ;
    return val
        >> then::union_all(std::move(fl))                   
        ;
}

template <class T, class F>
struct DeepFirstFunctor
{
    using this_t = DeepFirstFunctor<T, F>;
    using raw_applicator_result_t = decltype(std::declval<F>()(std::declval<const T&>()));
    using applicator_result_t = std::decay_t<raw_applicator_result_t>;

    F _applicator;
    int _lev;
    using applicator_element_t = typename fldet::sequence_type_t<applicator_result_t>::element_t;
    //static_assert(
    //    std::is_same_v< T, applicator_element_t>,
    //    "must operate on sequences producing same type elements");
    using value_seq_t = OfValue<T>;

    using poly_result_t = AbstractPolymorphFactory<const T&>;
    using result_t = std::shared_ptr<poly_result_t>;

    constexpr DeepFirstFunctor(F f, int lev = 3) noexcept
        : _applicator(std::move(f))
        , _lev(lev)
    {}

    
    result_t operator ()(const std::string& elem) const
    {
        std::cout << "\n" << std::setw(4 - _lev) << ' ';
        auto val = src::of_value(elem);
        auto fl = src::of_value(std::move(elem))
            >> then::flat_mapping(_applicator)
            >> then::flat_mapping(std::function(this_t(_applicator, _lev - 1)));
        return OP::flur::make_shared(std::move(val)
            >> then::union_all(std::move(fl)))
            ;
    }
};
#include <op/common/ftraits.h>

template <class F>
struct BroadFirstFactory : FactoryBase
{
    using this_t = BroadFirstFactory<F>;
    using applicator_traits_t = OP::utils::function_traits<F>;
    using raw_applicator_result_t = typename applicator_traits_t::result_t;
    using applicator_result_t = std::decay_t<raw_applicator_result_t>;
    F _applicator;
    int _lev;
    using applicator_element_t = typename fldet::sequence_type_t<applicator_result_t>::element_t;

    constexpr BroadFirstFactory(F f, int lev = 3) noexcept
        : _applicator(std::move(f))
        , _lev(lev)
    {}

    template <class Src, class F, 
        class Base = Sequence< typename OP::flur::details::sequence_type_t<Src>::element_t > >
    struct ProxySequence : Base
    {
        using base_t = Base;
        using element_t = typename base_t::element_t;
        using traits_t = FlatMapTraits<F, Src>;
        Src _origin;
        
        using flat_element_t = OP::flur::details::sequence_type_t<
            typename traits_t::applicator_result_t>;
        using then_vector_t = std::vector<flat_element_t>;

        then_vector_t _gen1, _gen2;
        bool _is_gen0 = true;
        size_t _gen1_idx = 0;
        F _applicator;

        constexpr ProxySequence(Src&& rref, F applicator) noexcept
            : _origin(std::move(rref))
            , _applicator(std::move(applicator))
        {
        }

        OP_VIRTUAL_CONSTEXPR bool is_sequence_ordered() const override
        {
            return false;
        }

        virtual void start()
        {
            _is_gen0 = true;
            _gen1_idx = 0;
            _gen1.clear(), _gen2.clear();
            _origin.start();
            drain();
        }

        virtual bool in_range() const override
        {
            if (_is_gen0)
            {
                return _origin.in_range();
            }
            else return (_gen1_idx < _gen1.size());
        }

        virtual element_t current() const override
        {
            if (_is_gen0)
            {
                return _origin.current();
            }
            else 
            {
                return 
                    _gen1[_gen1_idx].current();
            }
        }

        virtual void next() override
        {
            if (_is_gen0)
            {
                _origin.next();
            }
            else
            {
                auto& at = _gen1[_gen1_idx];
                assert(at.in_range());
                at.next();
                if( !at.in_range() )
                {
                    ++_gen1_idx;
                    seek_gen1();
                }
            }
            drain();
        }

    private:
        void drain()
        {
            if (in_range())
            {
                _gen2.emplace_back(_applicator(current()).compound());
                return;
            }
            std::swap(_gen1, _gen2);
            _is_gen0 = false;
            _gen2.clear();
            _gen1_idx = 0;
            seek_gen1();
        }

        void seek_gen1()
        {
            for (; _gen1_idx < _gen1.size(); ++_gen1_idx)
            { //find first non-empty sequence for generation-1 leftovers
                auto& at = _gen1[_gen1_idx];
                at.start();
                if (at.in_range())
                {
                    _gen2.emplace_back(_applicator(at.current()).compound());
                    return;
                }
            }
            //no entries in gen_1, EOS
            _gen1.clear();
        }
    };//proxy

    template <class Src>
    constexpr auto compound(Src&& seq) const
    {
        static_assert(
            std::is_same_v< typename OP::flur::details::dereference_t<Src>::element_t, applicator_element_t>,
            "must operate on sequences producing same type elements");
        return ProxySequence<Src, F>(std::move(seq), _applicator);
    }
};

template <class F>
constexpr auto broad_first(F f) noexcept
{
    using f_t = std::decay_t<F>;
    return BroadFirstFactory<f_t>(std::move(f));
}

void test_FlatMapRec(OP::utest::TestRuntime& rt)
{
    std::vector<std::string> src{ "a", "b", "c" };
    size_t n = 3;
    auto child_str_no_mor3 = [&](const std::string& t) ->decltype(auto)
    {
        std::vector<std::string> res;
        if (t.size() < 13)
        {
            for (auto i = 0; i < 3; ++i)
                res.emplace_back(t + ".(" + std::to_string(i) + ")");
        }
        return src::of_container(std::move(res));
    };
    using f_t = DeepFirstFunctor<std::string, decltype(child_str_no_mor3)>;
    auto ll = src::of_container(src)
        >> then::flat_mapping(f_t(child_str_no_mor3));

    for (auto x : ll)
        /*rt.debug()*/std::cout << x << ", ";
    std::cout << "\n";
    std::cout << "=============\n";
    n = 3;
    auto bf = src::of_container(src) >> broad_first(child_str_no_mor3);
    for (auto x : bf)
        /*rt.debug()*/std::cout << x << ", ";
}

static auto& module_suite = OP::utest::default_test_suite("flur.then")
.declare("flatmap", test_FlatMapFromPipeline)
.declare("rref-flatmap", test_FlatMapFromContainer)
.declare("cref-flatmap", test_FlatMapFromCref)
.declare("flatmap-with-empty", test_FlatMapWithEmpty)
.declare("flatmap-shared", test_FlatMapShared)
.declare("flatmap-arb_args", test_FlatMapArbitraryArgs)
.declare("flatmap-rec", test_FlatMapRec)
 ;