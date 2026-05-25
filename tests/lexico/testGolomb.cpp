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

    void test_golomb_code()
    {
        static constexpr char alphabet[] = "0123456789abcdef";
        using namespace std::string_literals;
        using namespace OP::lexico;

        {
            TestBitBuilder builder;
            encode_sign(builder, 0u);
            assert(builder.to_string() == "100"s);
        }
        {
            TestBitBuilder builder;
            golomb_code(builder, 0u);
            assert(builder.to_string() == "100"s);
        }
        {
            TestBitBuilder builder;
            golomb_code(builder, 1u);
            assert(builder.to_string() == "101"s);
        }
        {
            TestBitBuilder builder;
            encode_sign(builder, 1u);
            golomb_code(builder, 1u);
            assert(builder.to_string() == "110101"s);
            builder.clear();
            golomb_code(builder, std::numeric_limits<std::uint64_t>::max());
            std::string text_revert;
            builder.to_string(text_revert, alphabet, alphabet + 16);
            assert(text_revert == "ffffffffffffffff7fffffffffffffff8"s);
        }
        { //signed integers
            TestBitBuilder builder;
            golomb_code(builder, 0);
            assert(builder.to_string() == "100"s);
            builder.clear();

            encode_sign(builder, 1);
            golomb_code(builder, 1);
            assert(builder.to_string() == "110101"s);
            builder.clear();

            golomb_code(builder, 2);
            assert(builder.to_string() == "11010"s);
            builder.clear();

            encode_sign(builder, -1);
            golomb_code(builder, -1);
            assert(builder.to_string() == "001010"s);
            builder.clear();

            golomb_code(builder, -2);
            assert(builder.to_string() == "00101"s);
            builder.clear();

            golomb_code(builder, std::numeric_limits<std::int64_t>::max());
            std::string buffer;
            builder.to_string(buffer, alphabet, alphabet + 16);
            assert(buffer == "fffffffffffffffefffffffffffffffe"s);

            buffer.clear();
            builder.clear();

            golomb_code(builder, std::numeric_limits<std::int64_t>::min());
            builder.to_string(buffer, alphabet, alphabet + 16);
            assert(buffer == "0000000000000000bfffffffffffffff8"s); 

            TestBitBuilder builder2;
            golomb_code(builder2, std::numeric_limits<std::int64_t>::min() + 1);
            assert(builder2.to_string() > builder.to_string());
            assert(builder2 > builder);
        }
        //validate lexicographical order
        {
            for (unsigned i = 0; i < 11; ++i)
            {
                TestBitBuilder left, right;
                assert(
                    golomb_code(left, i).to_string() < golomb_code(right, i + 1).to_string());
                assert(left < right);
            }
            //
            // test signed
            //
            {
                TestBitBuilder left, right;
                golomb_code(left, -17);
                golomb_code(right, 17);
                assert(
                    left.to_string() < right.to_string());
                assert(left < right);
            }

            for (int i = -17; i <= 17; ++i)
            {
                TestBitBuilder left, right;
                encode_sign(left, i);
                golomb_code(left, i);

                encode_sign(right, i + 1);
                golomb_code(right, i + 1);

                //std::cout << " Left = " << left.to_string()
                //    << "\nRight = " << right.to_string() << "\n";
                assert(
                    left.to_string() < right.to_string());
                assert(left < right);
            }
            for (int i = -17; i < 3; ++i)
            {
                TestBitBuilder left, right;
                encode_sign(left, i);
                golomb_code(left, i);

                encode_sign(right, i + 17);
                golomb_code(right, i + 17);
                assert(
                    left.to_string() < right.to_string());
                assert(left < right);
            }

        }
    }

    template <auto ...algorithms, class T>
    auto test_reverse(T x, size_t pad_before = 0, size_t pad_after = 0)
    {
        using namespace OP::lexico;

        using source_int_t = decltype(x);
        TestBitBuilder builder;
        //add random padding before number
        for (auto i = 0; i < pad_before; ++i)
            (rand() % 2) ? builder.append_1() : builder.append_0();
        golomb_code<algorithms...>(builder, x);
        //add random padding after number
        for (auto i = 0; i < pad_after; ++i)
            (rand() % 2) ? builder.append_1() : builder.append_0();

        source_int_t v{};
        size_t size = revert_golomb_code<algorithms...>(builder, pad_before, v);
        assert(v == x);
        assert(size == (builder.size() - pad_before - pad_after));

    }

    void test_revert_golomb_code()
    {
        { //unsigned only
            test_reverse(0u);
            test_reverse(1u);
            test_reverse(std::numeric_limits<std::uint64_t>::max());
            test_reverse(0u, 3, 2);
            test_reverse(1u, 12, 13);
            test_reverse(std::numeric_limits<std::uint64_t>::max(), 3, 1);
            test_reverse(0xAAAAAAAAul, 3, 2);
            test_reverse(0x55555555ul, 3, 2);
            test_reverse(std::uint8_t(0x55u));
        }
        {//signed
            test_reverse(0);
            test_reverse(1);
            test_reverse(std::numeric_limits<std::int64_t>::max());
            //negatives...
            test_reverse(-1);
            test_reverse(std::numeric_limits<std::int64_t>::min());
            test_reverse(0x55555555);

            test_reverse(0, 2, 3);
            test_reverse(1, 1, 2);
            test_reverse(std::numeric_limits<std::int64_t>::max(), 2, 3);
            //negatives...
            test_reverse(-1, 3, 2);
            test_reverse(std::numeric_limits<std::int64_t>::min(), 2, 3);
            test_reverse(0x55555555, 2, 3);
        }
    }

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


    void test_decompose_real()
    {
        using namespace OP::lexico;


        auto revert = [](auto cases, auto sign) {
            std::reverse(cases.begin(), cases.end());
            for (auto& x : cases)
                x = std::copysign(x, sign);
            return cases;
            };
        auto test_step = [](auto cases) {
            using real_presentation_t = RealNumberPresentation<typename decltype(cases)::value_type>;
            TestBitBuilder prev;
            std::string prev_image;
            for (auto value : cases)
            {
                TestBitBuilder current;
                real_presentation_t::decompose(current, value);
                std::string image = current.to_string();
                assert(prev_image < image);
                assert(prev < current);
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

    void test_revert_real()
    {
        using namespace OP::lexico;

        auto negate = [](auto cases) {
            using container_t = decltype(cases);
            constexpr typename container_t::value_type neg{ -1 };
            for (auto& x : cases)
                x *= neg;
            return cases;
            };
        auto test_step = [](auto scope) {
            using real_t = decltype(scope)::value_type;
            using real_presentation_t = RealNumberPresentation<real_t>;
            for (auto x : scope)
            {
                TestBitBuilder bb1;
                real_presentation_t::decompose(bb1, x);
                std::cout << bb1.to_string() << "\n";
                real_t v;
                real_presentation_t::take_real(bb1, 0, v);
                std::cout << v << "\n";
                assert(
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

    using long_double_t = long double;

    constexpr auto lexico_order_data_c = std::make_tuple(
        -std::numeric_limits<int>::max(),
        double{-1.2},
        int{ -1 },
        long_double_t{ -0.6 },
        long_double_t{ -0.3 },
        float{ -0.0f },
        int{ 0 }, long_double_t{ 0.01 }, double{ 0.3 }, float{ 0.6f },
        int{ 1 }, std::uint8_t{ 12 }, char{ 57 },
        unsigned{ (1 << 16) - 1 }, float{ 3.1e29f }
    );


    void test_lexico_order()
    {
        namespace lo = OP::lexico;
        using num_t = std::variant<int, double, long double, float, unsigned>;
        auto factory = [](auto v) -> TestBitBuilder {
            TestBitBuilder bb;
            lo::encode(bb, num_t{ v });
            std::cout << bb.to_string() << "\t = " << v << "\n";
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
            std::cout << bb.to_string() << "\n";
            if (previous.has_value())
            {
                assert(*previous < bb);
            }
            previous = bb;
        }
    }

    template <class T>
    void test_l2()
    {
        namespace lo = OP::lexico;

        {
            TestBitBuilder a, b;
            lo::encode(a, -5);
            lo::encode(b, -4.9f);
            std::cout << std::setw(7) << std::setfill('=') << -5 << "=:" << a.to_string() << "\n";
            std::cout << std::setw(7) << std::setfill('=') << -4.9f << "=:" << b.to_string() << "\n";
            std::cout << "Must be a < b: " << std::boolalpha << (a < b) << "\n";
        }
        int check_point[] = { -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5 };
        using num_t = std::variant<T, int>;
        for (int i = 0; i < std::extent_v<decltype(check_point)>; ++i)
        {
            TestBitBuilder bb;
            auto n = check_point[i];
            lo::encode(bb, num_t{ n });
            std::cout << std::setw(2) << std::setfill('=') << n << "=>" << bb.to_string() << "\n";
            for (T x = T{ -0.9 }; x <= T{ 1.0 }; x += T{ 0.1 })
            {
                TestBitBuilder check_builder;
                auto v = x + check_point[i];
                lo::encode(check_builder, num_t{ v });
                std::cout << "===>" << check_builder.to_string() << "\n";
                if (v < n)
                    assert(check_builder < bb);
                else if( v > n )
                    assert(check_builder > bb);
                else {
                    std::cout << n << ":\n" << bb.to_string() << "\n" << check_builder.to_string() << "\n";
                }
            }
        }
    }


    static auto& module_suite = OP::utest::default_test_suite("lexico-golomb")
        .declare("golomb-code", test_golomb_code)
        .declare("revert-golomb-code", test_revert_golomb_code)
    ;
}
