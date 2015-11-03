#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif //_MSC_VER

#include "unit_test.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/CacheManager.h>
#include <op/trie/TransactedSegmentManager.h>
#include <op/trie/MemoryManager.h>
#include "GenericMemoryTest.h"

using namespace OP::trie;
using namespace OP::vtm;

static const char tst_seq[] = { 0, 1, 2, 3, 4, 5, 4, 3, 2, 1 };
static const char override_tst_seq[] = { 65, 64, 63, 62, 61 };
static const std::uint64_t read_only_data_fpos = 32;
static const std::uint64_t writable_data_fpos = 120;
static const std::uint8_t write_fill_seq1[] = { 
0x9B, 0xA8, 0x26, 0xA7, 0x69, 0x14, 0xAC, 0xE7, 0x64, 0xAD, 0x8D, 0xCE, 0xBD, 0x81, 0x5F, 0x63,
0x91, 0x55, 0xD7, 0x5C, 0x87, 0x42, 0xCC, 0x3A, 0xCE, 0x98, 0x75, 0x2B, 0x5B, 0x22, 0x1D, 0xA2,
0x0A, 0x01, 0x39, 0xE9, 0xC8, 0xE1, 0x8E, 0x39, 0xAA, 0x9E, 0x21, 0xF1, 0x0C, 0x0B, 0x7B, 0x19,
0x56, 0xB7, 0x9E, 0xCA, 0xE4, 0x0F, 0xB2, 0x46, 0x21, 0xE4, 0x16, 0xE7, 0x6E, 0xE3, 0xD3, 0xD4,
0x8A, 0xDE, 0xF6, 0xD0, 0xC1, 0x42, 0xEA, 0xE7, 0x5F, 0xA3, 0xF8, 0xED, 0x92, 0x69, 0xED, 0x1A,
0x2B, 0x8B, 0xC9, 0x9D, 0x49, 0xF3, 0x24, 0x9C, 0xB2, 0x42, 0x3A, 0x9C, 0x13, 0x92, 0xDC, 0xF4,
0xF4, 0xAB, 0x51, 0x78, 0x96, 0xF6, 0xEE, 0xCE, 0x69, 0x61, 0xD4, 0x9E, 0xE2, 0x2B, 0xA7, 0xF2,
0x83, 0x1F, 0x90, 0x6D, 0xF6, 0x51, 0xC8, 0x19, 0xE9, 0x5E, 0x5C, 0xB7, 0xD6, 0x89, 0x65, 0x20,
0xB2, 0x9C, 0xD8, 0xBC, 0xAE, 0x15, 0x3E, 0x52, 0x54, 0x0F, 0xE2, 0xC7, 0xBF, 0xB5, 0x4F, 0xE7,
0xB3, 0x06, 0x48, 0xA3, 0x56, 0x92, 0xE5, 0x40, 0x6C, 0x61, 0x72, 0x47, 0x41, 0xC7, 0x00, 0x77,
0xAD, 0xD4, 0x50, 0x9D, 0x28, 0x23, 0xB7, 0x9B, 0x55, 0x45, 0xB9, 0x48, 0x2E, 0xD8, 0x5D, 0xFE,
0xF4, 0x6F, 0x88, 0xBF, 0x1B, 0xBB, 0x6E, 0x3D, 0xC6, 0x7D, 0x3A, 0x1A, 0xDE, 0x96, 0xF2, 0xD0,
0x43, 0x0A, 0x4F, 0x32, 0x3B, 0xBC, 0x8D, 0xB6, 0x19, 0x89, 0xC2, 0x03, 0xF7, 0xBA, 0x75, 0x1D,
0xF9, 0xAE, 0x1A, 0xCF, 0x4A, 0x37, 0x30, 0xC1, 0x7B, 0xF6, 0x98, 0xBC, 0xF4, 0x7C, 0x2C, 0x69,
0xF3, 0x05, 0xF5, 0x62, 0x3A, 0xED, 0xCD, 0x54, 0xD6, 0x6E, 0xF5, 0x9D, 0x35, 0xA3, 0x6B, 0x21,
0x61, 0x24, 0x76, 0xB6, 0xEC, 0xB9, 0x78, 0x54, 0x34, 0xCC, 0x4E, 0x77, 0xE7, 0x39, 0x20, 0x74 
};
static const std::uint8_t write_fill_seq2[] = {
0xC5, 0x4E, 0xCC, 0xCA, 0xAD, 0x18, 0xA5, 0x74, 0x6F, 0xEA, 0x97, 0xD7, 0x5A, 0x46, 0xA0, 0xAD,
0x6E, 0xF2, 0xDF, 0x46, 0xC8, 0x51, 0x20, 0x69, 0x06, 0x19, 0x7B, 0x82, 0x7A, 0xED, 0xEF, 0xEB,
0x55, 0x6C, 0xFC, 0xFD, 0x4C, 0xCF, 0xA9, 0xC3, 0x6C, 0xD2, 0x34, 0x1F, 0x22, 0xE5, 0x32, 0xA3,
0x1D, 0x4A, 0xFF, 0x00, 0x5C, 0xD6, 0x47, 0x85, 0x74, 0xEB, 0xCD, 0x46, 0xFE, 0x1F, 0xED, 0x74,
0x56, 0x17, 0x25, 0x58, 0x3E, 0x42, 0xF9, 0x87, 0xE8, 0x79, 0x2A, 0x7A, 0x57, 0xB7, 0x1F, 0x08,
0x5F, 0x58, 0xAD, 0xD6, 0xB1, 0x61, 0x35, 0xCC, 0x42, 0x63, 0xC4, 0x20, 0x7C, 0xA0, 0x0E, 0x0E,
0x3F, 0xDA, 0xF4, 0xFA, 0x57, 0x7F, 0x23, 0x9C, 0x6C, 0x8C, 0x7D, 0x9C, 0xBA, 0x1C, 0x1D, 0xBD,
0xD6, 0xAD, 0x14, 0xF1, 0xA3, 0x59, 0x1B, 0x49, 0x2D, 0x98, 0x23, 0xE4, 0x64, 0x12, 0xDC, 0x82,
0x0F, 0x43, 0xEB, 0x91, 0x5A, 0x7F, 0xF0, 0x90, 0xEA, 0x6D, 0x1C, 0x96, 0x57, 0xB7, 0x93, 0xD9,
0x4F, 0x22, 0x9D, 0xEA, 0xED, 0x90, 0x7D, 0x0A, 0x31, 0xFB, 0xA3, 0xDE, 0xBA, 0x29, 0x34, 0x7B,
0xCB, 0x94, 0x8E, 0xCD, 0x89, 0x94, 0xDA, 0xB8, 0x4F, 0x31, 0x0E, 0xE5, 0x5E, 0xE4, 0x0F, 0x7C,
0x67, 0x35, 0xA5, 0xFF, 0x00, 0x08, 0x95, 0x94, 0xBA, 0x74, 0x10, 0x1B, 0x39, 0xDE, 0x1B, 0x9C,
0x46, 0xEB, 0xB3, 0x0A, 0xB1, 0x83, 0xF2, 0x80, 0x73, 0x9F, 0x7A, 0x23, 0x45, 0xC5, 0x6A, 0x75,
0x53, 0xA8, 0xA3, 0xA4, 0x91, 0xC8, 0x68, 0x5E, 0x34, 0xD4, 0x52, 0x0B, 0x65, 0x96, 0x39, 0x20,
0xB3, 0x56, 0x30, 0xBB, 0x80, 0x0A, 0xCC, 0x00, 0xC8, 0x07, 0x3F, 0xC5, 0xEE, 0x2B, 0xA4, 0x1E,
0x2F, 0x79, 0xC2, 0xDC, 0x41, 0x03, 0x58, 0xBD, 0xE3, 0x71, 0x1B, 0x8E, 0x62, 0x5E, 0x80, 0x7B
};
template <class Sm>
void test_overlappedException(Sm& tmanager)
{
    try{
        tmanager->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq)+10);
        OP_UTEST_ASSERT(false);//exception must be raised
    }
    catch (Exception const& e){
        OP_UTEST_ASSERT(e.code() == er_overlapping_block);
    }
    //check overlapping exception #2
    try{
        tmanager->readonly_block(FarAddress(read_only_data_fpos-1), sizeof(tst_seq));
        OP_UTEST_ASSERT(false);//exception must be raised
    }
    catch (Exception const& e){
        OP_UTEST_ASSERT(e.code() == er_overlapping_block);
    }
}
/**Check that this transaction sees changed data, but other transaction doesn't*/
template <class Sm>
void test_TransactionIsolation(Sm& tmanager, std::uint64_t pos, segment_pos_t block_size, const std::uint8_t *written)
{
    auto ro_block = tmanager->readonly_block(FarAddress(pos), block_size);
    //ro must see changes
    OP_UTEST_ASSERT(0 == memcmp(written, ro_block.pos(), block_size));
    std::future<bool> future_block1_t2 = std::async(std::launch::async, [&](){
        try{
            auto ro_block2 = tmanager->readonly_block(FarAddress(pos), sizeof(block_size));
        }
        catch (const ConcurentLockException&)
        {
            return true;
        }
        return false;//exception wasn't raised
    });
    OP_UTEST_ASSERT( future_block1_t2.get() );
    std::future<bool> future_block2_t2 = std::async(std::launch::async, [&](){
        TransactionGuard g(tmanager->begin_transaction());
        try{
            auto ro_block2 = tmanager->readonly_block(FarAddress(pos), sizeof(block_size));
        }
        catch (const ConcurentLockException&)
        {
            return true;
        }
        return false;//exception wasn't raised
    });
    OP_UTEST_ASSERT( future_block2_t2.get() );
}

void test_TransactedSegmentManager(OP::utest::TestResult &tresult)
{
    tresult.info() << "test Transacted Segment Manager..." << std::endl;

    const char seg_file_name[] = "t-segementation.test";

    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(1024));
    tmngr1->ensure_segment(0);
    std::fstream fdata_acc(seg_file_name, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    tresult.assert_true(fdata_acc.good());
    
    //read out of tran must be permitted
    ReadonlyMemoryRange ro_block1 = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
    //the same should be returned for another thread
    std::future<ReadonlyMemoryRange> future_block1_t1 = std::async(std::launch::async, [ tmngr1](){
        return tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
    });
    auto ro_block1_t1 = future_block1_t1.get();
    tresult.assert_true(0 == memcmp(&ro_block1, &ro_block1_t1, sizeof(ReadonlyMemoryRange)));
    //check ro have same view
    fdata_acc.seekp(read_only_data_fpos);
    fdata_acc.write(tst_seq, sizeof(tst_seq));
    fdata_acc.seekp(writable_data_fpos);
    fdata_acc.write((const char*)write_fill_seq1, sizeof(write_fill_seq1));
    fdata_acc.flush();
    //check data has appeared
    tresult.assert_true(0 == memcmp(tst_seq, ro_block1.pos(), sizeof(tst_seq)));

    //Test rollback without keeping locks
    if (1 == 1)
    {
        OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
        auto tx_rw = tmngr1->writable_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
        tresult.assert_true(0 == memcmp(tst_seq, tx_rw.pos(), sizeof(tst_seq)), OP_CODE_DETAILS( << "hello" ));
    }
    std::future<ReadonlyMemoryRange> future_check_unlock = std::async(std::launch::async, [ tmngr1](){
        return tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq)+10);
    });
    auto check_unlock = future_check_unlock.get();
    tresult.assert_true(0 == memcmp(tst_seq, check_unlock.pos(), sizeof(tst_seq)), OP_CODE_DETAILS( << "Check that overlapped block allowed when no transactions" ));

    //do the check data from file for transactions
    auto tr1 = tmngr1->begin_transaction();
    ReadonlyMemoryRange ro_block2 = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
    tresult.assert_true(0 == memcmp(&ro_block1, &ro_block2, sizeof(ReadonlyMemoryRange)));
    tresult.assert_true(0 == memcmp(tst_seq, ro_block2.pos(), sizeof(tst_seq)));

    std::future<bool> future_t2 = std::async(std::launch::async, [tmngr1](){
        OP::vtm::TransactionGuard transaction2 (tmngr1->begin_transaction());
        //check overlapping exception
        test_overlappedException(tmngr1);
        auto tx_ro = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
        return (0 == memcmp(tst_seq, tx_ro.pos(), sizeof(tst_seq)));
    });
    //while there is another tran
    test_overlappedException(tmngr1); 
    tresult.assert_true( future_t2.get() );
    //test brand new region write
    auto wr_range1 = tmngr1->writable_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq2));
    memcpy(wr_range1.pos(), write_fill_seq2, sizeof(write_fill_seq2));
    test_TransactionIsolation(tmngr1, writable_data_fpos, sizeof(write_fill_seq1), write_fill_seq2);
    tr1->commit();
    //test all threads can see new data
    std::future<bool> future_read_committed_block1_t2 = std::async(std::launch::async, [&](){
        auto ro_block2 = tmngr1->readonly_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq2));
        return (0 == memcmp(write_fill_seq2, ro_block2.pos(), sizeof(write_fill_seq2)));        
    });
    tresult.assert_true(future_read_committed_block1_t2.get());
    //without transaction check that write not allowed
    try{
        tmngr1->writable_block(FarAddress(read_only_data_fpos), 1);
        tresult.assert_true(false);//exception must be raised
    }
    catch (Exception const &e){
        tresult.assert_true(e.code() == er_transaction_not_started);
    }
    //when all transaction closed, no overlapped exception anymore
    auto overlapped_block = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq)+10);
    tresult.assert_true(0 == memcmp(tst_seq, overlapped_block.pos(), sizeof(tst_seq)), OP_CODE_DETAILS( << "part of overlaped is not correct" ));

}

void test_TransactedSegmentGenericMemoryAlloc(OP::utest::TestResult &tresult)
{
    const char seg_file_name[] = "t-segementation.test";
    test_MemoryManager<TransactedSegmentManager>(seg_file_name, tresult);

}
void test_TransactedSegmentManagerMemoryAllocator(OP::utest::TestResult &tresult)
{
    tresult.info() << "test Transacted Memory Allocation..." << std::endl;
    const char seg_file_name[] = "t-segementation.test";
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(seg_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    auto aa1 = tmngr1->segment_size();
    //tmngr1->ensure_segment(0);
    SegmentTopology<MemoryManager>& mngrToplogy 
        = *new SegmentTopology<MemoryManager>(tmngr1);

    struct TestAbc
    {
        int a;
        double b;
        char c[10];
        TestAbc(int a, double b, const char *c):
            a(a),
            b(b)
        {
            strncpy(this->c, c, sizeof(this->c));
        }
    };
    auto & mm = mngrToplogy.slot<MemoryManager>();
    auto test_avail = mm.available(0);
    try{
        auto abc1_off = mm.make_new<TestAbc>(1, 1.01, "abc");
        OP_UTEST_FAIL(<< "Exception OP::trie::er_transaction_not_started must be raised");
    }
    catch (OP::trie::Exception& e)
    {
        OP_UTEST_ASSERT(e.code() == OP::trie::er_transaction_not_started, << "must raise exception with code OP::trie::er_transaction_not_started");
    }
    mngrToplogy._check_integrity();
    
    OP::vtm::TransactionGuard transaction1(tmngr1->begin_transaction());
    auto abc1_off = mm.make_new<TestAbc>(1, 1.01, "abc");
    transaction1.commit();

    static const unsigned consumes[] = { 16, 16, 32, 113, 57, 320, 1025, 23157 };
    //ensure that thread-scenario works in single thread

    static const int test_threads = 20;
    std::atomic<int> synchro_count (0);
    std::condition_variable synchro_start;
    std::mutex synchro_lock;
    auto intensiveConsumption = [&](){
        --synchro_count;
        if (synchro_count)
        {
            std::unique_lock< std::mutex > g(synchro_lock);
            synchro_start.wait(g, [&synchro_count]{return synchro_count == 0; });
        }
        else
            synchro_start.notify_all();
        std::array<std::uint8_t*, std::extent<decltype(consumes)>::value> managed_ptr{};
        OP::vtm::TransactionGuard transaction1(tmngr1->begin_transaction());
        for (auto i = 0; i < managed_ptr.max_size(); ++i)
            managed_ptr[i] = mm.allocate(consumes[i]);
        transaction1.commit();
        OP::vtm::TransactionGuard transaction2(tmngr1->begin_transaction());
        //dealloc even
        for (auto i = 0; i < managed_ptr.max_size(); i += 2)
            mm.deallocate(managed_ptr[i]);
        transaction2.commit();
        OP::vtm::TransactionGuard transaction3(tmngr1->begin_transaction());
        //dealloc odd
        for (auto i = 1; i < managed_ptr.max_size(); i += 2)
            mm.deallocate(managed_ptr[i]);
        transaction3.commit();
    };
    //ensure that thread-scenario works in single thread
    synchro_count = 1;
    intensiveConsumption();
    tmngr1->_check_integrity();

    synchro_count = test_threads;
    std::vector<std::thread> parallel_tests;
    for (auto i = 0; i < test_threads; ++i)
    {
        parallel_tests.emplace_back(intensiveConsumption);
    }
    for (auto i = 0; i < test_threads; ++i)
        parallel_tests[i].join();
    tmngr1->_check_integrity();
}


//using std::placeholders;
static auto module_suite = OP::utest::default_test_suite("TransactedSegmentManager")
->declare(test_TransactedSegmentManager, "general")
->declare(test_TransactedSegmentGenericMemoryAlloc, "memoryAlloc")
->declare(test_TransactedSegmentManagerMemoryAllocator, "multithread")
;