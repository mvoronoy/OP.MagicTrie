#pragma once
#ifndef _MEMORYCHANGEHISTORYTESTHARNESS__H_
#define _MEMORYCHANGEHISTORYTESTHARNESS__H_


#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/flur/flur.h>

#include <op/vtm/AppendOnlySkipList.h>
#include <op/vtm/AppendOnlyLogFileRotation.h>

#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/Range.h>


namespace test::vtm
{
     inline void test_memory_change_history(
         OP::utest::TestRuntime& tresult,
         OP::vtm::MemoryChangeHistory& m_history)
     {
     
        using namespace std::string_literals;
        using namespace OP::vtm;

        using RWR = typename MemoryChangeHistory::RWR;
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;
        
        constexpr std::uint8_t transactions_per_file_c = 5;
        constexpr size_t block_test_width_c = 32;

        auto test_block_gen = [&](transaction_id_t id) -> std::string {
            std::string result( block_test_width_c, ' ' );
            char start_from = (id + ' ') % block_test_width_c + '0';
            std::iota(result.begin(), result.end(), start_from);
            return result;
            };

        constexpr transaction_id_t max_tran_n = transactions_per_file_c * 5 + 3;

        RWR rwr{ 0, block_test_width_c };
        for (auto tran_id = 0; tran_id < max_tran_n; ++tran_id)
        {
            m_history.on_new_transaction(tran_id);
            auto init_buf = test_block_gen(tran_id);
            //make alloc without intersection
            ShadowBuffer buffer = std::move(*
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, init_buf.data()));
            tresult.assert_true(0 == memcmp(buffer.get(), init_buf.data(), rwr.count()), 
                "test if init data has been applied");
            buffer = std::move(*
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::ro, nullptr));
            tresult.assert_true(0 == memcmp(buffer.get(), init_buf.data(), rwr.count()),
                "test ro buffer");
            rwr = rwr.offset(rwr.count());
        }
        //alter some buffers
        rwr = RWR{ 1, block_test_width_c/2 };
        std::string alt_text(block_test_width_c / 2, 'x');
        for (auto tran_id = 0; tran_id < max_tran_n; tran_id += 2)
        {
            auto init_buf = test_block_gen(tran_id);
            //make alloc without intersection
            ShadowBuffer buffer =
                std::move(*m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, nullptr));
            tresult.assert_true(0 == memcmp(buffer.get(), init_buf.data()+1, rwr.count()),
                "test if previous buffer taken");
            //override previously captured buffer:
            memcpy(buffer.get(), alt_text.data(), alt_text.size());
            rwr = rwr.offset(block_test_width_c).offset(block_test_width_c); //offset twice as tran_id+=2
        }
        //checks concurrent block are detected
        rwr = RWR{ block_test_width_c/2, block_test_width_c };
        for (auto tran_id = 0; tran_id < (max_tran_n - 1/*! need skip last as no transaction intersect*/); 
            ++tran_id, rwr = rwr.offset(rwr.count()))
        {
            //make alloc with intersection
            std::optional<ShadowBuffer> buffer = 
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, nullptr);
            tresult.assert_false(buffer.has_value(), "intersected block from different transactions is prohibited");
        }

        rwr = RWR{ 0, block_test_width_c };
        for (auto tran_id = 0; tran_id < max_tran_n; ++tran_id, rwr = rwr.offset(rwr.count()))
        {
            auto init_buf = test_block_gen(tran_id);
            if ((tran_id % 2) == 0)
                memcpy(init_buf.data() + 1, alt_text.data(), alt_text.size());
            ShadowBuffer buffer = std::move(*
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::ro, nullptr));
            tresult.assert_true(
                0 == memcmp(buffer.get(), init_buf.data(), rwr.count()),
                "test if changes are visible later");
            
            if ((tran_id % 2) == 0)
                m_history.on_commit(tran_id);
            else
                m_history.on_rollback(tran_id);
            m_history.destroy(tran_id, std::move(buffer));
        }
        //check ranges are available again for new transactions
        rwr = RWR{ 0, block_test_width_c };
        for (transaction_id_t i = 0; i < max_tran_n; ++i, rwr = rwr.offset(rwr.count()))
        {
            auto tran_id = i + max_tran_n; //simulate growing of tran-id
            //make alloc with intersection
            std::optional<ShadowBuffer> buffer =
                m_history.buffer_of_region(rwr, tran_id, OP::vtm::MemoryRequestType::wr, nullptr);
            tresult.assert_true(buffer.has_value(), "intersected block after all commits must be allowed");
        }

     }
}//ns:test.vtm
#endif //_MEMORYCHANGEHISTORYTESTHARNESS__H_