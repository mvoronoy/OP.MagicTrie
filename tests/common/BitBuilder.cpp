#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/common/BitBuilder.h>
#include <op/common/FixedString.h>

namespace
{
    using namespace OP::utest;

    void test_bb(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;
        using namespace OP::common;

        BitBuilder bb;
        bb.append_1();
        tresult.assert_that<equals>(bb.to_string() ,  "1"s);
        bb.append_0();
        tresult.assert_that<equals>(bb.to_string() ,  "10"s);
        bb.append_uint(0b0101ull, 1);
        tresult.assert_that<equals>(bb.to_string() ,  "101"s);
        bb.append_uint(0b0101ull, 3);
        tresult.assert_that<equals>(bb.to_string() ,  "101101"s);
        bb.append_uint(~0ull);
        tresult.assert_that<equals>(bb.to_string() ,  "1011011111111111111111111111111111111111111111111111111111111111111111"s);
        tresult.assert_that<equals>(bb ,  bb);

        /// test comparison
        BitBuilder b_bigger(0b111u, 3);
        BitBuilder b_smaller_long(0x08u, 8);
        tresult.assert_true(std::is_gt(b_bigger <=> b_smaller_long));
        BitBuilder b_empty;
        tresult.assert_true(std::is_lt(b_empty <=> b_smaller_long));
        tresult.assert_true(std::is_lt(b_empty <=> b_bigger));

        /// test arbitrary to_string
        std::string out;
        constexpr const char alphabet[] = "0123456789abcdef";
        BitBuilder b_x1({ 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde });
        tresult.assert_that<equals>("123456789abcde"s , 
            b_x1.to_string(out, alphabet, alphabet + 16));
        out.clear();
        BitBuilder b_x2 = { 0, 0, 3 };
        tresult.assert_that<equals>("00000003"s ,  b_x2.to_string(out, alphabet, alphabet + 8));
        out.clear();
        BitBuilder b_x3({ 3 });
        tresult.assert_that<equals>("006"s ,  b_x3.to_string(out, alphabet, alphabet + 8));

    }

    void test_bb_count_1(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::common;

        BitBuilder simple;
        tresult.assert_that<equals>(simple.sequence_width_1(0) ,  0);
        simple.append_1();
        tresult.assert_that<equals>(simple.sequence_width_1(0) ,  1);
        simple.append_0();
        tresult.assert_that<equals>(simple.sequence_width_1(0) ,  1);
        {
            BitBuilder bb{ 0xffu, 8 };

        }
        {
            BitBuilder bb(0b0010u, 4);
            tresult.assert_that<equals>(bb.sequence_width_1(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_1(1) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_1(2) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_1(3) ,  0);
        }

        {
            BitBuilder bb(0b000000010u, 9);
            tresult.assert_that<equals>(bb.sequence_width_1(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_1(7) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_1(8) ,  0);
        }
        {//test bits span between bytes
            BitBuilder bb(0b000000011u, 9);
            tresult.assert_that<equals>(bb.sequence_width_1(6) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_1(7) ,  2);
            tresult.assert_that<equals>(bb.sequence_width_1(8) ,  1);
            bb.append_1();
            tresult.assert_that<equals>(bb.sequence_width_1(7) ,  3);
            tresult.assert_that<equals>(bb.sequence_width_1(8) ,  2);
            tresult.assert_that<equals>(bb.sequence_width_1(9) ,  1);
            bb.append_0();
            tresult.assert_that<equals>(bb.sequence_width_1(7) ,  3);
            tresult.assert_that<equals>(bb.sequence_width_1(8) ,  2);
            tresult.assert_that<equals>(bb.sequence_width_1(9) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_1(10) ,  0);
        }
        {//test complete bytes
            BitBuilder bb(0b01u, 2);
            bb.append_uint(0xFFFFul, 16);
            tresult.assert_that<equals>(bb.sequence_width_1(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_1(1) ,  17);
            bb.append_0();
            tresult.assert_that<equals>(bb.sequence_width_1(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_1(1) ,  17);
            tresult.assert_that<equals>(bb.sequence_width_1(17) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_1(18) ,  0);
        }
    }

    void test_bb_count_0(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::common;
        BitBuilder simple;
        tresult.assert_that<equals>(simple.sequence_width_0(0) ,  0);
        simple.append_0();
        tresult.assert_that<equals>(simple.sequence_width_0(0) ,  1);
        simple.append_1();
        tresult.assert_that<equals>(simple.sequence_width_0(0) ,  1);

        {
            BitBuilder bb(0b1101u, 4);
            tresult.assert_that<equals>(bb.sequence_width_0(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_0(1) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_0(2) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_0(3) ,  0);
        }

        {
            BitBuilder bb(0b111111101u, 9);
            tresult.assert_that<equals>(bb.sequence_width_0(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_0(7) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_0(8) ,  0);
        }
        {//test bits span between bytes
            BitBuilder bb(0b111111100u, 9);
            tresult.assert_that<equals>(bb.sequence_width_0(6) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_0(7) ,  2);
            tresult.assert_that<equals>(bb.sequence_width_0(8) ,  1);
            bb.append_0();
            tresult.assert_that<equals>(bb.sequence_width_0(7) ,  3);
            tresult.assert_that<equals>(bb.sequence_width_0(8) ,  2);
            tresult.assert_that<equals>(bb.sequence_width_0(9) ,  1);
            bb.append_1();
            tresult.assert_that<equals>(bb.sequence_width_0(7) ,  3);
            tresult.assert_that<equals>(bb.sequence_width_0(8) ,  2);
            tresult.assert_that<equals>(bb.sequence_width_0(9) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_0(10) ,  0);
        }
        {//test complete bytes
            BitBuilder bb(0b10u, 2);
            bb.append_uint(0x0000ul, 16);
            tresult.assert_that<equals>(bb.sequence_width_0(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_0(1) ,  17);
            bb.append_1();
            tresult.assert_that<equals>(bb.sequence_width_0(0) ,  0);
            tresult.assert_that<equals>(bb.sequence_width_0(1) ,  17);
            tresult.assert_that<equals>(bb.sequence_width_0(17) ,  1);
            tresult.assert_that<equals>(bb.sequence_width_0(18) ,  0);
        }
    }

    /** uses FixedString instead of std::basic_string */
    void test_bb_alt_type(OP::utest::TestRuntime& tresult)
    {
        using namespace OP;
        using namespace OP::common;
        using ufstr_t = FixedString<fix_str_policy_noexcept<std::uint8_t, 64>>;

        {
            BitBuilder< ufstr_t> bb_alt1, bb_alt2;
            bb_alt1.append_0();
            bb_alt2.append_1();
            tresult.assert_that<negate<equals>>(bb_alt1, bb_alt2);
        }
        {
            BitBuilder< ufstr_t> bb1, bb2;
            bb1.append_uint(0xF5555u, 15); //redundant bits ahead
            bb2.append_uint(0x5555u, 15);
            tresult.assert_that<equals>(bb1, bb2);
        }
    }

    static auto& module_suite = OP::utest::default_test_suite("BitBuilder")
        .declare("general", test_bb)
        .declare("count_1", test_bb_count_1)
        .declare("count_0", test_bb_count_0)
        .declare("alt-type", test_bb_alt_type)
    ;
} //ns:
