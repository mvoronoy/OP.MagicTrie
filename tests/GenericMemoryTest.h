#ifndef _GENERICMEMORYTEST__H_
#define _GENERICMEMORYTEST__H_


#include "unit_test.h"

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
        TestHead(OP::trie::NodeType ntype) :
            _ntype(ntype)
        {
        }
        OP::trie::NodeType _ntype;
        OP::trie::far_pos_t table_pos;
    };
    typedef NodeHashTable<EmptyPayload, 8> htbl64_t;
    typedef NodeSortedArray<EmptyPayload, 32> sarr32_t;
    
    std::uint8_t* one_byte_block = nullptr;
    if (1 == 1)
    {       
        result.info() << "Test create new...\n";
        auto options = OP::trie::SegmentOptions().segment_size(0x110000);
        auto mngr1 = Sm::create_new(seg_file_name, options);
        tst_size = mngr1->segment_size();
        SegmentTopology<MemoryManager> mngrTopology = SegmentTopology<MemoryManager>(mngr1);
        one_byte_block = mngrTopology.slot<MemoryManager>().allocate(1);
        mngr1->_check_integrity();
    }
    result.info() << "Test reopen existing...\n";
    auto segmentMngr2 = Sm::open(seg_file_name);
    assert(tst_size == segmentMngr2->segment_size());
    SegmentTopology<MemoryManager>& mngr2 = *new SegmentTopology<MemoryManager>(segmentMngr2);

    auto half_block = mngr2.slot<MemoryManager>().allocate(tst_size / 2);
    mngr2._check_integrity();
    //try consume bigger than available
    //try
    {
        mngr2.slot<MemoryManager>().allocate(mngr2.slot<MemoryManager>().available(0) + 1);
        //new segment must be allocated
        assert(segmentMngr2->available_segments() == 2);
    }
    //catch (const OP::trie::Exception& e)
    //{
    //    assert(e.code() == OP::trie::er_no_memory);
    //}
    mngr2._check_integrity();
    //consume allmost all
    auto rest = mngr2.slot<MemoryManager>().allocate( mngr2.slot<MemoryManager>().available(0) - 16);
    mngr2._check_integrity();
    try
    {
        mngr2.slot<MemoryManager>().deallocate(rest + 1);
        assert(false);//exception must be raised
    }
    catch (const OP::trie::Exception& e)
    {
        assert(e.code() == OP::trie::er_invalid_block);
    }
    mngr2._check_integrity();
    //allocate new segment and allocate memory and try to dealloc in other segment
    //mngr2.segment_manager().ensure_segment(1);
    //try
    //{
    //    mngr2.slot<MemoryManager>().deallocate((1ull<<32)| rest);
    //    assert(false);//exception must be raised
    //}
    //catch (const OP::trie::Exception& e)
    //{
    //    assert(e.code() == OP::trie::er_invalid_block);
    //}
    mngr2.slot<MemoryManager>().deallocate(one_byte_block);
    mngr2._check_integrity();
    mngr2.slot<MemoryManager>().deallocate(rest);
    mngr2._check_integrity();
    mngr2.slot<MemoryManager>().deallocate(half_block);
    mngr2._check_integrity();
    auto bl_control = mngr2.slot<MemoryManager>().allocate(100);
    auto test_size = mngr2.slot<MemoryManager>().available(0);
    //make striped blocks
    std::uint8_t* stripes[7];
    for (size_t i = 0; i < 7; ++i)
        stripes[i] = mngr2.slot<MemoryManager>().allocate(100);
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
        if (!has_block_compression)
            test_size -= aligned_sizeof<MemoryBlockHeader>(SegmentDef::align_c);
        mm.deallocate(stripes[i]);
        mngr3->_check_integrity();
    }
    assert(test_size == mm.available(0));
    //repeat prev test on condition of releasing even blocks
    for (size_t i = 0; i < 7; ++i)
        stripes[i] = mm.allocate(100);
    for (size_t i = 0; i < 7; i+=2)
        mm.deallocate(stripes[i]);
    mngr3->_check_integrity();
    for (size_t i = 1; i < 7; i += 2)
    {
        mm.deallocate(stripes[i]);
        mngr3->_check_integrity();
    }
    assert(test_size == mm.available(0));
    
    //make random test
    void* rand_buf[1000];
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
        mm.deallocate((std::uint8_t*) p );
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
    assert(_compare(htbl_sample, *htbl_addr_of4));

    sarr32_t* sarr_addr_of4 = mngr4->from_far<sarr32_t>(heads_of4[1].table_pos);
    assert(_compare(sarr_sample, *sarr_addr_of4));
    */

}
#endif //_GENERICMEMORYTEST__H_
