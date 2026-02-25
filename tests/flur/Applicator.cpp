#include <vector>
#include <array>
#include <numeric>
#include <string>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

namespace {
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace std::string_literals;

    void test_sum(TestRuntime& rt)
    {
        short start = 17;
        rt.assert_that<equals>(17, src::null<int>() >>= apply::sum(start));
        rt.assert_that<equals>(17, start);
        rt.assert_that<equals>(0., src::null<double>() >>= apply::sum());

        rt.assert_that<equals>(6, src::of_container(std::array{1, 2, 3}) >>= apply::sum());
        start = 1;
        rt.assert_that<equals>(7, src::of_container(std::array{ 1, 2, 3 }) >>= apply::sum(start));
        rt.assert_that<equals>(7, start);

        //test alterantive operator
        start = 1;
        rt.assert_that<equals>(24,
            src::of_iota(1, 5) >>= apply::sum<std::multiplies>(start));
        rt.assert_that<equals>(24, start);
        start = 0;
        rt.assert_that<equals>(0,
            src::of_iota(1, 5) >>= apply::sum<std::multiplies>(start));
        rt.assert_that<equals>(0, start);
    }

    void test_count(TestRuntime& rt)
    {
        short start = 17;
        rt.assert_that<equals>(17, src::null<int>() >>= apply::count(start));
        rt.assert_that<equals>(17, start);
        rt.assert_that<equals>(0., src::null<double>() >>= apply::count());

        const auto sample = std::array{ 1, 2, 3 };
        rt.assert_that<equals>(3, 
            src::of_container(sample) >>= apply::count());
        start = 1;
        rt.assert_that<equals>(4, 
            src::of_container(sample) >>= apply::count(start));
        rt.assert_that<equals>(4, start);
    }

    static size_t g_invoke_count = 0;
    void test_f(int)
    {
        ++g_invoke_count;
    }

    void test_for_each(TestRuntime& rt)
    {
        size_t count = 17;
        src::null<int>() >>= apply::for_each([&](const auto&) {++count; });
        rt.assert_that<equals>(17, count);

        g_invoke_count = 0;
        src::null<int>() >>= apply::for_each(test_f);
        rt.assert_that<equals>(0, g_invoke_count);

        size_t sum = 0;
        count = 0;
        src::of_iota(1, 4) >>= apply::for_each([&](const auto& x) {++count; sum += x; });
        rt.assert_that<equals>(3, count);
        rt.assert_that<equals>(6, sum);

        //ability to handle const LazyRanges
        constexpr auto const source = src::of_iota(1, 10)
            >> then::filter([](auto i) {return static_cast<bool>(i & 1); });
        sum = 0;
        count = 0;
        source >>= apply::for_each([&](const auto& x) {++count; sum += x; });
        rt.assert_that<equals>(5, count);
        rt.assert_that<equals>(25, sum);
    }

    void test_drain(TestRuntime& rt)
    {
        std::vector<int> dest1( {17} );
        src::null<int>() >>= apply::drain(std::back_inserter(dest1));
        rt.assert_that<eq_sets>(std::array{ 17 }, dest1);

        std::set<int> dest2( {17} );
        src::of_iota(1, 4) >>= apply::drain(
            std::inserter(dest2, dest2.begin())
        );
        rt.assert_that<eq_sets>(std::array{1, 2, 3, 17}, dest2);

        std::vector<int> dest3{ {57, 57} };
        auto dest3_ins = ++dest3.begin(); //between 2 numbers
        src::of_iota(1, 4) >>= apply::drain(
            std::inserter(dest3, dest3_ins)
        );
        rt.assert_that<eq_sets>(std::array{57, 1, 2, 3, 57 }, dest3);

    }

    void test_reduce(TestRuntime& rt)
    {
        size_t count = 17;
        auto r1 = src::null<int>() >>= apply::reduce(count, [](auto& target, const auto&) {++target; });
        rt.assert_that<equals>(17, count);
        rt.assert_that<equals>(17, r1);

        size_t sum = 0;

        auto& r2 = //note result is taken as a reference
            src::of_iota(1, 4) >>= apply::reduce(
            sum, [](auto& target, const auto&i) { target += i; });
        rt.assert_that<equals>(6, sum);
        rt.assert_that<equals>(6, r2);

    }

    void test_first(TestRuntime& rt)
    {
        rt.assert_exception<std::out_of_range>([]() {
            auto skip = apply::first(src::null<int>());//manage [[nodiscard]]
            static_cast<void>(skip);
            });

        auto const range = src::of_value(57);

        auto r1 = apply::first(range);
        rt.assert_that<equals>(r1, 57);

        rt.assert_that<equals>(apply::first(src::of_iota(0, 3)), 0);

        auto r2 = range >>= apply::first;
        rt.assert_that<equals>(r2, 57);

        //test as shared_ptr
        rt.assert_that<equals>(
            make_shared(src::of_value(57)) >>= apply::first, 57);
        rt.assert_that<equals>(apply::first(
            make_shared(src::of_value(57))), 57);

    }

    void test_last(TestRuntime& rt)
    {
        rt.assert_exception<std::out_of_range>([]() {
            auto skip = apply::last(src::null<int>());//manage [[nodiscard]]
            static_cast<void>(skip);
            });
        
        auto const range = src::of_value(57);

        auto r1 = apply::last(range);
        rt.assert_that<equals>(r1, 57);

        rt.assert_that<equals>(apply::last(src::of_iota(0, -3, -1)), -2);

        auto r2 = range >>= apply::last;
        rt.assert_that<equals>(r2, 57);
        
        //test as shared_ptr
        rt.assert_that<equals>(
            make_shared(src::of_value(57)) >>= apply::last, 57);
        rt.assert_that<equals>(apply::last(
            make_shared(src::of_value(57))) , 57);
    }

    template <class T>
    constexpr T arithmetic_progression_sum(size_t N, T from, T to) 
    {
        return N * (from + to) / T{2};
    }

    template <class T, size_t N>
    constexpr auto test_generator(T factor = T{1}/N) noexcept
    {
        return src::generator([factor](const SequenceState& state) -> std::optional<T>{
            return state.step() < N 
                ? std::optional<T>{state.step() + factor}
                : std::nullopt
            ;
        });
    }

    template <class T, class TApp1, class TApp2>
    void compare_sum_algorithms_low_magnitude(TestRuntime& rt, TApp1&& worst, TApp2&& better)
    {
        constexpr size_t range_c = 1021; //some large enough prime number
        const T step_c = T{ 1 } / 10;//0.1
        const auto generator = test_generator<T, range_c>(step_c);
        const T expected = arithmetic_progression_sum(range_c, step_c, (step_c + range_c - 1));

        T val_worst = generator >>= worst;
        T val_better = generator >>= better;
        rt.debug() << "Small magnitude function:\n";
        rt.debug() << "\tdiff between expected and worst (abs error)=" << (expected - val_worst) << "\n";
        rt.debug() << "\tdiff between expected and better (abs error)=" << (expected - val_better) << "\n";
        rt.debug() << "\tdiff between naive and pairwise (rel error, the bigger the better)=" << (val_worst - val_better) << "\n";
        rt.assert_that<negate<almost_eq>>(expected, val_worst,
            "assert should check that absolute error is not appropriate and exceed machine depended epsilon");

        rt.assert_that<less_or_equals>(std::abs(expected - val_better), std::abs(expected - val_worst),
            "assert should check that algorithm better or equals to worst");
    }

    template <class T, size_t N>
    constexpr auto test_magnitude_generator(T a, T r) noexcept
    {
        return src::generator([=](const SequenceState& state) -> std::optional<T> {
            T eval = a * std::pow(r, static_cast<T>(state.step()));
            return state.step() < N
                ? std::optional<T>{eval}
                : std::nullopt
                ;
            });
    }

    constexpr static std::tuple<float, double, long double> type_depended_exponent_c{47.f, 101, 101.0};//some large enough prime number
    template <class T, class TApp1, class TApp2>
    void compare_sum_algorithms_large_magnitude(TestRuntime& rt, TApp1&& worst, TApp2&& better)
    {
        constexpr size_t range_c = std::get<T>(type_depended_exponent_c); //some large enough prime number
        constexpr T a{17}, r{-1.75311};
        const auto generator = test_magnitude_generator<T, range_c>(a, r);

        const T expected = a * (T{1} - std::pow(r, T{ range_c})) / (T{1} - r);

        T val_worst = generator >>= worst;
        T val_better = generator >>= better;

        rt.debug() << "Large magnitude function:\n";
        rt.debug() << "\tdiff between expected and worst (abs error)=" << (expected - val_worst) << "\n";
        rt.debug() << "\tdiff between expected and better (abs error)=" << (expected - val_better) << "\n";
        rt.debug() << "\tdiff between naive and pairwise (rel error, the bigger the better)=" << (val_worst - val_better) << "\n";
        rt.assert_that<negate<almost_eq>>(expected, val_worst,
            "assert should check that absolute error is not appropriate and exceed machine depended epsilon\n");

        rt.assert_that<less_or_equals>(std::abs(expected - val_better), std::abs(expected - val_worst),
            "assert should check that pairwise better than naive\n");
    }

    void test_fsum_pairwise(TestRuntime& rt)
    {
        rt.debug() << "Alg test for float...\n";
        compare_sum_algorithms_low_magnitude<float>(
            rt, apply::sum(), apply::fsum<apply::sum_pairwise_t>());
        compare_sum_algorithms_large_magnitude<float>(
            rt, apply::sum(), apply::fsum<apply::sum_pairwise_t>());

        rt.debug() << "Alg test for double...\n";
        compare_sum_algorithms_low_magnitude<double>(
            rt, apply::sum(), apply::fsum<apply::sum_pairwise_t>());

        double test_val = 0.0;
        constexpr unsigned range_c = 100;
        constexpr double step_c = 0.1;
        double expected = arithmetic_progression_sum(range_c, step_c, (step_c + range_c - 1));
        test_generator<double, range_c>(step_c) >>= apply::fsum<apply::sum_pairwise_t>(test_val); //check signature of collector with value reference
        constexpr static almost_eq_t close_enough_precise(step_c / 1000.);
        rt.assert_that<close_enough_precise>(test_val, expected, "check signature of collector with value reference\n");
    }

    void test_fsum_kahan(TestRuntime& rt)
    {
        rt.debug() << "Alg test for float...\n";
        compare_sum_algorithms_low_magnitude<float>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_kahan_t>());
        compare_sum_algorithms_large_magnitude<float>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_kahan_t>());

        rt.debug() << "Alg test for double...\n";
        compare_sum_algorithms_low_magnitude<double>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_kahan_t>());
    }

    void test_fsum_neumaier(TestRuntime& rt)
    {
        rt.debug() << "Alg test for float...\n";
        compare_sum_algorithms_low_magnitude<float>(
            rt, apply::fsum<apply::sum_kahan_t>(), apply::fsum<apply::sum_neumaier_t>());
        compare_sum_algorithms_large_magnitude<float>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_neumaier_t>());

        rt.debug() << "Alg test for double...\n";
        compare_sum_algorithms_low_magnitude<double>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_neumaier_t>());
        compare_sum_algorithms_large_magnitude<double>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_neumaier_t>());

        rt.debug() << "Alg test for long-double...\n";
        compare_sum_algorithms_low_magnitude<long double>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_neumaier_t>());
        compare_sum_algorithms_large_magnitude<long double>(
            rt, apply::fsum<apply::sum_pairwise_t>(), apply::fsum<apply::sum_neumaier_t>());
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.apply")
        .declare("sum", test_sum)
        .declare("count", test_count)
        .declare("for_each", test_for_each)
        .declare("drain", test_drain)
        .declare("reduce", test_reduce)
        .declare("first", test_first)
        .declare("last", test_last)
        .declare("fsum_pairwise", test_fsum_pairwise)
        .declare("fsum_kahan", test_fsum_kahan)
        .declare("fsum_neumaier", test_fsum_neumaier)
        ;
}//ns: