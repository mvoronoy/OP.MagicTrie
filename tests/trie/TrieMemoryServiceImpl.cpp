#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/common/DependencyManager.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/trie/servants/TrieOptionsImpl.h>

#include <numeric>
#include <vector>
#include <future>
#include <algorithm>

namespace
{
    using namespace OP::trie;
    using namespace OP::trie::servants;
    using namespace OP::utest;
    using OP::vtm::FarAddress;
    using OP::vtm::segment_pos_t;
    using transaction_id_t = TrieTransactionService::transaction_id_t;

    static const std::string test_file_name = "TrieMemoryService.dat";
    constexpr segment_pos_t test_block_size_c = 64;

    /** Standard layout type to exercise typed accessors */
    struct TestPod
    {
        std::uint32_t _a;
        double _b;

        constexpr TestPod(std::uint32_t a, double b) noexcept
            : _a(a), _b(b)
        {}
    };
    static_assert(std::is_standard_layout_v<TestPod>);

    std::vector<std::uint8_t> make_pattern(std::uint8_t seed, segment_pos_t size)
    {
        std::vector<std::uint8_t> result(size);
        std::iota(result.begin(), result.end(), seed);
        return result;
    }

    template <class Chunk>
    bool chunk_equals(const Chunk& chunk, const std::vector<std::uint8_t>& expected)
    {
        return chunk.count() == expected.size()
            && std::equal(expected.begin(), expected.end(), chunk.pos());
    }

    /** Write `pattern` to `addr` in a dedicated transaction and commit it */
    void commit_pattern(TrieOptions& toi, FarAddress addr, const std::vector<std::uint8_t>& pattern)
    {
        OP::vtm::TransactionGuard g(toi.transaction_service().begin_transaction(), true);
        auto wr = toi.memory_service().writable_block(
            addr, static_cast<segment_pos_t>(pattern.size()), false);
        wr.byte_copy(pattern.data(), static_cast<segment_pos_t>(pattern.size()));
    }

    /** Read block from another thread that has no transaction => observes committed state only */
    std::vector<std::uint8_t> read_committed(TrieOptions& toi, FarAddress addr, segment_pos_t size)
    {
        return std::async(std::launch::async, [&toi, addr, size]() {
            auto ro = toi.memory_service().readonly_block(addr, size);
            return std::vector<std::uint8_t>(ro.pos(), ro.pos() + ro.count());
            }).get();
    }

    void test_creation(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);

        auto* memory_impl = dynamic_cast<TrieMemoryServiceImpl*>(&toi.memory_service());
        auto* transaction_impl = dynamic_cast<TrieMemoryServiceImpl*>(&toi.transaction_service());
        tresult.assert_that<is_not_null>(memory_impl,
            OP_CODE_DETAILS("memory_service() must be TrieMemoryServiceImpl"));
        tresult.assert_that<equals>(memory_impl, transaction_impl,
            OP_CODE_DETAILS("TrieMemoryServiceImpl must play both roles: TrieMemoryService and TrieTransactionService"));

        tresult.assert_false(toi.transaction_service().current_transaction_id().has_value(),
            OP_CODE_DETAILS("No transaction expected right after creation"));
    }

    void test_readonly_no_transaction(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);
        TrieMemoryService& memory_service = toi.memory_service();

        auto addr = toi.allocator_service().allocate(test_block_size_c, AllocHint::regular);
        const auto pattern = make_pattern(0x10, test_block_size_c);
        commit_pattern(toi, addr, pattern);

        // read without transaction is permitted
        auto ro = memory_service.readonly_block(addr, test_block_size_c);
        tresult.assert_that<equals>(ro.address(), addr, OP_CODE_DETAILS("RO block must keep requested address"));
        tresult.assert_that<equals>(ro.count(), test_block_size_c, OP_CODE_DETAILS("RO block must keep requested size"));
        tresult.assert_true(chunk_equals(ro, pattern), OP_CODE_DETAILS("RO block must expose committed data"));

        // partial read with offset
        constexpr segment_pos_t offset_c = 8, part_c = 16;
        auto ro_part = memory_service.readonly_block(addr + offset_c, part_c);
        tresult.assert_true(
            std::equal(pattern.begin() + offset_c, pattern.begin() + offset_c + part_c, ro_part.pos()),
            OP_CODE_DETAILS("Partial RO block must expose correct slice"));
    }

    void test_write_commit(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);
        TrieMemoryService& memory_service = toi.memory_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto addr = toi.allocator_service().allocate(test_block_size_c, AllocHint::regular);
        const auto pattern_a = make_pattern(0x10, test_block_size_c);
        const auto pattern_b = make_pattern(0x80, test_block_size_c);
        commit_pattern(toi, addr, pattern_a);

        auto tran = transaction_service.begin_transaction();
        tresult.assert_true(transaction_service.current_transaction_id().has_value(),
            OP_CODE_DETAILS("Transaction id must be available inside transaction"));
        tresult.assert_that<equals>(*transaction_service.current_transaction_id(), tran->transaction_id(),
            OP_CODE_DETAILS("current_transaction_id must match started transaction"));

        if (1 == 1)
        {
            auto wr = memory_service.writable_block(addr, test_block_size_c /*need_previous_content = true*/);
            tresult.assert_that<equals>(wr.address(), addr, OP_CODE_DETAILS("WR block must keep requested address"));
            tresult.assert_that<equals>(wr.count(), test_block_size_c, OP_CODE_DETAILS("WR block must keep requested size"));
            tresult.assert_true(chunk_equals(wr, pattern_a),
                OP_CODE_DETAILS("need_previous_content=true must expose previous content"));
            wr.byte_copy(pattern_b.data(), test_block_size_c);
        }
        // same transaction sees own changes
        tresult.assert_true(chunk_equals(memory_service.readonly_block(addr, test_block_size_c), pattern_b),
            OP_CODE_DETAILS("Transaction must see own uncommitted changes"));
        // other threads do not see uncommitted changes
        tresult.assert_that<equals>(read_committed(toi, addr, test_block_size_c), pattern_a,
            OP_CODE_DETAILS("Uncommitted changes must be invisible for other threads"));

        tran->commit();
        tresult.assert_false(transaction_service.current_transaction_id().has_value(),
            OP_CODE_DETAILS("No transaction expected after commit"));

        tresult.assert_true(chunk_equals(memory_service.readonly_block(addr, test_block_size_c), pattern_b),
            OP_CODE_DETAILS("Committed changes must be visible in this thread"));
        tresult.assert_that<equals>(read_committed(toi, addr, test_block_size_c), pattern_b,
            OP_CODE_DETAILS("Committed changes must be visible for other threads"));
    }

    void test_write_rollback(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);
        TrieMemoryService& memory_service = toi.memory_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto addr = toi.allocator_service().allocate(test_block_size_c, AllocHint::regular);
        const auto pattern_a = make_pattern(0x10, test_block_size_c);
        const auto pattern_b = make_pattern(0x80, test_block_size_c);
        commit_pattern(toi, addr, pattern_a);

        auto tran = transaction_service.begin_transaction();
        if (1 == 1)
        {
            auto wr = memory_service.writable_block(addr, test_block_size_c, false);
            wr.byte_copy(pattern_b.data(), test_block_size_c);
        }
        tresult.assert_true(chunk_equals(memory_service.readonly_block(addr, test_block_size_c), pattern_b),
            OP_CODE_DETAILS("Transaction must see own uncommitted changes"));
        tran->rollback();
        tresult.assert_false(transaction_service.current_transaction_id().has_value(),
            OP_CODE_DETAILS("No transaction expected after rollback"));

        tresult.assert_true(chunk_equals(memory_service.readonly_block(addr, test_block_size_c), pattern_a),
            OP_CODE_DETAILS("Rolled back changes must be discarded (no transaction read)"));
        tresult.assert_that<equals>(read_committed(toi, addr, test_block_size_c), pattern_a,
            OP_CODE_DETAILS("Rolled back changes must be discarded (other thread read)"));

        if (1 == 1)
        {   // new transaction must see the old state too
            OP::vtm::TransactionGuard g(transaction_service.begin_transaction());
            tresult.assert_true(chunk_equals(memory_service.readonly_block(addr, test_block_size_c), pattern_a),
                OP_CODE_DETAILS("Rolled back changes must be discarded (new transaction read)"));
        }
    }

    void test_write_without_transaction(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);
        TrieMemoryService& memory_service = toi.memory_service();

        auto addr = toi.allocator_service().allocate(test_block_size_c, AllocHint::regular);
        // write is permitted in transaction scope only
        tresult.assert_exception<OP::Exception>([&]() {
            auto wr = memory_service.writable_block(addr, test_block_size_c);
            std::ignore = wr;
            }, OP_CODE_DETAILS("writable_block without transaction must raise exception"))
            .then([](const OP::Exception& ex) {
                return ex.code() == OP::vtm::ErrorCodes::er_transaction_not_started;
            }, OP_CODE_DETAILS("Exception code must be er_transaction_not_started"));
        // the same for typed accessor
        tresult.assert_exception<OP::Exception>([&]() {
            auto wr = memory_service.access<TestPod>(addr);
            std::ignore = wr;
            }, OP_CODE_DETAILS("access<T> without transaction must raise exception"));
    }

    void test_write_no_previous_content(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);
        TrieMemoryService& memory_service = toi.memory_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto addr = toi.allocator_service().allocate(test_block_size_c, AllocHint::regular);
        const auto pattern_a = make_pattern(0x10, test_block_size_c);
        const auto pattern_b = make_pattern(0x80, test_block_size_c);
        commit_pattern(toi, addr, pattern_a);

        // partial overwrite in the middle of committed block with need_previous_content=false
        constexpr segment_pos_t offset_c = 16, part_c = 16;
        auto expected = pattern_a;
        std::copy(pattern_b.begin(), pattern_b.begin() + part_c, expected.begin() + offset_c);

        OP::vtm::TransactionGuard g(transaction_service.begin_transaction(), true);
        if (1 == 1)
        {
            auto wr = memory_service.writable_block(addr + offset_c, part_c, false);
            tresult.assert_that<equals>(wr.address(), addr + offset_c, OP_CODE_DETAILS("WR block must keep requested address"));
            tresult.assert_that<equals>(wr.count(), part_c, OP_CODE_DETAILS("WR block must keep requested size"));
            wr.byte_copy(pattern_b.data(), part_c);
        }
        // the rest of the block must stay intact inside transaction ...
        tresult.assert_true(chunk_equals(memory_service.readonly_block(addr, test_block_size_c), expected),
            OP_CODE_DETAILS("Only overwritten part must change inside transaction"));
        g.commit();
        // ... and after commit
        tresult.assert_that<equals>(read_committed(toi, addr, test_block_size_c), expected,
            OP_CODE_DETAILS("Only overwritten part must change after commit"));
    }

    void test_typed_access(TestRuntime& tresult)
    {
        TrieOptionsImpl toi(test_file_name, OpenMode::create);
        TrieMemoryService& memory_service = toi.memory_service();
        TrieTransactionService& transaction_service = toi.transaction_service();

        auto single_addr = toi.allocator_service().allocate_for<TestPod>();
        constexpr segment_pos_t array_size_c = 5;
        auto array_addr = toi.allocator_service().allocate(
            OP::utils::memory_requirement<TestPod>::array_size(array_size_c), AllocHint::array);

        if (1 == 1)
        {
            OP::vtm::TransactionGuard g(transaction_service.begin_transaction(), true);
            // make_new constructs in-place
            TestPod* created = memory_service.make_new<TestPod>(single_addr, 57u, 1.5);
            tresult.assert_that<is_not_null>(created, OP_CODE_DETAILS("make_new must return constructed instance"));
            tresult.assert_that<equals>(created->_a, 57u);
            tresult.assert_that<equals>(created->_b, 1.5);
            // access<T> modifies in the same transaction
            auto wr = memory_service.access<TestPod>(single_addr);
            tresult.assert_that<equals>(wr->_a, 57u, OP_CODE_DETAILS("access<T> must see make_new result"));
            wr->_a = 58u;
            // view<T> inside transaction sees the change
            auto ro = memory_service.view<TestPod>(single_addr);
            tresult.assert_that<equals>(ro->_a, 58u, OP_CODE_DETAILS("view<T> must see own changes"));
            tresult.assert_that<equals>(ro->_b, 1.5);

            // array access by element
            for (segment_pos_t i = 0; i < array_size_c; ++i)
            {
                auto el = memory_service.access_element<TestPod>(array_addr, i);
                el.make_new(i, i * 0.5);
            }
            g.commit();
        }
        // outside transaction
        auto ro = memory_service.view<TestPod>(single_addr);
        tresult.assert_that<equals>(ro->_a, 58u, OP_CODE_DETAILS("view<T> must see committed data"));
        tresult.assert_that<equals>((*ro)._b, 1.5);

        for (segment_pos_t i = 0; i < array_size_c; ++i)
        {
            auto el = memory_service.view_element<TestPod>(array_addr, i);
            tresult.assert_that<equals>(el->_a, i, OP_CODE_DETAILS(<< "view_element<T> must see committed element #" << i));
            tresult.assert_that<equals>(el->_b, i * 0.5, OP_CODE_DETAILS(<< "view_element<T> must see committed element #" << i));
        }
    }

    static auto& module_suite = OP::utest::default_test_suite("Trie.memory_service")
        .declare("creation", test_creation)
        .declare("readonly_no_transaction", test_readonly_no_transaction)
        .declare("write_commit", test_write_commit)
        .declare("write_rollback", test_write_rollback)
        .declare("write_without_transaction", test_write_without_transaction)
        .declare("write_no_previous_content", test_write_no_previous_content)
        .declare("typed_access", test_typed_access)
        ;
}//ns:
