#include <unordered_set>
#include <unordered_map>
#include <execution>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/flur/flur.h>

#include <op/vtm/InMemMemoryChangeHistory.h>

#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/Range.h>
#include "MemoryChangeHistoryTestHarness.h"

namespace
{
    using namespace OP::utest;


    void test_Emplace(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool tp;
        std::unique_ptr<OP::vtm::MemoryChangeHistory> in_mem_history(new OP::vtm::InMemoryChangeHistory(tp));
        test::vtm::test_memory_change_history(tresult, *in_mem_history);
    }

    static auto& module_suite = OP::utest::default_test_suite("vtm.InMemMemoryChangeHistory")
        .declare("emplace", test_Emplace)
        ;
} //ns:
