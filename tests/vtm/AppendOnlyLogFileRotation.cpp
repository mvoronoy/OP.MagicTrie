#include <unordered_set>
#include <unordered_map>
#include <execution>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/flur/flur.h>

#include <op/vtm/AppendOnlySkipList.h>
#include <op/vtm/managers/AppendOnlyLogFileRotation.h>

#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/Range.h>
#include "MemoryChangeHistoryTestHarness.h"

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
        using namespace OP::vtm;

        using RWR = typename MemoryChangeHistory::RWR;
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;
        using file_rota_t = AppendLogFileRotationChangeHistory;
        
        constexpr std::uint8_t transactions_per_file_c = 5;
        constexpr size_t block_test_width_c = 32;


        auto frt = file_rota_t::create_new(tp,
            std::unique_ptr<OP::vtm::CreationPolicy>(
                new OP::vtm::FileCreationPolicy(tp, 
                    OP::vtm::FileRotationOptions{}
                        .transactions_per_file(transactions_per_file_c),
                    std::filesystem::path("."),
                    "a0"s, ".tlog"s))
        );
        test::vtm::test_memory_change_history(tresult, *frt);
    }

    static auto& module_suite = OP::utest::default_test_suite("vtm.RotaryLogChangeHistory")
        .declare_disabled("emplace", test_Emplace)
        ;
} //ns:
