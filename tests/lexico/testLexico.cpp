#include <array>
#include <optional>
#include <iomanip>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/lexico/lexcodec.h>

namespace
{

    using namespace OP::utest;
    using TestBitBuilder = OP::common::BitBuilder<>;

    template <class T>
    static constexpr auto real_number_positive_edge_cases =
        std::array{
            T{0. },
            std::numeric_limits<T>::min(),
            T{0.3}, //does not fits to std::frexp: [0.5..1.0)
            T{0.6}, //fits to the range of std::frexp: [0.5..1.0)
            T{1.0},
            T{1.2},
            T{2.9},
            T{3.0},
            std::numeric_limits<T>::max(),
            std::numeric_limits<T>::infinity(),
            std::numeric_limits<T>::quiet_NaN(),
    };


    void test_decompose_real(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::lexico;

        //change the sequence order and set specified sign to each number
        auto revert = [](auto cases, auto sign) {
            std::reverse(cases.begin(), cases.end());
            for (auto& x : cases)
                x = std::copysign(x, sign);
            return cases;
            };
        auto test_step = [&](auto cases) {
            using real_presentation_t = RealNumberPresentation<typename decltype(cases)::value_type>;
            TestBitBuilder prev;
            std::string prev_image;
            for (auto value : cases)
            {
                TestBitBuilder current;
                real_presentation_t::decompose(current, value);
                std::string image = current.to_string();
                tresult.assert_that<less>(prev_image, image);
                tresult.assert_that<less>(prev, current);
                prev = std::move(current);
                prev_image = std::move(image);
            }
            };
        test_step(real_number_positive_edge_cases<float>);
        test_step(real_number_positive_edge_cases<double>);
        test_step(real_number_positive_edge_cases<long double>);

        test_step(revert(real_number_positive_edge_cases<float>, -1.f));
        test_step(revert(real_number_positive_edge_cases<double>, -1.0));
        test_step(revert(real_number_positive_edge_cases<long double>, -1.0L));
    }

    void test_revert_real(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::lexico;

        auto negate = [](auto cases) {
            using container_t = decltype(cases);
            constexpr typename container_t::value_type neg{ -1 };
            for (auto& x : cases)
                x *= neg;
            return cases;
            };
        auto test_step = [&](auto scope) {
            using real_t = decltype(scope)::value_type;
            using real_presentation_t = RealNumberPresentation<real_t>;
            for (auto x : scope)
            {
                TestBitBuilder bb1;
                real_presentation_t::decompose(bb1, x);
                tresult.debug() << bb1.to_string() << "\n";
                real_t v;
                real_presentation_t::take_real(bb1, 0, v);
                tresult.debug() << v << "\n";
                tresult.assert_true(
                    std::strong_order(x, v) == std::strong_ordering::equal //allows compare +/- inf as well
                );
            }
            };
        test_step(real_number_positive_edge_cases<float>);
        test_step(real_number_positive_edge_cases<double>);
        test_step(real_number_positive_edge_cases<long double>);

        test_step(negate(real_number_positive_edge_cases<float>));
        test_step(negate(real_number_positive_edge_cases<double>));
        test_step(negate(real_number_positive_edge_cases<long double>));
    }


    constexpr auto lexico_order_data_c = std::make_tuple(
        -std::numeric_limits<int>::max(),
        double{ -1.2 },
        int{ -1 },
        long double{ -0.6 },
        long double{ -0.3 },
        float{ -0.0f },
        int{ 0 }, long double{ 0.01 }, double{ 0.3 }, float{ 0.6f },
        int{ 1 }, std::uint8_t{ 12 }, char{ 57 },
        unsigned{ (1 << 16) - 1 }, float{ 3.1e29f }
    );


    void test_lexico_order(OP::utest::TestRuntime& tresult)
    {
        namespace lo = OP::lexico;
        using num_t = std::variant<int, double, long double, float, unsigned>;
        auto factory = [](auto v) -> TestBitBuilder {
            TestBitBuilder bb;
            lo::encode(bb, num_t{ v });
            tresult.debug() << bb.to_string() << "\t = " << v << "\n";
            return bb;
            };
        //make both signed and unsigned
        auto target =
            std::apply(
                [&](auto ...a) {
                    return std::array{ factory(a) ... };
                },
                lexico_order_data_c
            );
        std::optional<TestBitBuilder> previous;
        for (const auto& bb : target)
        {
            tresult.debug() << bb.to_string() << "\n";
            if (previous.has_value())
            {
                tresult.assert_that<less>(*previous, bb);
            }
            previous = bb;
        }
    }

    template <class T>
    void test_l2(OP::utest::TestRuntime& tresult)
    {
        namespace lo = OP::lexico;

        {
            TestBitBuilder a, b;
            lo::encode(a, -5);
            lo::encode(b, -4.9f);
            tresult.debug() << std::setw(7) << std::setfill('=') << -5 << "=:" << a.to_string() << "\n";
            tresult.debug() << std::setw(7) << std::setfill('=') << -4.9f << "=:" << b.to_string() << "\n";
            tresult.assert_that<less>(a, b, OP_CODE_DETAILS() << "Must be a < b: \n");
        }
        int check_point[] = { -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5 };
        using num_t = std::variant<T, int>;
        for (int i = 0; i < std::extent_v<decltype(check_point)>; ++i)
        {
            TestBitBuilder bb;
            auto n = check_point[i];
            lo::encode(bb, num_t{ n });
            tresult.debug() << std::setw(2) << std::setfill('=') << n << "=>" << bb.to_string() << "\n";
            for (T x = T{ -0.9 }; x <= T{ 1.0 }; x += T{ 0.1 })
            {
                TestBitBuilder check_builder;
                auto v = x + check_point[i];
                lo::encode(check_builder, num_t{ v });
                tresult.debug() << "===>" << check_builder.to_string() << "\n";
                if (v < n)
                    tresult.assert_that<less>(check_builder, bb);
                else if (v > n)
                    tresult.assert_that<greater>(check_builder, bb);
                else {
                    tresult.debug() << n << ":\n" << bb.to_string() << "\n" << check_builder.to_string() << "\n";
                }
            }
        }
    }

    void test_lexico2(OP::utest::TestRuntime& tresult)
    {
        test_l2<float>(tresult);
        test_l2<double>(tresult);
        test_l2<long double>(tresult);
    }

    static auto& module_suite = OP::utest::default_test_suite("lexico-all")
        .declare("decompose-real", test_decompose_real)
        .declare("revert-real", test_revert_real)
        .declare("lexico-order", test_lexico_order)
        .declare("lexico2", test_lexico2)
        ;

}//ns:
