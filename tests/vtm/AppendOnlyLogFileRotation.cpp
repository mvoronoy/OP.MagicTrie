#include <unordered_set>
#include <unordered_map>
#include <execution>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/flur/flur.h>

#include <op/vtm/AppendOnlySkipList.h>
#include <op/vtm/AppendOnlyLogFileRotation.h>

#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/Range.h>

namespace
{
    using namespace OP::utest;

    using MyRange = OP::Range<unsigned>;


    struct TestPayload
    {

        TestPayload(MyRange key,
            std::uint64_t a, std::uint32_t b, double c) noexcept
            : _key(key)
            , v1(a)
            , inc(b)
            , v2(c)
        {
        }

        MyRange _key;
        std::uint64_t v1;
        std::uint32_t inc;
        double v2;

    };

    void test_Emplace(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool tp;
        using namespace std::string_literals;

        using a0list_t = OP::vtm::AppendOnlySkipList<32, TestPayload>;
        using file_rota_t = OP::vtm::AppendLogFileRotation<a0list_t>;
        auto frt = file_rota_t::create_new(tp, 
            std::unique_ptr<OP::vtm::CreationPolicy>(
                new OP::vtm::FileCreationPolicy(
                    tp, OP::vtm::FileRotationOptions{},
                    std::filesystem::path("."),
                    "a0"s, ".tlog"s))
        );

        //constexpr size_t test_tran_c = 11 * 11;
        //constexpr size_t test_range_from_c = 32;
        //constexpr size_t test_range_to_c = 1 << 24;
        //constexpr size_t test_range_length_c = 1024*10;
        //constexpr double test_expected_c = 57.1;
        //auto rand_range_gen = [&]() {
        //    auto pos = tresult.randomizer().next_in_range<std::uint32_t>(test_range_from_c, test_range_to_c);
        //    auto dim = tresult.randomizer().next_in_range<std::uint32_t>(8, test_range_length_c);
        //    return MyRange(pos, dim);
        //};

        //for (auto i = 0; i < test_tran_c; ++i)
        //{
        //    for (auto j = 0; j < 5; ++j)
        //    {
        //        auto tran_id = i + j;
        //        frt->on_new_transaction(tran_id);
        //        for (auto k = 0; k < 1000; ++k)
        //        {
        //            TestPayload tst_value{ rand_range_gen(), 73, 57, test_expected_c };
        //            frt->append(tran_id, tst_value);
        //        }
        //    }
        //    auto key = rand_range_gen();
        //    auto payload = TestPayload(key, 0, i, test_expected_c);
        //}

    }

    static auto& module_suite = OP::utest::default_test_suite("vtm.AppendOnlySkipList")
        .declare("emplace", test_Emplace)
        ;
} //ns:
