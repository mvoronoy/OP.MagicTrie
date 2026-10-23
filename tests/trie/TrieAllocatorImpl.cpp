#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/common/astr.h>
#include <op/common/DependencyManager.h>

#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/trie/servants/TrieOptionsImpl.h>

#include <thread>
#include <future>

namespace
{
    using namespace OP::trie;
    using namespace OP::trie::servants;
    using namespace std::chrono_literals;
    using namespace OP::utest;
    using OP::vtm::FarAddress;

    static const std::string test_file_name = "TrieAllocatorRollback.dat";

    void test_rollback_reuse(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        TrieAllocatorService& service_under_test = toi.allocator_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto ta = transaction_service.begin_transaction();
        auto addr1 = service_under_test.allocate(100, AllocHint::regular);
        tresult.assert_false(addr1.is_nil(), OP_CODE_DETAILS("Should be valid address"));
        ta->rollback();
        // need delay to allow GC complete
        std::this_thread::sleep_for(1000ms);

        auto addr2 = service_under_test.allocate(100, AllocHint::regular);
        tresult.assert_false(addr2.is_nil(), OP_CODE_DETAILS("Should be valid address"));
        tresult.assert_that<equals>(addr1, addr2, OP_CODE_DETAILS("Should be the same address after rollback"));
    }

    void test_commit_reuse(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        TrieAllocatorService& service_under_test = toi.allocator_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto ta = transaction_service.begin_transaction();
        auto addr1 = service_under_test.allocate(100, AllocHint::array);
        tresult.assert_false(addr1.is_nil(), OP_CODE_DETAILS("Should be valid address"));
        ta->commit();

        auto addr2 = service_under_test.allocate(100, AllocHint::array);
        tresult.assert_false(addr2.is_nil(), OP_CODE_DETAILS("Should be valid address"));
        tresult.assert_that<not_equals>(addr1, addr2, OP_CODE_DETAILS("Should be different addresses after commit"));
    }

    void test_deallocate_rollback(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        TrieAllocatorService& service_under_test = toi.allocator_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto addr1 = service_under_test.allocate(100, AllocHint::regular);
        tresult.assert_false(addr1.is_nil(), OP_CODE_DETAILS("Should be valid address"));

        auto ta = transaction_service.begin_transaction();
        service_under_test.deallocate(addr1);
        ta->rollback();

        // After rolling back the deallocation, the block should not be available for allocation
        auto addr2 = service_under_test.allocate(100, AllocHint::regular);
        tresult.assert_that<not_equals>(addr1, addr2, OP_CODE_DETAILS("Deallocated block should not be reused after rollback"));
    }

    void test_transaction_isolation(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        TrieAllocatorService& service_under_test = toi.allocator_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        std::promise<FarAddress> promise1;
        auto future1 = promise1.get_future();
        std::thread thread1([&]() {
            auto ta1 = transaction_service.begin_transaction();
            auto addr1 = service_under_test.allocate(100, AllocHint::irregular);
            promise1.set_value(addr1);
            std::this_thread::sleep_for(100ms);
            ta1->commit();
            });

        std::promise<FarAddress> promise2;
        auto future2 = promise2.get_future();
        std::thread thread2([&]() {
            auto ta2 = transaction_service.begin_transaction();
            auto addr2 = service_under_test.allocate(100, AllocHint::irregular);
            promise2.set_value(addr2);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ta2->rollback();
            });

        auto addr1 = future1.get();
        auto addr2 = future2.get();

        tresult.assert_false(addr1.is_nil(), OP_CODE_DETAILS("Thread 1 should have a valid address"));
        tresult.assert_false(addr2.is_nil(), OP_CODE_DETAILS("Thread 2 should have a valid address"));
        tresult.assert_that<not_equals>(addr1, addr2,
            OP_CODE_DETAILS("Addresses allocated in different transactions should be different"));

        thread1.join();
        thread2.join();

        auto addr3 = service_under_test.allocate(100, AllocHint::irregular);
        tresult.assert_false(addr3.is_nil(), OP_CODE_DETAILS("Should be able to allocate after transactions"));
        tresult.assert_that<equals>(addr2, addr3, OP_CODE_DETAILS("Should reuse the rolled-back address"));
    }

    static auto& module_suite = OP::utest::default_test_suite("Trie.allocator")
        .declare("rollback_reuse", test_rollback_reuse)
        .declare("commit_reuse", test_commit_reuse)
        .declare("deallocate_rollback", test_deallocate_rollback)
        .declare("transaction_isolation", test_transaction_isolation)
        ;
}
