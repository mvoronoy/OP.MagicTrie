#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/common/Bitset.h>
#include <op/common/typedefs.h>


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
    OP::trie::Bitset<1> b1_t(0xAAAAAAAAAAAAAAAAULL);
    for (auto i = 0; i < b1_t.capacity(); ++i)
    {
        tresult.assert_true(
            (i & 1) ? b1_t.get(i) : !b1_t.get(i), OP_CODE_DETAILS() << "Value must be " << (i & ~1));
    }
    generic_test(tresult, b1_t);

    OP::trie::Bitset<2> b2_t;
    generic_test(tresult, b2_t);

    OP::trie::Bitset<3> b3_t;
    generic_test(tresult, b3_t);

    OP::trie::Bitset<4> b4_t;
    generic_test(tresult, b4_t);

    OP::trie::Bitset<8> b8_t;
    generic_test(tresult, b8_t);
}
void test_Finds(OP::utest::TestRuntime& tresult)
{
    OP::trie::Bitset<3> b3_t;
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

    OP::trie::Bitset<1> b1_t2(0xFFFFFFFFFFFFFFFFULL);
    tresult.assert_true(63 == b1_t2.last_set());
    tresult.assert_true(b1_t2.nil_c == b1_t2.first_clear());
    tresult.assert_true(0 == b1_t2.first_set());
    for (auto i = 0; i < b1_t2.bit_length_c; ++i)
    {
        if (i != 0)
            tresult.assert_true((i - 1) == b1_t2.prev_set(i), OP_CODE_DETAILS() << "prev_set failed for i=" << i);
        if ((i + 1) != b1_t2.bit_length_c)
            tresult.assert_true((i + 1) == b1_t2.next_set(i), OP_CODE_DETAILS() << "next_set failed for i=" << i);
    }
}
static auto& module_suite = OP::utest::default_test_suite("Bitset")
.declare("general", test_Basic)
.declare("finds", test_Finds)
;
