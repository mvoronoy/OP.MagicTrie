#include <op/utest/unit_test.h>
#include <op/common/Unsigned.h>

using namespace OP::utest;

void test_UintDiff(OP::utest::TestResult& tresult)
{
    tresult.assert_that<equals>(2ull, OP::utils::uint_diff_int( 5ull, 3ull ), 
        OP_CODE_DETAILS() );

    tresult.assert_that<equals>(-2ll, OP::utils::uint_diff_int( 3ull, 5ull ), 
        OP_CODE_DETAILS() );
}
void test_UintDiffOverflow(OP::utest::TestResult& tresult)
{
    OP::utils::uint_diff_int( 0u, (unsigned)(-1));
}

static auto module_suite = OP::utest::default_test_suite("Utils")
->declare(test_UintDiff, "uint_diff")
->declare_exceptional(test_UintDiffOverflow, "uint_diff_overflow")
;