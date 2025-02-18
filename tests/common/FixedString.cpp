#include <cassert>
#include <iomanip>
#include <string>

#include <op/utest/unit_test.h>
#include <op/common/astr.h>
#include <op/common/FixedString.h>

namespace {
    using namespace OP;
    using namespace OP::utest;
    using namespace OP::common;

    using ufstr_t = FixedString<fix_str_policy_noexcept<std::uint8_t, 64>>;


    void test_basic(OP::utest::TestRuntime& tresult)
    {
        ufstr_t k1;
        assert(k1.capacity() == ufstr_t::capacity_c);
        assert(k1.empty());
        assert(k1.size() == 0);
        assert(k1.begin() == k1.end());
        assert(k1.cbegin() == k1.cend());
        try { k1.at(0); assert(false); }
        catch (std::out_of_range) { std::cout << "catched as expected\n"; }
        assert(k1 == k1);

        ufstr_t k2{ 1, 'x' };
        assert(!k2.empty());
        assert(k2.size() == 1);
        assert(k2.begin() < k2.end());
        assert(k2.cbegin() < k2.cend());
        assert(k2.at(0) == 'x');
        assert(k2[0] == 'x');
        assert(memcmp(k2.data(), "x", k2.size()) == 0);
        try { k2.at(1); assert(false); }
        catch (std::out_of_range) { std::cout << "catched as expected\n"; }
        assert(k2 == k2);
        assert(k2 != k1);
        assert(k1 != k2);
        k2 = ufstr_t{ 3, 'y' };
        assert(memcmp(k2.data(), "yyy", k2.size()) == 0);

        using namespace std::string_literals;
        ufstr_t k3{ ""s };
        assert(k3.empty());
        assert(k3.size() == 0);
        auto sample3 = "xyz"s;
        k3 = ufstr_t{ sample3 };
        assert(!k3.empty());
        assert(k3.size() == sample3.size());
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);
        auto i3 = k3.begin();
        auto s3 = sample3.begin();
        for (size_t i = 0; i < sample3.size(); ++i)
        {
            assert(*i3 == *s3);
            assert(k3[i] == sample3[i]);
            ++i3, ++s3;
        }
        assert(i3 == k3.end());
        sample3[1] = 'a';
        k3[1] = sample3[1];
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);
        k3.append(k2);
        sample3.append(k2.begin(), k2.end());
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);

        k3.clear();
        assert(k3.empty());
        assert(k3.begin() == k3.end());

        k3.append(sample3.begin(), sample3.end());
        assert(k3.size() == sample3.size());
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);
        k3.append(""s);
        assert(k3.size() == sample3.size());
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);

        auto suffix = "123"s;
        k3.append(suffix);
        sample3.append(suffix);
        assert(k3.size() == sample3.size());
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);
        
        k3.push_back('-');
        sample3.push_back('-');
        assert(k3.size() == sample3.size());
        assert(memcmp(k3.data(), sample3.data(), sample3.size()) == 0);

        sample3 = "0123456789abcdef"s;
        k3 = sample3;

        assert(k3 == sample3);
        assert(sample3 == k3);

        ufstr_t k4{ {'a', 'b', 'c'} };
        assert(k4 == "abc"_astr);
    }
    
    void test_append(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;

        using extrm1_str_t = FixedString<fix_str_policy_throw_exception<std::uint8_t, 1>>;
        tresult.assert_exception<std::runtime_error>([](){
            extrm1_str_t s1;
            s1.push_back('a');
            });
        tresult.assert_exception<std::runtime_error>([]() {
            extrm1_str_t s1;
            s1 + "a"s;
            });

        tresult.assert_that<equals>(ufstr_t{}.push_back('1'), "1"s);

        using one_char_t = FixedString<fix_str_policy_throw_exception<char, 2>>;
        tresult.assert_that<equals>(
            one_char_t{} + "1"s, one_char_t{"1", 1});
    }
    
    void test_compare(OP::utest::TestRuntime& tresult)
    {
        ufstr_t k1, k2(1, '0');
        tresult.assert_true(k1 < k2);
        tresult.assert_false(k2 < k1);
        tresult.assert_false(k1 < k1);
        tresult.assert_false(k2 < k2);

        tresult.assert_true(k2 < ufstr_t{"01"_astr});
        tresult.assert_true(ufstr_t{"000"_astr } < ufstr_t{ "01"_astr });
    }

    void test_replace(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;

        ufstr_t k1;
        k1.replace(0, 1, std::string{});
        tresult.assert_that<equals>(k1, ufstr_t{});
        tresult.assert_that<equals>(ufstr_t{}.replace(0, std::string::npos, "123"s), ufstr_t{"123"s});
        tresult.assert_exception<std::out_of_range>([](){
                ufstr_t{}.replace(1, std::string::npos, "123"s);
            });
        //target is less
        tresult.assert_that<equals>(
            ufstr_t{"01234"s}.replace(1, 3, "a"s), ufstr_t{ "0a4"s });
        tresult.assert_that<equals>(
            ufstr_t{"01234"s}.replace(0, 1, "a"s), ufstr_t{ "a1234"s });
        tresult.assert_that<equals>(
            ufstr_t{"01234"s}.replace(4, 1, "a"s), ufstr_t{ "0123a"s });
        tresult.assert_that<equals>( //at the range
            ufstr_t{"01234"s}.replace(5, 1, "a"s), ufstr_t{ "01234a"s });

        tresult.assert_that<equals>(
            ufstr_t{ "01234"s }.replace(1, 3, "abcdefgh"s), ufstr_t{ "0abcdefgh4"s });
        tresult.assert_that<equals>(//exact fit
            ufstr_t{ "01234"s }.replace(1, 3, "abc"s), ufstr_t{ "0abc4"s });
        //replace full
        tresult.assert_that<equals>(//exact fit
            ufstr_t{ "01234"s }.replace(0, std::string::npos, "abcde"s), ufstr_t{ "abcde"s });
        tresult.assert_that<equals>(//src is smaller
            ufstr_t{ "01"s }.replace(0, std::string::npos, "abcde"s), ufstr_t{ "abcde"s });
        tresult.assert_that<equals>(//src is smaller and at range
            ufstr_t{ "01"s }.replace(2, std::string::npos, "abcde"s), ufstr_t{ "01abcde"s });
        tresult.assert_that<equals>(//src is larger
            ufstr_t{ "0123456789"s }.replace(0, std::string::npos, "abcde"s), ufstr_t{ "abcde"s });
        tresult.assert_that<equals>(//src is larger
            ufstr_t{ "0123456789"s }.replace(0, 5, "abcde"s), ufstr_t{ "abcde56789"s });
        //replace with empty
        tresult.assert_that<equals>(//src is larger
            ufstr_t{ "0123456789"s }.replace(0, std::string::npos, ""s), ufstr_t{ ""s });
        tresult.assert_that<equals>(//one char remove
            ufstr_t{ "0123456789"s }.replace(0, 1, ""s), ufstr_t{ "123456789"s });
        tresult.assert_that<equals>(//one char remove
            ufstr_t{ "0123456789"s }.replace(9, 1, ""s), ufstr_t{ "012345678"s });
        tresult.assert_that<equals>(//one char remove
            ufstr_t{ "0123456789"s }.replace(1, 1, ""s), ufstr_t{ "023456789"s });
    }

    static auto& module_suite = OP::utest::default_test_suite("FixedString")
        .declare("basic", test_basic)
        .declare("append", test_append)
        .declare("cmp", test_compare)
        .declare("replace", test_replace)
        ;
} //ns:
