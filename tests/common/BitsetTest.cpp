#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/common/Bitset.h>
namespace {
    using namespace OP::utest;

template <class B>
void generic_test(OP::utest::TestRuntime& tresult, B& container)
{
    for (auto i = 0; i < container.capacity(); ++i)
        container.clear(i);

    for (auto i = 0; i < container.capacity(); ++i)
    {
        tresult.assert_true(!container.get(i), "Value must be 0");
        container.set(i);
    }
    for (auto i = 0; i < container.capacity(); ++i)
    {
        tresult.assert_true(container.get(i), "Value must be 1");
        if (i & 1)//clear only odd 
            container.clear(i);
    }
    for (auto i = 0; i < container.capacity(); ++i)
    {
        tresult.assert_true(
            ((i & 1) ? !container.get(i) : container.get(i)), OP_CODE_DETAILS() << "Value must be " << (i & ~1));
        container.toggle(i);
    }
    for (auto i = 0; i < container.capacity(); ++i)
    {
        tresult.assert_true(
            ((i & 1) ? container.get(i) : !container.get(i)), OP_CODE_DETAILS() << "Value must be " << (i & ~1));
    }
}
void test_Basic(OP::utest::TestRuntime& tresult)
{
    OP::common::Bitset<1> b1_t(0xAAAAAAAAAAAAAAAAULL);
    for (auto i = 0; i < b1_t.capacity(); ++i)
    {
        tresult.assert_true(
            (i & 1) ? b1_t.get(i) : !b1_t.get(i), 
            OP_CODE_DETAILS( << "Value must be " << (i & ~1)));
    }
    generic_test(tresult, b1_t);

    OP::common::Bitset<2> b2_t;
    generic_test(tresult, b2_t);

    OP::common::Bitset<3> b3_t;
    generic_test(tresult, b3_t);

    OP::common::Bitset<4> b4_t;
    generic_test(tresult, b4_t);

    OP::common::Bitset<8> b8_t;
    generic_test(tresult, b8_t);
}

void test_Finds(OP::utest::TestRuntime& tresult)
{
    OP::common::Bitset<3> b3_t;
    tresult.assert_true(b3_t.nil_c == b3_t.first_set());
    tresult.assert_true(0 == b3_t.first_clear());
    tresult.assert_true(b3_t.nil_c == b3_t.next_set(0));
    tresult.assert_true(b3_t.nil_c == b3_t.next_set_or_this(0));
    tresult.assert_true(b3_t.nil_c == b3_t.prev_set(b3_t.bit_length_c - 1u));
    tresult.assert_true(b3_t.nil_c == b3_t.prev_set(0));
    tresult.assert_true(b3_t.nil_c == b3_t.last_set());

    b3_t.set(0);
    tresult.assert_true(0 == b3_t.first_set());
    tresult.assert_true(0 == b3_t.last_set());
    tresult.assert_true(b3_t.nil_c == b3_t.next_set(0));
    tresult.assert_true(0 == b3_t.next_set_or_this(0));
    b3_t.clear(0);

    b3_t.set(1);
    tresult.assert_true(1 == b3_t.first_set());
    tresult.assert_true(1 == b3_t.last_set());
    tresult.assert_true(0 == b3_t.first_clear());
    tresult.assert_true(1 == b3_t.next_set(0));
    tresult.assert_true(1 == b3_t.next_set_or_this(0));
    tresult.assert_true(1 == b3_t.prev_set(b3_t.bit_length_c - 1));
    b3_t.set(2);//keep 1 set
    tresult.assert_true(1 == b3_t.first_set());
    tresult.assert_true(0 == b3_t.first_clear());
    tresult.assert_true(2 == b3_t.last_set());
    tresult.assert_true(2 == b3_t.next_set(1));
    tresult.assert_true(1 == b3_t.next_set_or_this(1));
    tresult.assert_true(2 == b3_t.next_set_or_this(2));
    tresult.assert_true(2 == b3_t.prev_set(b3_t.bit_length_c - 1));
    tresult.assert_true(b3_t.nil_c == b3_t.next_set(2));
    tresult.assert_true(b3_t.nil_c == b3_t.prev_set(1));

    b3_t.set(b3_t.bit_length_c - 1);
    tresult.assert_true((b3_t.bit_length_c - 1) == b3_t.next_set(2));
    tresult.assert_true((b3_t.bit_length_c - 1) == b3_t.last_set());

    b3_t.set(0);
    tresult.assert_true(0 == b3_t.prev_set(1));

    OP::common::Bitset<1> b1_t2(0xFFFFFFFFFFFFFFFFULL);
    tresult.assert_true(63 == b1_t2.last_set());
    tresult.assert_true(b1_t2.nil_c == b1_t2.first_clear());
    tresult.assert_true(0 == b1_t2.first_set());
    for (auto i = 0; i < b1_t2.bit_length_c; ++i)
    {
        if (i != 0)
            tresult.assert_true((i - 1) == b1_t2.prev_set(i), 
                OP_CODE_DETAILS(<< "prev_set failed for i=" << i));
        if ((i + 1) != b1_t2.bit_length_c)
            tresult.assert_true((i + 1) == b1_t2.next_set(i), 
                OP_CODE_DETAILS( << "next_set failed for i=" << i));
    }
}

    void test_Count(OP::utest::TestRuntime& tresult)
    {
        OP::common::Bitset<1, std::uint64_t> b1_64_t;
        tresult.assert_that<equals>(0, b1_64_t.count_bits());
        b1_64_t.set(64-1);
        tresult.assert_that<equals>(1, b1_64_t.count_bits());
        b1_64_t.set(0);
        tresult.assert_that<equals>(2, b1_64_t.count_bits());


        OP::common::Bitset<3, std::uint64_t> b3_64;
        tresult.assert_that<equals>(0, b3_64.count_bits());
        b3_64.set(3*64 - 1);
        tresult.assert_that<equals>(1, b3_64.count_bits());
        b3_64.set(0);
        tresult.assert_that<equals>(2, b3_64.count_bits());
        
        for(int i = 0; i < 3; ++i)
        {
            OP::common::Bitset<3, std::uint64_t> b3;
            for(int j = 0; j < 3; ++j)
                b3.set(i*8 + j);
            tresult.assert_that<equals>(3, b3.count_bits());
            b3.invert_all();
            tresult.assert_that<equals>(3*64 - 3, b3.count_bits());
        }

        OP::common::Bitset<3, std::uint64_t> b3x(~0ull);
        tresult.assert_that<equals>(3 * 64, b3x.count_bits());
    }

static auto& module_suite = OP::utest::default_test_suite("Bitset")
.declare("general", test_Basic)
.declare("finds", test_Finds)
    .declare("count", test_Count)
;
} //ns:
