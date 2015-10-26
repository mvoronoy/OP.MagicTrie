    // trie.cpp : Defines the entry point for the console application.
//
#define _SCL_SECURE_NO_WARNINGS 1

#include <iostream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/MemoryManager.h>
#include <op/trie/CacheManager.h>
#include <op/trie/Containers.h>

#include <set>
#include <cstdint>
#include <cassert>
#include <iterator>
//#include <windows.h>

#include <ctime>
#include <chrono>

using namespace OP::trie;

template <node_size_t capacity_c>
struct TestHashTable : public NodeHashTable< EmptyPayload, capacity_c >
{
    typedef NodeHashTable < EmptyPayload, capacity_c > base_t;
    TestHashTable() 
    {}
    ~TestHashTable()
    {
    }
    
};

void test_NodeHash_insert(bool)
{
    std::cout << "test insert..." << std::endl;
    for (unsigned i = 0; i < 8; ++i)
    {
        TestHashTable<8> tbl;
        assert(i == tbl.insert(i)._debug());
        unsigned x = 0;
        for (unsigned j = 8; j < 16; ++j, ++x)
        {
            if (x == i)
                x += 1;
            if (j==15)
                assert(tbl.end() == tbl.insert(j));
            else
                assert(x == tbl.insert(j)._debug());
        }
    }
    std::cout << "\tpassed" << std::endl;
}
template <class Tbl>
std::set<std::uint8_t> _randomize(Tbl& tbl)
{
    std::set<std::uint8_t> result;
    while (tbl.size() < tbl.capacity())
    {
        std::uint8_t r = static_cast<std::uint8_t>(std::rand() & 0xFF);
        if (tbl.insert(r) != tbl.end())
            result.insert(r);
    }
    return result;
}
template <class Tbl, class F>
std::set<std::string> _randomize_array(Tbl& tbl, F && f)
{
    std::set<std::string> result;
    while (tbl.size() < tbl.capacity())
    {
        std::string s;
        auto l = (std::rand() % (Tbl::chunk_limit_c-1))+1;
        while (l--)
            s+=(static_cast<char>(std::rand() % ('_' - '0')) + '0');
        
        if (tbl.insert(s.begin(), s.end()) != tbl.end())
        {
            result.insert(s);
            f();
        }
    }
    return result;
}
template <class Tbl, class Sample>
bool _compare(const /*std::set<std::uint8_t>*/Sample& sample, Tbl& test)
{
    if (sample.size() != test.size())
        return false;
    for (auto a : sample)
        if (test.find(a) == test.end())
        {
            std::cout << "Not found: 0x" << std::setbase(16) << std::setw(2) << std::setfill('0') << /*(unsigned)*/a << std::endl;
            return false;
        }
    return true;
}
template <class It1, class It2>
bool _my_equal(It1 first1, It1 last1, It2 first2, It2 last2)
{
    for (; first1 != last1 && first2 != last2; ++first1, ++first2)
        if (*first1 != *first2)
            return false;
    return first1 == last1 && first2 == last2;
}
template <class T>
void _random_test(T &tbl)
{
    
    std::cout << "\nRandom erase with page=[" << tbl.capacity() << "]\n";
    const unsigned limit = 2u << 16;
    for (unsigned i = 0; i < limit; ++i)
    {
        if (!(i & (i - 1)))
            std::cout << '.';
        tbl.clear();
        auto sample = _randomize(tbl);
        while (!tbl.empty())
        {
            auto p = sample.begin();
            std::advance(p, rand() % sample.size());
            tbl.erase(*p);
            sample.erase(p);
            if (!_compare(sample, tbl))
            {
                std::cout << "Sample:\n";
                std::copy(sample.begin(), sample.end(),
                    std::ostream_iterator<int>(std::cout, ", "));
                std::cout << "\nTested:\n";
                std::copy(tbl.begin(), tbl.end(),
                    std::ostream_iterator<int>(std::cout, ", "));
                assert(false);
            }
        }
    }

}
void test_NodeHash_erase(bool allow_long_test)
{
    std::cout << "test erase..." << std::endl;
    TestHashTable<16> tbl;
    tbl.insert(0);
    assert(0 == tbl.erase(1));
    assert(1 == tbl.size());
    assert(1 == tbl.erase(0));
    assert(0 == tbl.size());
    assert(0 == tbl.insert(0)._debug());
    //render items with the same hashcode
    assert(1 == tbl.insert(16)._debug());
    assert(2 == tbl.insert(32)._debug());
    assert(tbl.end() == tbl.insert(48));
    assert(1 == tbl.erase(32));
    assert(2 == tbl.insert(48)._debug());
    assert(1 == tbl.erase(16));
    assert(1 == tbl.find(48)._debug());
    assert(2 == tbl.insert(64)._debug());

    assert(1 == tbl.erase(0));
    assert(tbl.end() == tbl.find(0));
    assert(0 == tbl.find(48)._debug());
    assert(1 == tbl.find(64)._debug());//shift must affect last one
    assert(2 == tbl.insert(0)._debug());
    //test erase overflow
    assert(15 == tbl.insert(15)._debug());
    assert(tbl.end() == tbl.insert(31)); //since items at 0, 1 already populated
    assert(1 == tbl.erase(64));
    assert(1 == tbl.erase(48));
    assert(1 == tbl.erase(0));
    assert(0 == tbl.insert(31)._debug()); //since items just released, so insert is success
    assert(1 == tbl.erase(15));
    assert(15 == tbl.find(31)._debug());//shift must affect last one
    assert(0 == tbl.insert(0)._debug());
    if (1 == 1)
    {
        TestHashTable<16> tbl;
        tbl.insert(0);
        assert(1 == tbl.insert(16)._debug()); //force place
        assert(2 == tbl.insert(1)._debug()); //force place
        assert(3 == tbl.insert(17)._debug()); //force place
        assert(1 == tbl.erase(0));
        assert(tbl.size() == 3);
        auto p = tbl.begin();
        assert(16 == *p++);
        assert(1 == *p++);
        assert(17 == *p++);
    }
    if (1 == 1)
    {
        TestHashTable<16> tbl;
        tbl.insert(0);
        assert(0 == tbl.insert(0)._debug()); //force place
        assert(1 == tbl.insert(1)._debug()); //force place
        assert(2 == tbl.insert(16)._debug()); //force place
        assert(1 == tbl.erase(1));
        assert(tbl.size() == 2);
        auto p = tbl.begin();
        assert(0 == *p++);
        assert(16 == *p++);
        tbl.erase(0);
        tbl.erase(16);
        //erase test on range cases
        assert(14 == tbl.insert(0xe)._debug()); 
        assert(15 == tbl.insert(0xf)._debug()); 
        assert(0 == tbl.insert(0x1e)._debug()); 
        assert(1 == tbl.erase(0xf));
        assert(tbl.size() == 2);
        p = tbl.begin();
        assert(14 == *p++);
        assert(0x1e == *p++);
        tbl.erase(14);
        tbl.erase(0x1e);
        //
        assert(14 == tbl.insert(0xe)._debug()); 
        assert(15 == tbl.insert(0xf)._debug()); 
        assert(0 == tbl.insert(0x1e)._debug()); 
        assert(1 == tbl.insert(0)._debug()); 
        assert(1 == tbl.erase(0xe));
        assert(tbl.size() == 3);
        p = tbl.begin();
        assert(0 == *p++);
        assert(0x1e == *p++);
        assert(0xf == *p++);

    }
    if (allow_long_test)
    {
         TestHashTable<8> tbl8;
        _random_test(tbl8);
        TestHashTable<16> tbl16;
        _random_test(tbl16);
        TestHashTable<32> tbl32;
        _random_test(tbl32);
        TestHashTable<64> tbl64;
        _random_test(tbl64);
        TestHashTable<128> tbl128;
        _random_test(tbl128);
        TestHashTable<256> tbl256;
        _random_test(tbl256);
    }
    std::cout << "\tpassed" << std::endl;
}


struct TestSortedArray : public NodeSortedArray < EmptyPayload, 16 >
{
    typedef NodeSortedArray < EmptyPayload, 16 > base_t;
    TestSortedArray() 
    {}
    ~TestSortedArray()
    {
    }
};
void test_NodeArray_insert(bool allow_long_test)
{
    std::cout << "test sorted array insert..." << std::endl;
    
    TestSortedArray tbl;
    assert(tbl.end() == tbl.insert(std::string("")));
    const atom_t a0[] = "a";
    const atom_t a1[] = "ab";
    const atom_t a2[] = "abc";
    const atom_t a3[] = "ac";
    auto p = std::begin(a1);
    auto tst_first = tbl.insert(p, std::end(a1));
    assert(0 == tst_first._debug());
    assert(1 == tbl.size());
    //insert the same second time
    p = std::begin(a1);
    assert( tst_first == tbl.insert(p, std::end(a1)) );

    p = std::begin(a3);
    auto tst_3 = tbl.insert(p, std::end(a3));
    assert(1 == tst_3._debug());

    p = std::begin(a2);
    auto tst_2 = tbl.insert(p, std::end(a2));
    assert(1 == tst_2._debug());
    assert(0 == tbl.find(std::begin(a1), std::end(a1))._debug());
    p = std::begin(a0);
    auto count_before_test = tbl.size();
    auto tst_0 = tbl.insert(p, p+1/*std::end(a0)*/);
    assert(0 == tst_0._debug());
    assert(count_before_test + 1 == tbl.size());
    // Test substring-length control
    const atom_t a08[] = "012345678";
    const atom_t a07[] = "01234567";

    p = std::begin(a08);
    auto tst_08 = tbl.insert(p, std::end(a08));
    assert('8' == *p);
    assert(0 == tst_08._debug());
    p = std::begin(a07);
    assert(tst_08 == tbl.insert(p, std::end(a07)));
    const atom_t templ[7] = "x";
    const atom_t *templ_end = templ + 1 + sizeof(short);
    for (unsigned short i = 0; tbl.size() < tbl.capacity(); ++i)
    {
        *((short*)(templ + 1)) = i;
        auto n = tbl.size();
        auto inspos = tbl.insert(p = templ, templ_end);
        auto v = *inspos;
        assert(_my_equal(templ, templ_end, v.first, v.second));
    }
    const atom_t ustr[] = "abcde";
    p = std::begin(ustr);
    auto fpos = tbl.best_match(p, std::end(ustr));
    assert(tbl.find(p = std::begin(a2), std::end(a2)) == fpos);
    
    if (1 == 1)
    {
        //NodeSortedArray<16, EmptyPayload> tbl;
        tbl.clear();
        tbl.insert(std::string("abc"));
        tbl.insert(std::string("ac"));
        const std::string t1("a");
        std::string::const_iterator p = t1.begin();
        auto ins = tbl.best_match(p, t1.end());
        assert(ins == tbl.begin());

        const std::string t2("b");
        p = t2.begin();
        assert(tbl.end() == tbl.best_match(p, t2.end()));
        const std::string t3("abcde");
        p = t3.begin();
        ins = tbl.best_match(p, t3.end());
        assert(ins == tbl.begin());
        assert(*p == 'd');
        assert(*++p == 'e');
    }
    //
    //------
    if (allow_long_test)
    {
        TestHashTable<64> tbl64;
        _random_test(tbl64);
    }
}
void test_align()
{
    std::cout << "test alignment routines..." << std::endl;

    assert(10 == OP::trie::align_on(3, 10));
    assert(20 == OP::trie::align_on(11, 10));
    assert(0 == OP::trie::align_on(0, 10));
    assert(10 == OP::trie::align_on(10, 10));
    //check for 
    assert(17 == OP::trie::align_on(16, 17));
    assert(17 == OP::trie::align_on(3, 17));
    assert(0 == OP::trie::align_on(0, 17));
    assert(34 == OP::trie::align_on(18, 17));
}
void test_SegmentManager()
{
    std::cout << "test virtual memory Segment Manager..." << std::endl;

    const char seg_file_name[] = "segementation.test";
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
    typedef std::shared_ptr<SegmentManager> segments_ptr_t;
    std::uint8_t* one_byte_block = nullptr;
    if (1 == 1)
    {       
        auto options = OP::trie::SegmentOptions()
            .heuristic_size(
            size_heuristic::of_array<TestMemAlloc1, 100>,
            size_heuristic::of_array<TestMemAlloc1, 900>,
            size_heuristic::of_array<TestMemAlloc2, 1000>,
            size_heuristic::of_array<TestHead, 3>,
            size_heuristic::of_assorted<htbl64_t, 1>,
            size_heuristic::of_assorted<sarr32_t, 1>
            , size_heuristic::add_percentage(5)/*+5% of total size*/
            );
        auto mngr1 = OP::trie::SegmentManager::create_new(seg_file_name, options);
        tst_size = mngr1->segment_size();
        SegmentTopology<MemoryManager> mngrTopology = SegmentTopology<MemoryManager>(mngr1);
        one_byte_block = mngrTopology.slot<MemoryManager>().allocate(1);
        mngr1->_check_integrity();
    }
    std::shared_ptr<SegmentManager> segmentMngr2 = SegmentManager::open(seg_file_name);
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

    std::shared_ptr<SegmentManager> segmentMngr3 = SegmentManager::open(seg_file_name);
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
    
    auto mngr4 = SegmentManager::open(seg_file_name);
    const TestHead* heads_of4 = mngr4->get_named_object<TestHead>(named_object);
    
    htbl64_t* htbl_addr_of4 = mngr4->from_far<htbl64_t>(heads_of4[0].table_pos);
    assert(_compare(htbl_sample, *htbl_addr_of4));

    sarr32_t* sarr_addr_of4 = mngr4->from_far<sarr32_t>(heads_of4[1].table_pos);
    assert(_compare(sarr_sample, *sarr_addr_of4));
    */
}

struct TestValue
{
    TestValue() :
        _value(0),
        _version(++version)
    {
    }
    TestValue(int value) :
        TestValue(){
        _value = value;
    }
    static int version;
    int _version;
    int _value;
};
int TestValue::version = 0;

void test_CacheManager()
{
    std::cout << "test Cache management..." << std::endl;
    const unsigned limit = 10;
    OP::trie::CacheManager<int, TestValue> cache(limit);
    auto f = [](int key){
        return TestValue(key + 1);
    };
    for (auto i = 0; i < limit; ++i)
    {
        TestValue r = cache.get(i, f);
        TestValue r1 = cache.get(i, f);//may return new instance of value
        //assert(r._version == r1._version);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto trie = cache.get(i);
        assert(trie._version == r._version || trie._version == r1._version);
    }
    cache._check_integrity();
    assert(cache.size() == cache.limit());
    TestValue r = cache.get(limit+1, f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(r._value == (limit + 2));
    //check that 0 was wiped
    bool f2_called = false;
    auto f2 = [&](int key){
        f2_called = true;
        return TestValue(-1);
    };
    r = cache.get(0, f2);
    
    assert(f2_called);
    assert(r._value == -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    try{
        cache.get(1);//1 had to be poped
        assert(false); //exception must be raised
    }
    catch (std::out_of_range&){}
    cache._check_integrity();
    //now lowest is 2, let float it up
    f2_called = false;
    r = cache.get(2, f2);
    assert(!f2_called); //factory must not be invoked for 2
    //pop up some value (it is 3)
    r = cache.get(limit + 10, f2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(f2_called);
    //now test that 2 is there
    cache.get(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cache._check_integrity();
}
extern void test_NodeManager();
extern void test_RangeContainer();
extern void test_Range();
extern void test_TransactedSegmentManager();

int main(int argc, char* argv[])
{
    bool allow_long_test = false;

    test_align();
    test_NodeHash_insert(allow_long_test);
    test_NodeHash_erase(allow_long_test);
    test_NodeArray_insert(allow_long_test);
    test_RangeContainer();
    test_Range();
    test_SegmentManager();
    test_CacheManager();
    test_NodeManager();
    test_TransactedSegmentManager();
    return 0;
}
