#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif //_MSC_VER

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/EventSourcingSegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include "GenericMemoryTest.h"

using namespace OP::trie;
using namespace OP::vtm;
using namespace OP::utest;

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
void test_TransactionIsolation(Sm& tmanager, std::uint64_t pos, segment_pos_t block_size, 
    const std::uint8_t *written,
    const std::uint8_t* origin
    )
{
    auto ro_block = tmanager->readonly_block(FarAddress(pos), block_size);
    //ro must see changes
    OP_UTEST_ASSERT(0 == memcmp(written, ro_block.pos(), block_size));
    
    std::future<bool> future_block1_read_cmmitted = std::async(std::launch::async, [&]() {
        //another tran with ReadCommitted isolation must see previous state
        auto ro_block2 = tmanager->readonly_block(FarAddress(pos), sizeof(block_size));
        OP_UTEST_ASSERT(0 == memcmp(origin, ro_block2.pos(), block_size));
        return true;
    });
    OP_UTEST_ASSERT(future_block1_read_cmmitted.get());

    auto prev_isolation = 
        tmanager->read_isolation(OP::trie::ReadIsolation::Prevent);
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
void test_Locking(OP::utest::TestRuntime& tresult)
{
    const char seg_file_name[] = "t-segementation.test";

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(1024));
    tmngr1->ensure_segment(0);
    constexpr segment_pos_t start_offset_c = 0x40;
    constexpr segment_pos_t test_seq_len_c = 0x20;
    atom_string_t test_seq(write_fill_seq1, write_fill_seq1 + test_seq_len_c);

    OP::vtm::TransactionGuard op_g(tmngr1->begin_transaction()); 
    auto bx40_x20 = tmngr1->writable_block(FarAddress(0, start_offset_c), test_seq_len_c);
    bx40_x20.byte_copy(test_seq.data(), test_seq_len_c);
    auto check_wait = std::async(std::launch::async, [&]() {
        OP::vtm::TransactionGuard guard2(tmngr1->begin_transaction());
        tresult.assert_exception <OP::vtm::ConcurentLockException >([&]() {
            tmngr1->writable_block(FarAddress(0, start_offset_c - 1), 0x2);
            });
        tresult.assert_exception <OP::vtm::ConcurentLockException >([&]() {
            tmngr1->writable_block(FarAddress(0, start_offset_c), 0x2);
            });
        tresult.assert_exception <OP::vtm::ConcurentLockException >([&]() {
            tmngr1->writable_block(FarAddress(0, start_offset_c+0x1E), 0x20);
            });
        //even after retry
        tresult.assert_exception <OP::vtm::ConcurentLockException >([&]()
            {
                auto header = OP::vtm::transactional_yield_retry_n<10>([&]()
                    {
                        return tmngr1->wr_at<double>(FarAddress(0, start_offset_c));
                    });
                *header = 57.75;
            });
        guard2.commit();
        });
    check_wait.get();//void
    op_g.commit();
    auto ro_x40_x20 = tmngr1->readonly_block(FarAddress(0, start_offset_c), test_seq_len_c);
    segment_pos_t idx = 0;
    for (auto ca : test_seq)
        tresult.assert_that<equals>(ca, ro_x40_x20.pos()[idx++]);
}
void test_EvSrcSegmentManager(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "test Transacted Segment Manager..." << std::endl;

    const char seg_file_name[] = "t-segementation.test";

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(1024));
    tmngr1->ensure_segment(0);
    std::fstream fdata_acc(seg_file_name, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    tresult.assert_true(fdata_acc.good());
    
    //read out of tran must be permitted
    ReadonlyMemoryChunk ro_block1 = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
    //check ro have same view
    fdata_acc.seekp(read_only_data_fpos);
    fdata_acc.write(tst_seq, sizeof(tst_seq));
    fdata_acc.seekp(writable_data_fpos);
    fdata_acc.write((const char*)write_fill_seq1, sizeof(write_fill_seq1));
    fdata_acc.flush();
    //check data has appeared
    tresult.assert_true(0 == memcmp(tst_seq, ro_block1.pos(), sizeof(tst_seq)), OP_CODE_DETAILS(<< "External file changes must be seen by RO block"));
    //the same should be returned for another thread
    std::future<ReadonlyMemoryChunk> future_block1_t1 = std::async(std::launch::async, [ tmngr1](){
        return tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
    });
    auto ro_block1_t1 = future_block1_t1.get();
    tresult.assert_true(0 == memcmp(ro_block1.at<char>(0), ro_block1_t1.at<char>(0), sizeof(ReadonlyMemoryChunk)), "RO memory block from different thread must return same bytes");

    //Test rollback without keeping locks
    if (1 == 1)
    {
        OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
        auto tx_rw = tmngr1->writable_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
        tresult.assert_true(0 == memcmp(tst_seq, tx_rw.pos(), sizeof(tst_seq)), 
            OP_CODE_DETAILS( << "Check write transaction see previous memory state" ));
    }
    std::future<ReadonlyMemoryChunk> future_check_unlock = std::async(std::launch::async, [ tmngr1](){
        return tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq)+10);
    });
    auto check_unlock = future_check_unlock.get();
    tresult.assert_true(0 == memcmp(tst_seq, check_unlock.pos(), sizeof(tst_seq)), 
        OP_CODE_DETAILS( << "Check that overlapped block allowed when no transactions" ));

    //do the check data from file for transactions
    auto tr1 = tmngr1->begin_transaction();
    ReadonlyMemoryChunk ro_block2 = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
    tresult.assert_true(ro_block1.address() == ro_block2.address() && ro_block1.count() == ro_block2.count(), 
        OP_CODE_DETAILS( <<"RO block inside transaction must point the same memory"));
    tresult.assert_true(tools::range_equals(
        tst_seq, tst_seq + sizeof(tst_seq), ro_block2.pos(), ro_block2.pos()+sizeof(tst_seq)), 
        OP_CODE_DETAILS( << "RO block inside transaction must point the same memory"));
    /*
    This block of test doesn't work anymore since readonly now allows extending the memory blocks
    std::mutex mutex1;
    std::unique_lock<std::mutex> lk1(mutex1);

    std::future<bool> future_t2 = std::async(std::launch::async, [tmngr1, &mutex1](){
        OP::vtm::TransactionGuard transaction2 (tmngr1->begin_transaction());
        //check overlapping exception
        test_overlappedException(tmngr1);
        auto tx_ro = tmngr1->readonly_block(FarAddress(read_only_data_fpos), sizeof(tst_seq));
        std::unique_lock<std::mutex> lk_local(mutex1); //wait for other overlapped
        lk_local.unlock();
        return (0 == memcmp(tst_seq, tx_ro.pos(), sizeof(tst_seq)));
    });
    //while there is another tran
    test_overlappedException(tmngr1); 
    lk1.unlock();
    tresult.assert_true( future_t2.get() );
    */
    //test brand new region write
    auto range1_origin = tmngr1->readonly_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq2));
    auto wr_range1 = tmngr1->writable_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq2));
    memcpy(wr_range1.pos(), write_fill_seq2, sizeof(write_fill_seq2));
    test_TransactionIsolation(
        tmngr1, writable_data_fpos, sizeof(write_fill_seq1), write_fill_seq2, range1_origin.pos());
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
    //test block inclusion
}
/*!!!!!!!!!==>
void test_FarAddress(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "test FAR address translation ..." << std::endl;
    const char seg_file_name[] = "t-segementation.test";
    const FarAddress test_addr_seg0(0, 100);
    const FarAddress test_addr_seg1(1, 100);
    const FarAddress test_addr2_seg1(1, 200);

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    tmngr1->ensure_segment(0);
    tmngr1->ensure_segment(1);
    //take reference out of tran
    auto block0 = tmngr1->readonly_block(test_addr_seg0, 1);
    try
    { //check correctness of invalid segment processing
        tmngr1->to_far(1, block0.pos());  
        tresult.fail("Exception must be raised");
    }
    catch (const OP::trie::Exception& e)
    {
        tresult.assert_true(OP::trie::er_invalid_block== e.code());
    }
    auto block1_1 = tmngr1->readonly_block(test_addr_seg1, 1);
    auto block2_1 = tmngr1->readonly_block(test_addr2_seg1, 1);

    auto tran1 = tmngr1->begin_transaction();
    auto wr_block_seg0 = tmngr1->writable_block(test_addr_seg0, 1);
    auto ptr1 = wr_block_seg0.pos();
    tresult.assert_true(ptr1 != block0.pos(), "Transaction must allocate new memory space for write operation");
    auto far_addr1 = tmngr1->to_far(0, ptr1);
    auto far_addr_test = tmngr1->to_far(0, block0.pos());
    tresult.assert_that<equals>(far_addr1, far_addr_test, "Far address must be the same");
    auto ro_block_seg1 = tmngr1->readonly_block(test_addr_seg1, 1);
    //ro must force use optimistic-write strategy, 
    auto wr_block_seg1 = tmngr1->writable_block(test_addr_seg1, 1);
    tresult.assert_true(ro_block_seg1.pos() == wr_block_seg1.pos(), "Inside tran all blocks must be the same");
    tresult.assert_true(block1_1.pos() == wr_block_seg1.pos(), "Optimistic write must point the same memory as before transaction");

    auto wr_block2_seg1 = tmngr1->writable_block(test_addr2_seg1, 1);
    tresult.assert_true(block2_1.pos() != wr_block2_seg1.pos(), "Transaction must allocate new memory space for write operation");
    try
    { //before commit check correctness of invalid segment spec
        tmngr1->to_far(1, wr_block_seg0.pos());  
        tresult.fail("Exception must be raised");
    }
    catch (const OP::trie::Exception& e)
    {
        tresult.assert_true(OP::trie::er_invalid_block== e.code());
    }

    tran1->commit();
    
    try
    {
        tmngr1->to_far(0, wr_block_seg0.pos());
        tresult.fail("Exception must be raised");
    }
    catch (const OP::trie::Exception& e){
        tresult.assert_true(e.code() == OP::trie::er_invalid_block);
    }
}
!!!!!!!!!==>*/
void test_EvSrcSegmentGenericMemoryAlloc(OP::utest::TestRuntime &tresult)
{
    const char seg_file_name[] = "t-segementation.test";
    GenericMemoryTest::test_MemoryManager<EventSourcingSegmentManager>(seg_file_name, tresult);

}
void test_EvSrcSegmentManagerMultithreadMemoryAllocator(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "test Transacted Memory Allocation..." << std::endl;
    const char seg_file_name[] = "t-segementation.test";
    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    auto aa1 = tmngr1->segment_size();
    //tmngr1->ensure_segment(0);
    SegmentTopology<HeapManagerSlot> mngrToplogy (tmngr1);

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
    auto & mm = mngrToplogy.slot<HeapManagerSlot>();
    auto test_avail = mm.available(0);
    auto abc1_off = mm.make_new<TestAbc>(1, 1.01, "abc");
    auto abc1_block = tmngr1->readonly_block(abc1_off, sizeof(TestAbc));
    auto abc1_ptr = abc1_block.at<TestAbc>(0);
    tresult.assert_true(strcmp(abc1_ptr->c, "abc") == 0, "wrong assign");
    try{
        tmngr1->wr_at<TestAbc>(abc1_off);
        OP_UTEST_FAIL(<< "Exception OP::trie::er_transaction_not_started must be raised");
    }
    catch (OP::trie::Exception& e)
    {
        OP_UTEST_ASSERT(e.code() == OP::trie::er_transaction_not_started, << "must raise exception with code OP::trie::er_transaction_not_started");
    }
    mngrToplogy._check_integrity();
    
    OP::vtm::TransactionGuard transaction1(tmngr1->begin_transaction());
    auto abc2_off = mm.make_new<TestAbc>(1, 1.01, "abc");
    transaction1.commit();

    static const unsigned consumes[] = { 16, 16, 32, 113, 57, 320, 1025, 23157 };
    //ensure that thread-scenario works in single thread

    static const int test_threads = 2;
    std::atomic<int> synchro_count (0);
    std::condition_variable synchro_start;
    std::mutex synchro_lock;
    std::mutex m2;
    using lock_t = std::unique_lock< std::mutex >;
    auto intensiveConsumption = [&](){
        try
        {
            --synchro_count;
            std::ostringstream os; os << "Intensive #" << synchro_count;
            if (synchro_count)
            {
                lock_t g(synchro_lock);
                synchro_start.wait(g, [&synchro_count]{return synchro_count == 0; });
            }
            else
                synchro_start.notify_all();
            std::array<FarAddress, std::extent<decltype(consumes)>::value> managed_ptr{};
            for (auto i = 0; i < managed_ptr.max_size(); ++i)
            {
            OP::vtm::TransactionGuard transaction1(tmngr1->begin_transaction());
                lock_t g(m2);
                managed_ptr[i] = mm.allocate(consumes[i]);
            transaction1.commit();
            }
            //dealloc even
            for (auto i = 0; i < managed_ptr.max_size(); i += 2)
            {
            OP::vtm::TransactionGuard transaction2(tmngr1->begin_transaction());
                lock_t g(m2);
                mm.forcible_deallocate(managed_ptr[i]);
            transaction2.commit();
            }
            //dealloc odd
            for (auto i = 1; i < managed_ptr.max_size(); i += 2)
            {
            OP::vtm::TransactionGuard transaction3(tmngr1->begin_transaction());
                lock_t g(m2);
                mm.forcible_deallocate(managed_ptr[i]);
            transaction3.commit();
            }
        }
        catch (std::exception& e){
            std::cerr << "Not handled exception" << e.what()<<std::endl;
        }
        catch (...){
            std::cerr << "Not handled exception ?"<<std::endl;
        }
    };
    //ensure that thread-scenario works in single thread
/*    synchro_count = 1;
    std::thread thr1(intensiveConsumption);
    thr1.join();
    tmngr1->_check_integrity();
*/    
    synchro_count = test_threads;
    std::vector<std::thread> parallel_tests;
    for (auto i = 0; i < test_threads; ++i)
    {
        parallel_tests.emplace_back(intensiveConsumption);
    }
    for (auto i = 0; i < test_threads; ++i)
        parallel_tests[i].join();
    tmngr1->_check_integrity();
    tmngr1.reset();
}

void test_EvSrcMemmngrAllocDealloc(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "Reproduce issue with dealloc-alloc in single transaction..." << std::endl;
    const char seg_file_name[] = "t-segementation.test";
    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    auto aa1 = tmngr1->segment_size();
    //tmngr1->ensure_segment(0);
    SegmentTopology<HeapManagerSlot> mngrToplogy (tmngr1);

    auto & mm = mngrToplogy.slot<HeapManagerSlot>();
    auto tran1 = tmngr1->begin_transaction();
    auto block1 = mm.allocate(50);
    tran1->commit();
    auto test_avail = mm.available(0);
    
    // do it twice to test that issue is not disappeared during rollback
    for (auto i = 0; i < 2; ++i)
    {
        auto tran2 = tmngr1->begin_transaction();
        mm.forcible_deallocate(block1);
        mm.allocate(50);  // overlapped exception there

        tran2->rollback();
    }
    //
    tresult.assert_true(test_avail == mm.available(0));
    //std::cout <<"0x"<<std::setbase(16) << mm.available(0) << '\n';
    auto tran3 = tmngr1->begin_transaction();
    mm.deallocate(block1);
    mm.allocate(50);  //no overlapped exception there
    tran3->commit();
    //following 16 bytes are consumed by allocation new MemoryHeader, so include this to calc
    tresult.assert_true((test_avail- aligned_sizeof<MemoryBlockHeader>(SegmentDef::align_c)) == mm.available(0));
    
    tmngr1->_check_integrity();
    tmngr1.reset();
}
void test_EvSrcReleaseReadBlock(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "Generic positive tests...\n";
    const char seg_file_name[] = "t-segementation.test";
    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    tmngr1->ensure_segment(0);
    OP_CONSTEXPR(static const) segment_pos_t read_block_pos = 0x50;
    OP_CONSTEXPR(static const) segment_pos_t write_block_pos = 0x100;
    OP_CONSTEXPR(static const) std::uint8_t test_byte = 0x57;
    //make some write
    auto tran1 = tmngr1->begin_transaction();
    auto wr1 = tmngr1->writable_block(FarAddress(0, read_block_pos), write_block_pos - read_block_pos);
    memset(wr1.pos(), test_byte, wr1.count());
    auto wr2 = tmngr1->writable_block(FarAddress(0, write_block_pos), sizeof(write_fill_seq1));
    memcpy(wr2.pos(), write_fill_seq1, sizeof(write_fill_seq1));
    tran1->commit();
    auto tran2 = tmngr1->begin_transaction();
    OP_CONSTEXPR(static const) segment_pos_t wide_write_block_size = sizeof(write_fill_seq1) + write_block_pos - read_block_pos;
    //now create read-only block, that released afterall
    if (1 == 1)
    {
        auto ro1t2 = 
            tmngr1->readonly_block(FarAddress(0, read_block_pos), wide_write_block_size); //big overlapped chunk
        //test we have ro available for all
        auto future1 = std::async(std::launch::async, [&]() -> bool {
            auto r1_no_tran = tmngr1->readonly_block(FarAddress(0, read_block_pos), wide_write_block_size); 
            for (auto i = 0u; i < (write_block_pos - read_block_pos); ++i)
                if (test_byte != r1_no_tran.pos()[i])
                    return false;
            return 0 == memcmp(r1_no_tran.pos()+write_block_pos - read_block_pos, write_fill_seq1, sizeof(write_fill_seq1));
        });
        OP_UTEST_ASSERT(future1.get(), << "Wrong data at tested RO block");
        //now ro1t2 must be released, but t2 is still open
    }
    //after previous block is closed, it should be possible to open write block in another transaction
    auto future2 = std::async(std::launch::async, [&]() -> bool {
        auto tran3 = tmngr1->begin_transaction();
        auto w1t3 = tmngr1->writable_block(FarAddress(0, read_block_pos), wide_write_block_size); 
        //it's work! No exception there

        for (auto i = 0u; i < (write_block_pos - read_block_pos); ++i)
            if (test_byte != w1t3.pos()[i])
                return false;
        if (0 != memcmp(w1t3.pos() + write_block_pos-read_block_pos, write_fill_seq1, sizeof(write_fill_seq1)))
            return false;
        //write some new data 
        memcpy(w1t3.pos(), write_fill_seq2, sizeof(write_fill_seq2));
        tran3->commit();
        return true;
    });
    OP_UTEST_ASSERT(future2.get(), << "Wrong data in writable block");
    //check that current transaction sees changes
    auto ro1t2 = 
        tmngr1->readonly_block(FarAddress(0, read_block_pos), wide_write_block_size); //big overlapped chunk
    tresult.assert_true(
        tools::range_equals(
            ro1t2.pos(), ro1t2.pos()+ sizeof(write_fill_seq2), 
            write_fill_seq2, write_fill_seq2 + sizeof(write_fill_seq2)), "Wrong data read for separate transaction");
    tran2->commit();
    //
    // Retain behaviour
    //
    tresult.info() << "Test RO block with retain behaviour...\n";
    auto tran3 = tmngr1->begin_transaction();
    if (1 == 1)
    {   //this block will cause destroy, but not release
        ReadonlyMemoryChunk ro_keep_lock = 
            tmngr1->readonly_block(FarAddress(0, read_block_pos), 10, ReadonlyBlockHint::ro_keep_lock);

    }
    //after block destroyed, start another-thread-transaction to try capture
    auto future3 = std::async(std::launch::async, [&]() -> bool {
        auto tran4 = tmngr1->begin_transaction();
        try
        {
            tmngr1->writable_block(FarAddress(0, read_block_pos), 10);
            //exception must be raised!
            tran4->rollback();
            return false;
        }
        catch (const ConcurentLockException&)
        {
        }
        tran4->commit();
        return true;
    });
    tresult.assert_true(future3.get(), "Exception ConcurentLockException must be raised");
    tran3->commit();
    //
    //  Stacked block is deleted later than originated transaction
    //
    tresult.info() << "Test stacked RO block behaviour...\n";
    ReadonlyMemoryChunk stacked;
    if (1 == 1)
    {
        auto local_tran = tmngr1->begin_transaction();
        stacked = tmngr1->readonly_block(FarAddress(0, read_block_pos), 10); //now we have object that destoyes later than transaction
        local_tran->rollback();
        //no exceptions there
    }
}
void test_EvSrcNestedTransactions(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "Nesting transactions...\n";
    const char seg_file_name[] = "t-segementation.test";
    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    tmngr1->ensure_segment(0);
    OP_CONSTEXPR(static const) segment_pos_t write_block_len = 0x11;
    static_assert(3 * write_block_len < sizeof(write_fill_seq1), "please use block smaller than test data");
    static_assert(3 * write_block_len < sizeof(write_fill_seq2), "please use block smaller than test data");
    OP_CONSTEXPR(static const) segment_pos_t write_block_pos1 = 0x50;
    OP_CONSTEXPR(static const) segment_pos_t write_block_pos2 = 0x100;
    OP_CONSTEXPR(static const) std::uint8_t test_byte = 0x57;
    if (1 == 1)
    {
        //make some write
        auto tran1 = tmngr1->begin_transaction();
        auto wr1 = tmngr1->writable_block(FarAddress(0, write_block_pos1), write_block_len);
        memcpy(wr1.pos(), write_fill_seq1, write_block_len);
        if (1 == 1)
        {
            OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
            auto wr2 = tmngr1->writable_block(FarAddress(0, write_block_pos2), write_block_len);
            memcpy(wr2.pos(), write_fill_seq2, write_block_len);
            //check I see scope of 1st transaction
            auto t2_ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len);
            tresult.assert_true(tools::range_equals(
                t2_ro1.pos(), t2_ro1.pos()+ write_block_len,
                write_fill_seq1, write_fill_seq1+write_block_len), OP_CODE_DETAILS(<< "RO block inside transaction must point the same memory"));
            g.commit();
        }
        //check that T1 sees inner changes
        auto t1_r1 = tmngr1->readonly_block(FarAddress(0, write_block_pos2), write_block_len);
        tresult.assert_true(tools::range_equals(
            t1_r1.pos(), t1_r1.pos()+ write_block_len,
            write_fill_seq2, write_fill_seq2+write_block_len), OP_CODE_DETAILS(<< "RO block outside transaction must point the same memory"));
        tran1->commit();
        auto case1_r = std::async(std::launch::async, [&](){ //start thread to see changes are visible
            auto tran2 = tmngr1->begin_transaction();
            auto tst_ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len);
            tresult.assert_true(tools::range_equals(
                tst_ro1.pos(), tst_ro1.pos()+ write_block_len,
                write_fill_seq1, write_fill_seq1+write_block_len), OP_CODE_DETAILS(<< "RO block inside transaction must point the same memory"));
            auto tst_ro2 = tmngr1->readonly_block(FarAddress(0, write_block_pos2), write_block_len);
            tresult.assert_true(tools::range_equals(
                tst_ro2.pos(), tst_ro2.pos()+ write_block_len,
                write_fill_seq2, write_fill_seq2+write_block_len),
                OP_CODE_DETAILS(<< "RO block outside transaction must point the same memory"));
            tran2->rollback();
            return true;
        });
        tresult.assert_true(case1_r.get());
    }
    //....:::::....................:::::....
    tresult.info() << "Nesting transactions roll-back...\n";
    if (1 == 1)
    {
        auto t2_write_block1 = write_fill_seq1 + write_block_len;
        auto t2_write_block2 = write_fill_seq2 + write_block_len;
        auto tran2 = tmngr1->begin_transaction();
        auto case2_wr1 = tmngr1->writable_block(FarAddress(0, write_block_pos1), write_block_len);
        memcpy(case2_wr1.pos(), t2_write_block1, write_block_len);
        if (1 == 1)
        {
            OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
            auto case_wr2 = tmngr1->writable_block(FarAddress(0, write_block_pos2), write_block_len);
            memcpy(case_wr2.pos(), t2_write_block2, write_block_len);
            //check I see scope of 1st transaction
            auto t2_ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len);
            tresult.assert_true(
                tools::range_equals(t2_ro1.pos(), t2_ro1.pos()+write_block_len, t2_write_block1, t2_write_block1+write_block_len),
                OP_CODE_DETAILS(<< "RO block inside transaction must point the same memory"));
            g.rollback();
        }

        //test that same transaction doesn't see changes
        auto case2_ro2 = tmngr1->readonly_block(FarAddress(0, write_block_pos2), write_block_len);
        tresult.assert_true(tools::range_equals(
            case2_ro2.pos(), case2_ro2.pos()+ write_block_len, 
            write_fill_seq2, write_fill_seq2+write_block_len),
            OP_CODE_DETAILS(<< "RO block outside transaction must contain old data"));

        tran2->commit();
        auto case2_r = std::async(std::launch::async, [&](){ //start thread to see changes are visible
            auto tran2 = tmngr1->begin_transaction();
            auto case2_tst_ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len);
            tresult.assert_true(
                tools::range_equals(
                    case2_tst_ro1.pos(), case2_tst_ro1.pos()+ write_block_len,
                    t2_write_block1, t2_write_block1+write_block_len), OP_CODE_DETAILS(<< "RO block after commit must contain valid sequence"));
            auto case2_tst_ro2 = tmngr1->readonly_block(FarAddress(0, write_block_pos2), write_block_len);
            tresult.assert_true(
                tools::range_equals(
                    case2_tst_ro2.pos(), case2_tst_ro2.pos()+write_block_len,
                    write_fill_seq2, write_fill_seq2+write_block_len),
                OP_CODE_DETAILS(<< "RO block outside transaction must contain old data"));
            tran2->rollback();
            return true;
        });
        tresult.assert_true(case2_r.get());
    }
    //....:::::....................:::::....
    tresult.info() << "Framing transactions roll-back...\n";
    auto t3_write_block1 = write_fill_seq1 + 2*write_block_len;
    auto t3_write_block2 = write_fill_seq2 + 2*write_block_len;
    if (1 == 1)
    { //prepare information
        auto tranClean = tmngr1->begin_transaction();
        auto wrclean = tmngr1->writable_block(FarAddress(0, write_block_pos1), write_block_len);
        memcpy(wrclean.pos(), write_fill_seq1, write_block_len);
        auto wrclean2 = tmngr1->writable_block(FarAddress(0, write_block_pos2), write_block_len);
        memcpy(wrclean2.pos(), write_fill_seq2, write_block_len);
        tranClean->commit();
        //close & reopen segment
        tmngr1.reset();
        tmngr1 = OP::trie::SegmentManager::open<EventSourcingSegmentManager>(seg_file_name);
        tmngr1->ensure_segment(0);
    }
    if (1 == 1)
    {
        auto tran3 = tmngr1->begin_transaction();
        auto case3_wr1 = tmngr1->writable_block(FarAddress(0, write_block_pos1), write_block_len);
        memcpy(case3_wr1.pos(), t3_write_block1, write_block_len);
        if (1 == 1)
        { //nested transaction emulation
            OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
            auto case_wr2 = tmngr1->writable_block(FarAddress(0, write_block_pos2), write_block_len);
            memcpy(case_wr2.pos(), t3_write_block2, write_block_len);
            //check I see scope of 1st transaction
            auto t3_ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len);
            tresult.assert_true(tools::range_equals(
                t3_ro1.pos(), t3_ro1.pos()+ write_block_len,
                t3_write_block1, t3_write_block1+write_block_len),
                OP_CODE_DETAILS(<< "RO block inside transaction must point the same memory"));
            g.commit();
        }

        //test that same transaction sees changes
        auto case3_ro2 = tmngr1->readonly_block(FarAddress(0, write_block_pos2), write_block_len);
        tresult.assert_true(tools::range_equals(
            case3_ro2.pos(), case3_ro2.pos() + write_block_len,
            t3_write_block2, t3_write_block2+write_block_len),
            OP_CODE_DETAILS(<< "RO block outside transaction must contain old data"));

        tran3->rollback();
        //now check that all blocks are clean
        auto case3_r = std::async(std::launch::async, [&](){ //start thread to see no changes
            auto tran2 = tmngr1->begin_transaction();
            auto case3_tst_ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len);
            tresult.assert_true(tools::range_equals(
                case3_tst_ro1.pos(), case3_tst_ro1.pos() + write_block_len,
                write_fill_seq1, write_fill_seq1+write_block_len), 
                OP_CODE_DETAILS(<< "RO block after commit must contain valid sequence"));
            auto case3_tst_ro2 = tmngr1->readonly_block(FarAddress(0, write_block_pos2), write_block_len);
            tresult.assert_true(tools::range_equals(
                case3_tst_ro2.pos(), case3_tst_ro2.pos()+write_block_len,
                write_fill_seq2, write_fill_seq2+write_block_len),
                OP_CODE_DETAILS(<< "RO block outside transaction must contain old data"));
            tran2->rollback();
            return true;
        });
        tresult.assert_true(case3_r.get());
    }
}
void test_EvSrcBlockInclude(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "test Transacted Segment Manager..." << std::endl;

    const char seg_file_name[] = "t-segementation.test";

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(1024));
    tmngr1->ensure_segment(0);
    std::fstream fdata_acc(seg_file_name, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    tresult.assert_true(fdata_acc.good());

    //read out of tran must be permitted
    OP::vtm::TransactionGuard g(tmngr1->begin_transaction());

    auto outer_block = tmngr1->writable_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq1));
    outer_block.byte_copy(write_fill_seq1, sizeof(write_fill_seq1));
    constexpr segment_pos_t inner_offset = segment_pos_t{ sizeof(write_fill_seq1) / 2 };
    constexpr segment_pos_t inner_size = sizeof(write_fill_seq2) / 4;
    auto inner_block = tmngr1->writable_block(FarAddress(writable_data_fpos) + inner_offset, inner_size);
    inner_block.byte_copy(write_fill_seq2, inner_size);
    auto do_test_overlap = [&](){
        using namespace OP::utest;
        const segment_pos_t effective_size = sizeof(write_fill_seq1) - inner_offset - inner_size;
        auto block1 = tmngr1->readonly_block(FarAddress(writable_data_fpos), inner_offset);
        auto block2 = tmngr1->readonly_block(FarAddress(writable_data_fpos)+inner_offset, inner_size);
        auto block3 = tmngr1->readonly_block(FarAddress(writable_data_fpos)+inner_offset+inner_size, effective_size);
        tresult.assert_true(tools::range_equals(
            block1.pos(), block1.pos()+inner_offset, write_fill_seq1, write_fill_seq1+inner_offset),
            OP_CODE_DETAILS(<< "Invalid overlapped data #1"));
        tresult.assert_true(tools::range_equals(
            block2.pos(), block2.pos()+ inner_size, write_fill_seq2, write_fill_seq2+inner_size),
            OP_CODE_DETAILS(<< "Invalid overlapped data #2"));
        auto strain_data_begin = write_fill_seq1 + inner_offset + inner_size;
        tresult.assert_true(tools::range_equals(
            block3.pos(), block3.pos()+ effective_size, strain_data_begin, strain_data_begin+effective_size),
            OP_CODE_DETAILS(<< "Invalid overlapped data #3"));
    };
    do_test_overlap();
    g.commit();
    do_test_overlap();
}

void test_EvSrcBlockIncludeOnRead(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "test Transacted Segment Manager..." << std::endl;

    const char seg_file_name[] = "t-segementation.test";

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(1024));
    tmngr1->ensure_segment(0);
    std::fstream fdata_acc(seg_file_name, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    tresult.assert_true(fdata_acc.good());

    //read out of tran must be permitted
    OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
    auto outer_block = tmngr1->writable_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq1));
    outer_block.byte_copy(write_fill_seq1, sizeof(write_fill_seq1));
    g.commit();

    OP::vtm::TransactionGuard g1(tmngr1->begin_transaction());
    auto block1 = tmngr1->readonly_block(FarAddress(writable_data_fpos), 2);
    auto block2 = tmngr1->readonly_block(FarAddress(writable_data_fpos), sizeof(write_fill_seq1));
    tresult.assert_true(tools::range_equals(
        block2.pos(), block2.pos() + sizeof(write_fill_seq1), write_fill_seq1, write_fill_seq1 + sizeof(write_fill_seq1)),
        OP_CODE_DETAILS(<< "Invalid overlapped data #2"));
    g1.commit();

}
void test_EvSrcBlockOverlapOnRead(OP::utest::TestRuntime &tresult)
{
    tresult.info() << "Overalapped block inside read-only transactions..." << std::endl;

    const char seg_file_name[] = "t-segementation.test";

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    tmngr1->ensure_segment(0);
    constexpr segment_pos_t write_block_pos1 = 0;
    constexpr segment_pos_t write_block_len1 = sizeof(write_fill_seq1);
    constexpr segment_pos_t write_block_pos2 = write_block_pos1 + write_block_len1;
    constexpr segment_pos_t write_block_len2 = sizeof(write_fill_seq2);

    auto test_total_sequence = [&]() {
        auto ro_block = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len1 + write_block_len2);
        tresult.assert_that< logical_not<less> >(write_block_len1 + write_block_len2, ro_block.count(),
            OP_CODE_DETAILS(<< "Block is unexpected size"));
        tresult.assert_that<equals>(
            0, 
            memcmp(ro_block.pos(), write_fill_seq1, write_block_len1),
            OP_CODE_DETAILS(<< "Block comparison failed #1"));
        tresult.assert_that<equals>(
            0,
            memcmp(ro_block.pos()+ write_block_len1, write_fill_seq2, write_block_len2),
            OP_CODE_DETAILS(<< "Block comparison failed #2"));
    };
    if (1 == 1)
    {
        OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
        auto wr1 = tmngr1->writable_block(FarAddress(0, write_block_pos1), write_block_len1);
        memcpy(wr1.pos(), write_fill_seq1, write_block_len1);

        auto wr2 = tmngr1->writable_block(FarAddress(0, write_block_pos2), write_block_len2);
        memcpy(wr2.pos(), write_fill_seq2, write_block_len2);

        g.commit();
    }
    //just check that everything works till now
    test_total_sequence();
    //create RO locks over some stripes
    [&](){
        tresult.info() << "\tTest creation of big RO block that covers many small RO block" << std::endl;
        OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
        std::vector<ReadonlyMemoryChunk> retain_array;
        constexpr segment_pos_t stripe_size = 16;
        for (segment_pos_t i = 0; i < write_block_len1; i += 2 * stripe_size)
        {
            auto ro1 = tmngr1->readonly_block(FarAddress(0, write_block_pos1+i), stripe_size);
            retain_array.emplace_back(std::move(ro1));
        }
        //and now all stripes should be covered by 1 block

        auto ro_block = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len1 + write_block_len2);
        tresult.assert_that< logical_not<less> >(write_block_len1 + write_block_len2, ro_block.count(),
            OP_CODE_DETAILS(<< "Block is unexpected size"));

        tresult.assert_true(tools::range_equals(
            ro_block.pos(), ro_block.pos() + sizeof(write_fill_seq1), write_fill_seq1, write_fill_seq1 + sizeof(write_fill_seq1)),
            OP_CODE_DETAILS(<< "Invalid overlapped data #1"));
        tresult.assert_true(tools::range_equals(
            ro_block.pos() + +sizeof(write_fill_seq1), ro_block.pos() + sizeof(write_fill_seq1) + sizeof(write_fill_seq2), 
            write_fill_seq2, write_fill_seq2 + sizeof(write_fill_seq2)),
            OP_CODE_DETAILS(<< "Invalid overlapped data #2"));

        g.commit();

    }();

    std::vector<std::uint8_t> wr_block1_clone{ std::begin(write_fill_seq1), std::end(write_fill_seq1) };
    [&]() {
        tresult.info() << "\tTest creation of big RO block that covers many small WR block (on the same tran)" << std::endl;
        OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
        std::vector<MemoryChunk> retain_array;
        constexpr segment_pos_t stripe_size = 16;
        std::array<std::uint8_t, stripe_size> override_sample;
        std::iota(override_sample.begin(), override_sample.end(), stripe_size);

        for (segment_pos_t i = 0; i < write_block_len1; i += 2 * stripe_size)
        {
            auto wr1 = tmngr1->writable_block(FarAddress(0, write_block_pos1 + i), stripe_size);
            //fill with sample value
            wr1.byte_copy(override_sample.data(), static_cast<segment_pos_t>(override_sample.size()));
            std::copy(override_sample.begin(), override_sample.end(), wr_block1_clone.begin()+i);
            //retain block for the late
            retain_array.emplace_back(std::move(wr1));
        }
        //and now all stripes should be covered by 1 block

        auto ro_block = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len1 + write_block_len2);
        tresult.assert_that< logical_not<less> >(write_block_len1 + write_block_len2, ro_block.count(),
            OP_CODE_DETAILS(<< "Block is unexpected size"));

        tresult.assert_true(tools::range_equals(
            ro_block.pos(), ro_block.pos() + sizeof(write_fill_seq1), 
            wr_block1_clone.begin(), wr_block1_clone.end()),
            OP_CODE_DETAILS(<< "Invalid RO/WR overlapped data #1"));

        tresult.assert_true(tools::range_equals(
            ro_block.pos() + +sizeof(write_fill_seq1), ro_block.pos() + sizeof(write_fill_seq1) + sizeof(write_fill_seq2),
            write_fill_seq2, write_fill_seq2 + sizeof(write_fill_seq2)),
            OP_CODE_DETAILS(<< "Invalid RO overlapped data #2"));

        g.commit();
    }();
    //check the same afetr commit
    auto ro_block = tmngr1->readonly_block(FarAddress(0, write_block_pos1), write_block_len1 + write_block_len2);
    tresult.assert_true(tools::range_equals(
        ro_block.pos(), ro_block.pos() + sizeof(write_fill_seq1),
        wr_block1_clone.begin(), wr_block1_clone.end()),
        OP_CODE_DETAILS(<< "Invalid RO/WR overlapped data (after commit)"));

}

//void test_EvSrcMemoryReuseFail(OP::utest::TestRuntime &tresult)
//{
//    tresult.info() << "test Transacted Segment Manager..." << std::endl;
//
//    const char seg_file_name[] = "t-segementation.test";
//
//    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(seg_file_name,
//        OP::trie::SegmentOptions()
//        .segment_size(0x110000));
//
//    SegmentTopology<HeapManagerSlot> mngrToplogy(tmngr1);
//
//
//    auto & mm = mngrToplogy.slot<HeapManagerSlot>();
//    OP::vtm::TransactionGuard g(tmngr1->begin_transaction());
//
//    std::vector<std::tuple<FarAddress, unsigned>> dealloc;
//
//    for (unsigned i = 0, grow_size = 0x10; i < 0x10; ++i, grow_size <<= 1)
//    {
//        
//        for (unsigned j = 0; j < 0x40; ++j)
//        {
//            auto block = mm.allocate(grow_size);
//            dealloc.emplace_back(std::make_tuple(block, grow_size));
//        }
//    }
//    g.commit();
//    OP::vtm::TransactionGuard g1(tmngr1->begin_transaction());
//
//    //dealloc in the same transaction
//    for (unsigned x = 0; x < dealloc.size(); ++x) {
//        const auto &p = dealloc[x];
//        const auto block = std::get<FarAddress>(p);
//        const auto grow_size = std::get<unsigned>(p);
//        tmngr1->readonly_block(block, grow_size);
//        if (x & 1) {
//            tmngr1->writable_block(block + (grow_size - 1) / 2, 2);
//        }
//
//        mm.deallocate(block);
//    }
//    g1.commit();
//    //try {
//    //    tmngr1->wr_at<TestAbc>(abc1_off);
//    //    OP_UTEST_FAIL(<< "Exception OP::trie::er_transaction_not_started must be raised");
//    //}
//    //catch (OP::trie::Exception& e)
//    //{
//    //    OP_UTEST_ASSERT(e.code() == OP::trie::er_transaction_not_started, << "must raise exception with code OP::trie::er_transaction_not_started");
//    //}
//    mngrToplogy._check_integrity();
//
//}

//using std::placeholders;
static auto& module_suite = OP::utest::default_test_suite("EventSourcingSegmentManager")
.declare("general", test_EvSrcSegmentManager)
.declare("locking", test_Locking)
//.declare("far address conversion", test_FarAddress)
.declare("memoryAlloc", test_EvSrcSegmentGenericMemoryAlloc)
.declare("multithreadAlloc", test_EvSrcSegmentManagerMultithreadMemoryAllocator)
.declare("dealloc-alloc issue in single tran" , test_EvSrcMemmngrAllocDealloc)
.declare("release read block", test_EvSrcReleaseReadBlock)
.declare("nested transactions", test_EvSrcNestedTransactions)
.declare("test write-block include capability", test_EvSrcBlockInclude)
.declare("test read-block include capability", test_EvSrcBlockIncludeOnRead)
.declare("transaction on overlapped blocks", test_EvSrcBlockOverlapOnRead)
;