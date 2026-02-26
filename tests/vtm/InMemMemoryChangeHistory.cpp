#include <unordered_set>
#include <unordered_map>
#include <execution>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/flur/flur.h>

#include <op/vtm/managers/InMemMemoryChangeHistory.h>

#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/Range.h>

namespace
{
    using namespace OP::utest;
    using namespace std::string_literals;
    using namespace OP::vtm;


    void test_Emplace(OP::utest::TestRuntime& tresult)
    {
        using RWR = typename MemoryChangeHistory::RWR;
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;

        OP::utils::ThreadPool tp;
        OP::vtm::InMemoryChangeHistory m_history(tp);

        constexpr std::uint8_t transactions_per_file_c = 5;
        constexpr size_t block_test_width_c = 32;

        // data generator for specific transaction
        auto test_block_gen = [&](transaction_id_t id) -> std::string {
            std::string result(block_test_width_c, ' ');
            char start_from = (id + ' ') % block_test_width_c + '0';
            std::iota(result.begin(), result.end(), start_from);
            return result;
            };

        constexpr transaction_id_t max_tran_n = transactions_per_file_c * 5 + 3;
        constexpr auto as_buf = [](auto history) {
            return std::get<ShadowBuffer>(std::move(history));
            };
        constexpr auto as_err = [](auto history) {
            return std::get<typename MemoryChangeHistory::ConcurrentAccessError>(std::move(history)); };

        tresult.info() << "Basic parallel wr/ro acceess for multiple transactions\n";

        RWR rwr{ 0, block_test_width_c };
        for (auto tran_id = 0; tran_id < max_tran_n; ++tran_id)
        {
            m_history.on_new_transaction(tran_id);
            auto init_buf = test_block_gen(tran_id);
            //make alloc without intersection
            ShadowBuffer buffer = as_buf(
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, init_buf.data()));
            tresult.assert_true(0 == memcmp(buffer.get(), init_buf.data(), rwr.count()),
                "test if init data has been applied");
            buffer = as_buf(
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::ro, nullptr));
            tresult.assert_true(0 == memcmp(buffer.get(), init_buf.data(), rwr.count()),
                "test ro buffer");
            rwr = rwr.offset(block_test_width_c);
        }
        ///////////////////////////////////////////////////////////////////////////
        /// Test `destroy` and concurrent access
        ///////////////////////////////////////////////////////////////////////////
        tresult.info() << "Test `destroy` and concurrent access\n";
        rwr = RWR{ 0, block_test_width_c };
        // test `destroy` not visible anymore for scope
        std::vector<std::future<void>> test_tasks;
        constexpr size_t parallel_factor_c = 5;
        constexpr size_t threads_count_c = parallel_factor_c * 2;
        OP::utils::ThreadPool thr_pool(threads_count_c);
        OP::utils::Waitable<bool, OP::utils::WaitableSemantic::all> sync_start(false);

        for (int current_thread = 0; current_thread < threads_count_c; ++current_thread)
            test_tasks.emplace_back(thr_pool.async([&](transaction_id_t current_tran) {
            auto init_buf = test_block_gen(current_tran);
            std::ostringstream os;  //render some noise stuff
            os << std::setfill('-')
                << std::setw(block_test_width_c / 2)
                << std::this_thread::get_id();
            sync_start.wait(false); //wait other threads
            auto block_position = rwr.offset(current_tran * block_test_width_c);
            // change region of memory
            ShadowBuffer buffer = as_buf(
                m_history.buffer_of_region(block_position, current_tran, OP::vtm::MemoryRequestType::wr, nullptr));
            std::string start_from(buffer.get(), buffer.get() + buffer.size());
            memcpy(buffer.get(), os.str().c_str(), block_test_width_c / 2); //put random stuff to wr-buffer
            //ensure changes (with respect to MT it can be anything, so just check buffer != init_buf)
            ShadowBuffer ro = as_buf(
                m_history.buffer_of_region(block_position, current_tran, OP::vtm::MemoryRequestType::ro, nullptr));
            tresult.assert_false(0 == memcmp(ro.get(), init_buf.c_str(), block_test_width_c / 2));
            //restore previous state by `destroy` just changed buffer
            m_history.destroy(current_tran, std::move(buffer));
            /* There code cannot check that destroy rollback work, when parallel_factor_c > 1 on races so do
            it later out of thread code */
                }, /*use # of thread as transaction id*/current_thread % parallel_factor_c));

        sync_start = true; //notify all to start
        for (auto& thr : test_tasks)
            thr.get(); //on exception inside thread it will be re-raised
        //when all threads done, check that memory is intact
        for (transaction_id_t current_tran = 0; current_tran < parallel_factor_c; ++current_tran)
        {
            auto init_buf = test_block_gen(current_tran);
            auto block_position = rwr.offset(current_tran * block_test_width_c);

            ShadowBuffer ro = as_buf(
                m_history.buffer_of_region(block_position, current_tran, OP::vtm::MemoryRequestType::ro, nullptr));
            tresult.assert_that<OP::utest::eq_ranges>(
                ro.get(), ro.get() + ro.size(),
                init_buf.c_str(), init_buf.c_str() + init_buf.size()
            );
        }

        ///////////////////////////////////////////////////////////////////////////
        /// Test altering of memory
        ///////////////////////////////////////////////////////////////////////////
        tresult.info() << "Test `destroy` and concurrent access\n";

        //alter some buffers
        rwr = RWR{ 1, block_test_width_c / 2 };
        std::string alt_text(block_test_width_c / 2, 'x');
        for (auto tran_id = 0; tran_id < max_tran_n; tran_id += 2/*!!*/)
        {
            auto init_buf = test_block_gen(tran_id);
            //make alloc where requested buffer fully inside same transaction buffer [1, 1+block_test_width_c / 2)
            ShadowBuffer buffer =
                as_buf(m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, nullptr));
            tresult.assert_true(0 == memcmp(buffer.get(), init_buf.data() + 1, rwr.count()),
                "test if previous changes has been taken");
            //override previously captured buffer:
            memcpy(buffer.get(), alt_text.data(), alt_text.size());
            rwr = rwr.offset(block_test_width_c).offset(block_test_width_c); //offset twice as tran_id+=2
        }
        //checks concurrent block are detected
        rwr = RWR{ block_test_width_c / 2, block_test_width_c }; //queried block intersects 2 transactions' buffer
        for (auto tran_id = 0; tran_id < (max_tran_n - 1/*! need skip last as no transaction intersect*/);
            ++tran_id, rwr = rwr.offset(block_test_width_c))
        {
            auto check_error_correctness = [&](const typename MemoryChangeHistory::ConcurrentAccessError& err) {
                tresult.assert_that<equals>(
                    err._requesting_transaction, tran_id, "wrong requested tran");
                tresult.assert_that<equals>(
                    err._requested_range, rwr, "wrong requested range");
                tresult.assert_that<equals>(
                    err._locking_transaction, tran_id + 1, "wrong locked transaction");
                RWR locked_by{ (tran_id + 1) * block_test_width_c, block_test_width_c };
                tresult.assert_that<equals>(
                    err._locked_range, locked_by, "wrong locked transaction");

                };
            //make alloc with intersection
            check_error_correctness(as_err(
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, nullptr)));
            //make alloc with intersection no-history
            check_error_correctness(as_err(
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr_no_history, nullptr)));
        }

        rwr = RWR{ 0, block_test_width_c };
        for (auto tran_id = 0; tran_id < max_tran_n; ++tran_id, rwr = rwr.offset(rwr.count()))
        {
            auto init_buf = test_block_gen(tran_id);
            if ((tran_id % 2) == 0)
                memcpy(init_buf.data() + 1, alt_text.data(), alt_text.size());
            ShadowBuffer buffer = as_buf(
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::ro, nullptr));
            tresult.assert_true(
                0 == memcmp(buffer.get(), init_buf.data(), rwr.count()),
                "test if changes are visible later");

            if ((tran_id % 2) == 0)
                m_history.on_commit(tran_id);
            else
                m_history.on_rollback(tran_id);
        }

        ///////////////////////////////////////////////////////////////////////////
        ///check ranges are available again for new transactions
        ///////////////////////////////////////////////////////////////////////////
        tresult.info() << "Test ranges are accessible after commit/rollback\n";
        rwr = RWR{ 0, block_test_width_c };
        for (transaction_id_t i = 0; i < max_tran_n; ++i, rwr = rwr.offset(rwr.count()))
        {
            auto tran_id = i + max_tran_n; //simulate growing of tran-id
            //make alloc with intersection
            ShadowBuffer buffer = as_buf(
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, nullptr));
            tresult.assert_true(buffer.size(), "intersected block after all commits must be allowed");
        }

    }

    static auto& module_suite = OP::utest::default_test_suite("vtm.InMemMemoryChangeHistory")
        .declare("emplace", test_Emplace)
        ;
} //ns:
