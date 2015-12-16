#ifndef _GENERICMEMORYTEST__H_
#define _GENERICMEMORYTEST__H_


#include "unit_test.h"
#include "unit_test_is.h"
using namespace OP::utest;

template <class Sm>
void test_MemoryManager(const char * seg_file_name, OP::utest::TestResult& result)
{
    result.info() << "test MemoryManager on" << typeid(Sm).name() << "..." << std::endl;
    std::uint32_t tst_size = -1;
    struct TestMemAlloc1
    {
        int a;
        char b[10];
    };
    struct TestMemAlloc2
    {
        int a;
        double x;
        double y[121];
    };
    struct TestHead
    {
        
        int _ntype;
        OP::trie::far_pos_t table_pos;
    };
    typedef NodeHashTable<EmptyPayload, 8> htbl64_t;
    typedef NodeSortedArray<EmptyPayload, 32> sarr32_t;
    
    const std::uint8_t* one_byte_block = nullptr;
    FarAddress one_byte_pos;
    if (1 == 1)
    {       
        result.info() << "Test create new...\n";
        auto options = OP::trie::SegmentOptions().segment_size(0x110000);
        auto mngr1 = Sm::create_new(seg_file_name, options);
        tst_size = mngr1->segment_size();
        SegmentTopology<MemoryManager> mngrTopology = SegmentTopology<MemoryManager>(mngr1);
        one_byte_pos = mngrTopology.slot<MemoryManager>().allocate(1);
        one_byte_block = mngr1->readonly_block(one_byte_pos, 1).at<std::uint8_t>(0);
        mngr1->_check_integrity();
    }
    result.info() << "Test reopen existing...\n";
    auto segmentMngr2 = Sm::open(seg_file_name);
    result.assert_true(tst_size == segmentMngr2->segment_size(), OP_CODE_DETAILS());
    SegmentTopology<MemoryManager>& mngr2 = *new SegmentTopology<MemoryManager>(segmentMngr2);

    auto half_block = mngr2.slot<MemoryManager>().allocate(tst_size / 2);
    mngr2._check_integrity();
    //try consume bigger than available
    //try
    {
        mngr2.slot<MemoryManager>().allocate(mngr2.slot<MemoryManager>().available(0) + 1);
        //new segment must be allocated
        result.assert_true(segmentMngr2->available_segments() == 2, OP_CODE_DETAILS());
    }
    //catch (const OP::trie::Exception& e)
    //{
    //    result.assert_true(e.code() == OP::trie::er_no_memory);
    //}
    mngr2._check_integrity();
    //consume allmost all
    auto rest = mngr2.slot<MemoryManager>().allocate( mngr2.slot<MemoryManager>().available(0) - SegmentDef::align_c );
    mngr2._check_integrity();
    try
    {
        mngr2.slot<MemoryManager>().deallocate(rest + 1);
        result.assert_true(false, OP_CODE_DETAILS(<<"exception must be raised"));
    }
    catch (const OP::trie::Exception& e)
    {
        result.assert_true(e.code() == OP::trie::er_invalid_block, OP_CODE_DETAILS());
    }
    mngr2._check_integrity();
    //allocate new segment and allocate memory and try to dealloc in other segment
    //mngr2.segment_manager().ensure_segment(1);
    //try
    //{
    //    mngr2.slot<MemoryManager>().deallocate((1ull<<32)| rest);
    //    result.assert_true(false);//exception must be raised
    //}
    //catch (const OP::trie::Exception& e)
    //{
    //    result.assert_true(e.code() == OP::trie::er_invalid_block);
    //}
    mngr2.slot<MemoryManager>().deallocate(one_byte_pos);
    mngr2._check_integrity();
    mngr2.slot<MemoryManager>().deallocate(rest);
    mngr2._check_integrity();
    mngr2.slot<MemoryManager>().deallocate(half_block);
    mngr2._check_integrity();
    auto bl_control = mngr2.slot<MemoryManager>().allocate(100);
    auto test_size = mngr2.slot<MemoryManager>().available(0);
    static const std::uint8_t seq[] = { 0x7A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
        0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x0B, 0x00, 0x00, 0x12, 0x0B, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0A, 0x1D, 0x16, 0x12, 0x1E, 0x1B, 0x12, 0x1F, 0x10,
        0x06, 0x16, 0x0F, 0x07, 0x18, 0x0E, 0x0A, 0x1D, 0x0C, 0x0C, 0x1E, 0x08, 0x09, 0x1E, 0x0A, 0x0A,
        0x28, 0x02, 0x01, 0x29, 0x07, 0x01, 0x36, 0x00, 0x00, 0x00, 0x15, 0x0E, 0x1B, 0x1F, 0x15, 0x1B,
        0x31, 0x23, 0x29, 0x38, 0x28, 0x2F, 0x36, 0x28, 0x2E, 0x31, 0x25, 0x2B, 0x30, 0x26, 0x2C, 0x2A,
        0x22, 0x2C, 0x27, 0x1F, 0x30, 0x21, 0x16, 0x30, 0x1F, 0x10, 0x36, 0x00, 0x00, 0x00, 0x2A, 0x1C,
        0x20, 0x39, 0x26, 0x23, 0x59, 0x43, 0x3D, 0x72, 0x58, 0x52, 0x71, 0x5A, 0x52, 0x65, 0x50, 0x48,
        0x64, 0x51, 0x49, 0x5F, 0x4D, 0x46, 0x56, 0x44, 0x45, 0x4F, 0x3C, 0x45, 0x45, 0x2E, 0x44, 0x00,
        0x00, 0x00, 0x5C, 0x47, 0x3F, 0x76, 0x5C, 0x4B, 0x90, 0x6F, 0x5B, 0x94, 0x72, 0x5B, 0x9C, 0x78,
        0x60, 0x93, 0x72, 0x58, 0x9C, 0x7E, 0x61, 0x9C, 0x81, 0x67, 0x92, 0x77, 0x63, 0x86, 0x68, 0x5D,
        0x6E, 0x4E, 0x4F, 0x00, 0x00, 0x00, 0x97, 0x79, 0x60, 0xA9, 0x86, 0x64, 0xB4, 0x8A, 0x65, 0xB0,
        0x86, 0x5C, 0xAB, 0x80, 0x55, 0xA7, 0x7E, 0x51, 0xB7, 0x91, 0x61, 0xBE, 0x9B, 0x6F, 0xBB, 0x97,
        0x71, 0xAF, 0x8B, 0x6D, 0x99, 0x71, 0x5F, 0x00, 0x00, 0x00, 0xB6, 0x94, 0x66, 0xB7, 0x90, 0x59,
        0xBA, 0x8C, 0x52, 0xC0, 0x8E, 0x52, 0xB1, 0x7E, 0x40, 0xAE, 0x7E, 0x3E, 0xBA, 0x8F, 0x4C, 0xC5,
        0x9C, 0x5D, 0xC8, 0xA0, 0x66, 0xC0, 0x97, 0x66, 0xB1, 0x85, 0x60, 0x00, 0x00, 0x00, 0xCC, 0xA5,
        0x61, 0xCC, 0xA0, 0x53, 0xC8, 0x97, 0x49, 0xC6, 0x91, 0x40, 0xBD, 0x88, 0x37, 0xBC, 0x8A, 0x37,
        0xC8, 0x9A, 0x46, 0xD1, 0xA6, 0x55, 0xD5, 0xAB, 0x60, 0xCA, 0xA0, 0x5F, 0xBD, 0x8F, 0x59, 0x00,
        0x00, 0x00, 0xDA, 0xAF, 0x52, 0xD3, 0xA5, 0x40, 0xD2, 0x9E, 0x3A, 0xD3, 0x9D, 0x38, 0xCD, 0x97,
        0x32, 0xCD, 0x9C, 0x36, 0xD7, 0xAA, 0x43, 0xE0, 0xB6, 0x53, 0xDB, 0xB2, 0x56, 0xCB, 0xA3, 0x51,
        0xC4, 0x98, 0x51, 0x00, 0x00, 0x00, 0xEB, 0xC0, 0x49, 0xE8, 0xB8, 0x3B, 0xE3, 0xB0, 0x36, 0xE3,
        0xAE, 0x35, 0xE4, 0xB0, 0x38, 0xE3, 0xB3, 0x3D, 0xE6, 0xBB, 0x44, 0xE9, 0xC0, 0x4C, 0xE2, 0xBB,
        0x4E, 0xD5, 0xAE, 0x4B, 0xCE, 0xA4, 0x4B, 0x00, 0x00, 0x00 };
    //make striped blocks
    FarAddress stripes[7];
    for (size_t i = 0; i < 7; ++i)
    {
        auto b_pos = mngr2.slot<MemoryManager>().allocate(sizeof(seq));
        auto b = segmentMngr2->writable_block(b_pos, sizeof(seq)).at<std::uint8_t>(0);
        memcpy(b, seq, sizeof(seq));
        stripes[i] = b_pos;
    }
    mngr2._check_integrity();
    //check closing and reopenning
    delete&mngr2;//mngr2.reset();//causes delete
    segmentMngr2.reset();

    auto segmentMngr3 = Sm::open(seg_file_name);
    SegmentTopology<MemoryManager>* mngr3 = new SegmentTopology<MemoryManager>(segmentMngr3);
    auto& mm = mngr3->slot<MemoryManager>();
    /**Flag must be set if memory management allows merging of free adjacent blocks*/
    const bool has_block_compression = mm.has_block_merging(); 
    mngr3->_check_integrity();
    
    //make each odd block free
    for (size_t i = 1; i < 7; i += 2)
    {
        if (!has_block_compression)
            test_size -= aligned_sizeof<MemoryBlockHeader>(SegmentDef::align_c);
        mm.deallocate(stripes[i]);
    }
    mngr3->_check_integrity();
    //now test merging of adjacency blocks
    for (size_t i = 0; i < 7; i += 2)
    {
        auto ro_ptr = segmentMngr3->readonly_block(stripes[i], sizeof(seq)).at<std::uint8_t>(0);
            
        result.assert_that(is::equals(seq, ro_ptr, sizeof(seq)), "striped block corrupted");

        if (!has_block_compression)
            test_size -= aligned_sizeof<MemoryBlockHeader>(SegmentDef::align_c);
        mm.deallocate(stripes[i]);
        mngr3->_check_integrity();
    }
    result.assert_true(test_size == mm.available(0), OP_CODE_DETAILS());
    //repeat prev test on condition of releasing even blocks
    for (size_t i = 0; i < 7; ++i)
        stripes[i] = mm.allocate(sizeof(seq));
    for (size_t i = 0; i < 7; i+=2)
        mm.deallocate(stripes[i]);
    mngr3->_check_integrity();
    for (size_t i = 1; i < 7; i += 2)
    {
        mm.deallocate(stripes[i]);
        mngr3->_check_integrity();
    }
    result.assert_true(test_size == mm.available(0), OP_CODE_DETAILS());
    
    //make random test
    FarAddress rand_buf[1000];
    size_t rnd_indexes[1000];//make unique vector and randomize access over it
    for (size_t i = 0; i < 1000; ++i)
    {
        rnd_indexes[i] = i;
        auto r = rand();
        if (r & 1)
            rand_buf[i] = mm.make_new<TestMemAlloc2>();
        else
            rand_buf[i] = mm.make_new<TestMemAlloc1>();
    }
    std::random_shuffle(std::begin(rnd_indexes), std::end(rnd_indexes));
    mngr3->_check_integrity();
    std::chrono::system_clock::time_point now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < 1000; ++i)
    {
        auto p = rand_buf[rnd_indexes[i]];
        mm.deallocate( p );
        mngr3->_check_integrity();
    }
    std::cout << "\tTook:" 
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now).count() 
        << "ms" << std::endl;
    mngr3->_check_integrity();
    delete mngr3;
    segmentMngr3.reset();
    
    //do more real example with NodeHead + conatiner
    /*far_pos_t heads_off = mm.make_array<TestHead>(0, 2, NodeType::hash_c);
    TestHead *heads = mngr3->from_far<TestHead>(heads_off);
    const std::string named_object = "heads";
    mngr3->put_named_object(0, named_object, heads_off);
    mngr3->_check_integrity();

    far_pos_t htbl_addr_off = mngr3->make_new<htbl64_t>(0);
    htbl64_t* htbl_addr = mngr3->from_far<htbl64_t>(htbl_addr_off);

    heads[0].table_pos = htbl_addr_off;
    std::set<std::uint8_t> htbl_sample = _randomize(*htbl_addr);
    mngr3->_check_integrity();

    far_pos_t sarr_addr_off = mngr3->make_new<sarr32_t>(0);
    sarr32_t* sarr_addr = mngr3->from_far<sarr32_t>(sarr_addr_off);
    mngr3->_check_integrity();

    heads[1].table_pos = sarr_addr_off;
    std::set<std::string> sarr_sample = _randomize_array(*sarr_addr, [&](){mngr3->_check_integrity(); });
    mngr3->_check_integrity();
    mngr3.reset();//causes delete
    
    auto mngr4 = Sm::open(seg_file_name);
    const TestHead* heads_of4 = mngr4->get_named_object<TestHead>(named_object);
    
    htbl64_t* htbl_addr_of4 = mngr4->from_far<htbl64_t>(heads_of4[0].table_pos);
    result.assert_true(_compare(htbl_sample, *htbl_addr_of4));

    sarr32_t* sarr_addr_of4 = mngr4->from_far<sarr32_t>(heads_of4[1].table_pos);
    result.assert_true(_compare(sarr_sample, *sarr_addr_of4));
    */

}
#endif //_GENERICMEMORYTEST__H_
