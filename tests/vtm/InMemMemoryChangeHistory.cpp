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

    using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;
    using RWR = typename MemoryChangeHistory::RWR;


    void test_Emplace(OP::utest::TestRuntime& tresult)
    {

        OP::utils::ThreadPool thread_pool;
        OP::vtm::InMemoryChangeHistory m_history(thread_pool);

        constexpr size_t block_test_width_c = 32;

        // data generator for specific transaction
        auto test_block_gen = [&](transaction_id_t id) -> std::string {
            std::string result(block_test_width_c, ' ');
            char start_from = (id + ' ') % block_test_width_c + '0';
            std::iota(result.begin(), result.end(), start_from);
            return result;
            };

        constexpr transaction_id_t max_tran_n = 28;

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

        OP::utils::Waitable<bool, OP::utils::WaitableSemantic::all> sync_start(false);

        for (int current_thread = 0; current_thread < threads_count_c; ++current_thread)
            test_tasks.emplace_back(thread_pool.async([&](transaction_id_t current_tran) {
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
        tresult.info() << "Test altering of memory\n";

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
    
    void test_ROConcurrent(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool thread_pool;
        OP::vtm::InMemoryChangeHistory m_history(thread_pool);

        const std::string wr_data( { 'w', 'r', 'w', 'r', 'w', 'r', 'w', 'r', 'w', 'r', 'w', 'r', 'w', 'r', '0', '1' });
        const std::string ro_data( { 'r', 'o', 'r', 'o', 'r', 'o', 'r', 'o', 'r', 'o', 'r', 'o', 'r', 'o', '3', '4', });
        const transaction_id_t wr_tran = 1;
        const transaction_id_t ro_tran = 2;

        //check memory isolation
        auto check_memory_isolation = [&](const RWR& range, transaction_id_t transaction, ReadIsolation isolation, const std::string& expected) {
            auto current = m_history.read_isolation(isolation);
            assert(expected.size() == range.count());
            try {
                std::string random_data(range.count(), 'R');
                auto ro_block = m_history.buffer_of_region(range, transaction, MemoryRequestType::ro, 
                    isolation == ReadIsolation::ReadCommitted 
                    ? expected.data()
                    : random_data.data());
                auto& buf = std::get<ShadowBuffer>(ro_block);
                tresult.assert_that<eq_ranges>(
                    expected.begin(), expected.end(),
                    buf.get(), buf.get() + buf.size(),
                    OP_CODE_DETAILS() << "RO-tran #" << transaction << " resolved wrong data\n"
                );
                //restore previous isolation
                m_history.read_isolation(current);
            }
            catch (...) {
                //restore previous
                m_history.read_isolation(current);
                throw;
            }
            };


        m_history.on_new_transaction(wr_tran);
        RWR wr_range{ 10, wr_data.size() };
        auto wr_block = m_history.buffer_of_region(wr_range, wr_tran, MemoryRequestType::wr, wr_data.data());
        auto& buf1 = std::get<ShadowBuffer>(wr_block);
        tresult.assert_that<eq_ranges>(
            buf1.get(), buf1.get() + buf1.size(),
            wr_data.begin(), wr_data.end());

        tresult.info() << "Test same tran, RO request can see changes of data...\n";
        for (ReadIsolation l : {ReadIsolation::Prevent, ReadIsolation::ReadCommitted, ReadIsolation::ReadUncommitted})
            check_memory_isolation(wr_range, wr_tran, l, wr_data);

        tresult.info() << "Test other tran, RO request for dirty-read can see changes of data...\n";
        check_memory_isolation(wr_range, ro_tran, ReadIsolation::ReadUncommitted, wr_data);

        tresult.info() << "Test other tran, RO request cannot see changes of data...\n";
        check_memory_isolation(wr_range, ro_tran, ReadIsolation::ReadCommitted, ro_data);

        tresult.info() << "Test other tran, RO request prevents changes of data...\n";
        tresult.assert_exception<std::bad_variant_access>([&]() {
            check_memory_isolation(wr_range, ro_tran, ReadIsolation::Prevent, ro_data);
            });

        RWR wr_no_history_range{ 100, 12 };

        auto wr_block_no_history = m_history.buffer_of_region(wr_no_history_range, wr_tran, MemoryRequestType::wr_no_history, nullptr);
        auto& buf4 = std::get<ShadowBuffer>(wr_block_no_history);
        for (auto i = 0; i < wr_no_history_range.count(); ++i) //alter chunk with 'x'*12 string
            buf4.get()[i] = 'x';

        auto ro_block_no_history = m_history.buffer_of_region(wr_no_history_range, wr_tran, MemoryRequestType::ro, nullptr);
        auto& buf5 = std::get<ShadowBuffer>(ro_block_no_history);
        tresult.assert_that<eq_ranges>(
            buf4.get(), buf4.get() + buf4.size(),
            buf5.get(), buf5.get() + buf5.size(), "Same tran ro buffer must see changes");

        std::string other_tran_ro_data2(wr_no_history_range.count(), '-');
        tresult.info() << "Test other tran, RO request cannot see changes of data from wr_no_history...\n";
        check_memory_isolation(wr_no_history_range, ro_tran, ReadIsolation::ReadCommitted, other_tran_ro_data2);

        tresult.info() << "Test other tran, RO request for dirty-read can see changes of data from wr_no_history...\n";
        check_memory_isolation(wr_no_history_range, ro_tran, ReadIsolation::ReadUncommitted, 
            std::string(buf4.get(), buf4.get()+buf4.size()));

        tresult.info() << "Test other tran, RO request prevents changes of data from wr_no_history...\n";
        tresult.assert_exception<std::bad_variant_access>([&]() {
            check_memory_isolation(wr_no_history_range, ro_tran, ReadIsolation::Prevent, other_tran_ro_data2);
            });
    }

    static auto& module_suite = OP::utest::default_test_suite("vtm.InMemMemoryChangeHistory")
        .declare("emplace", test_Emplace)
        .declare("ro-concurrent", test_ROConcurrent)
        ;
} //ns:
