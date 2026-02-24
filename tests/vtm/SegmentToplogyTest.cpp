#include <op/utest/unit_test.h>
#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/vtm/SegmentTopology.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/slots/HeapManager.h>

namespace
{

    using namespace OP::vtm;
    using namespace OP::utils;

    struct TestMemAlloc1
    {
        int a;
        char b[10];
    };
    struct TestMemAlloc2
    {
        int a;
        double x;
        double y[121];
    };
    void test_SegmentTopology(OP::utest::TestRuntime& result)
    {
        const char seg_file_name[] = "segmentation.test";
        typedef std::uint8_t assorted_t[13];

        auto options = SegmentOptions()
            .heuristic_size(
                size_heuristic::of_array<TestMemAlloc1, 100>,
                size_heuristic::of_array<TestMemAlloc1, 900>,
                size_heuristic::of_array<TestMemAlloc2, 1000>,
                size_heuristic::of_assorted<assorted_t, 3>,
                size_heuristic::add_percentage(5)/*+5% of total size*/
            );
        std::shared_ptr<SegmentManager> mngr1 = BaseSegmentManager::create_new(seg_file_name, options);
        auto tst_size = mngr1->segment_size();
        OP_CONSTEXPR(const) size_t control_size = ((align_on(sizeof(TestMemAlloc1) * 100, SegmentDef::align_c)
            + align_on(sizeof(TestMemAlloc1) * 900, SegmentDef::align_c)
            + align_on(sizeof(TestMemAlloc2) * 100, SegmentDef::align_c)
            + align_on(sizeof(assorted_t), SegmentDef::align_c) * 3) * 105) / 100;
        result.assert_true(control_size <= tst_size);
        SegmentTopology<HeapManagerSlot> mngrTopology = { mngr1 };
        mngrTopology._check_integrity(
            result.run_options().log_level() > OP::utest::ResultLevel::info
        );
        result.assert_true(mngrTopology.slot<HeapManagerSlot>().available(0) < tst_size);
    }

    //using std::placeholders;
    static auto& module_suite = OP::utest::default_test_suite("vtm.SegmentTopology")
        .declare("general", test_SegmentTopology)
        ;
}//ns