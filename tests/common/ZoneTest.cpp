#include <op/utest/unit_test.h>
#include <op/common/Range.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/SegmentHelper.h>
#include <assert.h>
#include <random>
#include <map>
#include <forward_list>
#include <op/common/Bitset.h>
#include <op/flur/flur.h>
#include <op/flur/flur.h>


using namespace OP;
using namespace OP::vtm;
using namespace OP::trie;
using namespace OP::utest;


void test_Zones(TestRuntime& tresult)
{
    typedef Range<size_t> range_t;
    typedef std::map<range_t, int> ranges_t;

    ranges_t cont;
    //create many ranges
    for (auto i = 0; i < 10; ++i)
    {
        cont.emplace(range_t(i * 100, 10), i);
    }
    //check findability
    for (auto i = 0; i < 10; ++i)
    {
        auto f = cont.find(range_t(i * 100+5, 1));
        tresult.assert_true(cont.end() != f);
        tresult.assert_true(f->second == i);
        tresult.assert_true(f->first.count() == 10);
        f = cont.find(range_t(i * 100+11, 1));
        tresult.assert_true(cont.end() == f);
    }
    //test overlapping
    range_t r1 = { 10, 5 }, r2 = { 9, 5 }, r3 = { 14, 1 }, r4 = { 0, 11 };
    tresult.assert_true(r1.is_overlapped(r1)); //self test
    tresult.assert_true(r1.is_included(r1)); //self test

    tresult.assert_true(r1.is_overlapped(r2) && r2.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(r2) && !r2.is_included(r1));

    tresult.assert_true(r1.is_overlapped(r3) && r3.is_overlapped(r1));
    tresult.assert_true(r1.is_included(r3) && !r3.is_included(r1));

    tresult.assert_true(r1.is_overlapped(r4) && r4.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(r4) && !r4.is_included(r1));

    range_t nr1 = { 9, 6 }/*no overlaps, but inclusion*/, nr2 = { 0, 10 }/*just adjacence*/,
        nr3 = { 1, 1 }, nr4 = { 15, 2 }/*adjacent*/, nr5 = { 20, 2 };
    tresult.assert_true(r1.is_overlapped(nr1) && nr1.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr1) && nr1.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr2) && !nr2.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr2) && !nr2.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr3) && !nr3.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr3) && !nr3.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr4) && !nr4.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr4) && !nr4.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr5) && !nr5.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr5) && !nr5.is_included(r1));

}

static auto& module_suite = OP::utest::default_test_suite("Zones")
.declare("zone", test_Zones)
;