#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/AppendOnlyLog.h>
#include <op/common/ThreadPool.h>

namespace
{
    using namespace OP::utest;

    static const char* test_file_name = "a0l.test";
    constexpr std::uint64_t expected_value = 0x1000FA1710BEEF;
    constexpr std::uint32_t expected_inc = 0x55AA55AA;
    constexpr double expected_v2 = 57.;

    struct TestPayload
    {/*The size of Payload selected to be bigger than FixedSizeMemoryManager::ZeroHeader */
        TestPayload() noexcept
            : v1(0)
            , inc(57)
            , v2(3.)
        {
        }

        TestPayload(
            std::uint64_t a, std::uint32_t b, double c) noexcept
            : v1(a)
            , inc(b)
            , v2(c)
        {
        }

        std::uint64_t v1;
        std::uint32_t inc;
        double v2;
    };


    void test_Allocate(OP::utest::TestRuntime& tresult)
    {
        if (std::filesystem::remove(test_file_name))
            tresult.debug() << "previous test file has been removed: '" << test_file_name << "'\n";

        OP::utils::ThreadPool tp;
        std::shared_ptr<OP::vtm::AppendOnlyLog> a0l = OP::vtm::AppendOnlyLog::create_new(
            tp, test_file_name
            /*OS minimal allowed size for segment-size*/
        );
        tresult.assert_that<greater>(a0l->segment_size(), 1, "OS must provide minimal allowed page size");
        auto[addr, test_payload] = a0l->construct<TestPayload>();
        test_payload->v1 = expected_value;
        test_payload->inc = expected_inc;
        test_payload->v2 = expected_v2;
        a0l = nullptr; //close

        a0l = OP::vtm::AppendOnlyLog::open(
            tp, test_file_name
        );
        size_t count = 0;
        a0l->for_each([&](TestPayload* payload) {
            ++count;
            tresult.assert_that<equals>(payload->v1, expected_value);
            tresult.assert_that<equals>(payload->inc, expected_inc);
            tresult.assert_that<equals>(payload->v2, expected_v2);
        });
        tresult.assert_that<equals>(count, 1);
    }
    
    void test_NewSegment(OP::utest::TestRuntime& tresult)
    {
        if (std::filesystem::remove(test_file_name))
            tresult.debug() << "previous test file has been removed: '" << test_file_name << "'\n";

        OP::utils::ThreadPool tp;
        std::shared_ptr<OP::vtm::AppendOnlyLog> a0l = OP::vtm::AppendOnlyLog::create_new(
            tp, test_file_name
            /*OS minimal allowed size for segment-size*/
        );
        //now overflow single segment
        for (size_t space = 0, order = 0; space < a0l->segment_size();
            //very rough estimate of memory consumed by TestPayload:
            space += 3 * OP::utils::aligned_sizeof<TestPayload>(OP::vtm::SegmentHeader::align_c) / 2,
            ++order
            )
        {
            static_cast<void>(a0l->construct<TestPayload>(expected_value, order, expected_v2));
        }
        tresult.assert_that<equals>(2, a0l->segments_count());
        a0l = nullptr; //close

        //reopen
        a0l = OP::vtm::AppendOnlyLog::open(
            tp, test_file_name
        );
        //check consistency
        tresult.assert_that<equals>(2, a0l->segments_count());
        size_t order = 0;
        a0l->for_each([&](TestPayload* payload) {
            std::ostringstream line_info; 
            line_info << "equality check failed at step #" << order;
            tresult.assert_that<equals>(payload->v1, expected_value, line_info.str());
            tresult.assert_that<equals>(payload->inc, order++, line_info.str());
            tresult.assert_that<equals>(payload->v2, expected_v2, line_info.str());
            });
    }

    static auto& module_suite = OP::utest::default_test_suite("vtm.AppendOnlyLog")
        .declare("allocate", test_Allocate)
        .declare("new_segment", test_NewSegment)
        ;
} //ns:
