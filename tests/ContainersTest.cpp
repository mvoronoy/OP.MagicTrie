
#include <op/utest/unit_test.h>

#include <op/trie/Containers.h>

using namespace OP::trie;
bool allow_long_test = false;
struct TestEmpty{};
template <node_size_t capacity_c>
struct TestHashTable : public NodeHashTable< TestEmpty, capacity_c >
{
    typedef NodeHashTable < TestEmpty, capacity_c > base_t;
    TestHashTable() 
    {}
    ~TestHashTable()
    {
    }
    
};

template <class T, class Tbl, class F>
std::set<T> _randomize(Tbl& tbl, F f = OP::utest::tools::randomize<T> )
{
    std::set<T> result;
    while (tbl.size() < tbl.capacity())
    {
        auto r = f();
        if (tbl.insert(r).first != tbl.end())
            result.insert(r);
    }
    return result;
}

template <class Key, class T>
void _random_test(T &tbl)
{
    
    std::cout << "\nRandom erase with page=[" << tbl.capacity() << "]\n";
    const unsigned limit = 2u << 16;
    for (unsigned i = 0; i < limit; ++i)
    {
        if (!(i & (i - 1)))
            std::cout << '.';
        tbl.clear();
        auto sample =_randomize<Key>(tbl, OP::utest::tools::randomize<Key>);
        while (!tbl.empty())
        {
            auto p = sample.begin();
            std::advance(p, rand() % sample.size());
            tbl.erase(*p);
            sample.erase(p);
            auto callback = [&](const Key& ){
                std::cout << "Sample:\n";
                std::copy(sample.begin(), sample.end(),
                    std::ostream_iterator<int>(std::cout, ", "));
                std::cout << "\nTested:\n";
                std::copy(tbl.begin(), tbl.end(),
                    std::ostream_iterator<int>(std::cout, ", "));
            };
            OP_UTEST_ASSERT(OP::utest::tools::compare(sample, tbl, callback));
            
        }
    }

}



void NodeHash_insert( )
{
    for (unsigned i = 0; i < 8; ++i)
    {
        TestHashTable<8> tbl;
        OP_UTEST_ASSERT(i == tbl.insert(i).first._debug());
        unsigned x = 0;
        for (unsigned j = 8; j < 16; ++j, ++x)
        {
            if (x == i)
                x += 1;
            if (j==15)
                OP_UTEST_ASSERT(tbl.end() == tbl.insert(j).first);
            else
                OP_UTEST_ASSERT(x == tbl.insert(j).first._debug());
        }
    }
  
}

void NodeHash_erase(OP::utest::TestResult &tresult)
{
    tresult.info() << "test erase...\n";
    TestHashTable<16> tbl;
    tbl.insert(0);
    tresult.assert_true(0 == tbl.erase(1));
    tresult.assert_true(1 == tbl.size());
    tresult.assert_true(1 == tbl.erase(0));
    tresult.assert_true(0 == tbl.size());
    tresult.assert_true(0 == tbl.insert(0).first._debug());
    //render items with the same hashcode
    tresult.assert_true(1 == tbl.insert(16).first._debug());
    tresult.assert_true(2 == tbl.insert(32).first._debug());
    tresult.assert_true(tbl.end() == tbl.insert(48).first);
    tresult.assert_true(1 == tbl.erase(32));
    tresult.assert_true(2 == tbl.insert(48).first._debug());
    tresult.assert_true(1 == tbl.erase(16));
    tresult.assert_true(1 == tbl.find(48)._debug());
    tresult.assert_true(2 == tbl.insert(64).first._debug());

    tresult.assert_true(1 == tbl.erase(0));
    tresult.assert_true(tbl.end() == tbl.find(0));
    tresult.assert_true(0 == tbl.find(48)._debug());
    tresult.assert_true(1 == tbl.find(64)._debug());//shift must affect last one
    tresult.assert_true(2 == tbl.insert(0).first._debug());
    //test erase overflow
    tresult.assert_true(15 == tbl.insert(15).first._debug());
    tresult.assert_true(tbl.end() == tbl.insert(31).first); //since items at 0, 1 already populated
    tresult.assert_true(1 == tbl.erase(64));
    tresult.assert_true(1 == tbl.erase(48));
    tresult.assert_true(1 == tbl.erase(0));
    tresult.assert_true(0 == tbl.insert(31).first._debug()); //since items just released, so insert is success
    tresult.assert_true(1 == tbl.erase(15));
    tresult.assert_true(15 == tbl.find(31)._debug());//shift must affect last one
    tresult.assert_true(0 == tbl.insert(0).first._debug());
    if (1 == 1)
    {
        TestHashTable<16> tbl;
        tbl.insert(0);
        tresult.assert_true(1 == tbl.insert(16).first._debug()); //force place
        tresult.assert_true(2 == tbl.insert(1).first._debug()); //force place
        tresult.assert_true(3 == tbl.insert(17).first._debug()); //force place
        tresult.assert_true(1 == tbl.erase(0));
        tresult.assert_true(tbl.size() == 3);
        auto p = tbl.begin();
        tresult.assert_true(16 == *p++);
        tresult.assert_true(1 == *p++);
        tresult.assert_true(17 == *p++);
    }
    if (1 == 1)
    {
        TestHashTable<16> tbl;
        tbl.insert(0);
        tresult.assert_true(0 == tbl.insert(0).first._debug()); //force place
        tresult.assert_true(1 == tbl.insert(1).first._debug()); //force place
        tresult.assert_true(2 == tbl.insert(16).first._debug()); //force place
        tresult.assert_true(1 == tbl.erase(1));
        tresult.assert_true(tbl.size() == 2);
        {
            auto p = tbl.begin();
            tresult.assert_true(0 == *p++);
            tresult.assert_true(16 == *p++);
        }
        tbl.erase(0);
        tbl.erase(16);
        //erase test on range cases
        tresult.assert_true(14 == tbl.insert(0xe).first._debug());
        tresult.assert_true(15 == tbl.insert(0xf).first._debug());
        tresult.assert_true(0 == tbl.insert(0x1e).first._debug());
        tresult.assert_true(1 == tbl.erase(0xf));
        tresult.assert_true(tbl.size() == 2);
        
        {
            auto p = tbl.begin();
            tresult.assert_true(14 == *p++);
            tresult.assert_true(0x1e == *p++);
        }
        tbl.erase(14);
        tbl.erase(0x1e);
        //
        tresult.assert_true(14 == tbl.insert(0xe).first._debug()); 
        tresult.assert_true(15 == tbl.insert(0xf).first._debug()); 
        tresult.assert_true(0 == tbl.insert(0x1e).first._debug()); 
        tresult.assert_true(1 == tbl.insert(0).first._debug()); 
        tresult.assert_true(1 == tbl.erase(0xe));
        tresult.assert_true(tbl.size() == 3);
        {
            auto p = tbl.begin();
            tresult.assert_true(0 == *p++);
            tresult.assert_true(0x1e == *p++);
            tresult.assert_true(0xf == *p++);
        }
    }
    if (allow_long_test)
    {
         TestHashTable<8> tbl8;
        _random_test<std::uint8_t>(tbl8);
        TestHashTable<16> tbl16;
        _random_test<std::uint8_t>(tbl16);
        TestHashTable<32> tbl32;
        _random_test<std::uint8_t>(tbl32);
        TestHashTable<64> tbl64;
        _random_test<std::uint8_t>(tbl64);
        TestHashTable<128> tbl128;
        _random_test<std::uint8_t>(tbl128);
        TestHashTable<256> tbl256;
        _random_test<std::uint8_t>(tbl256);
    }
    tresult.info() << "\tpassed" << std::endl;
}
struct TestSortedArray : public NodeSortedArray < TestEmpty, 16 >
{
    typedef NodeSortedArray < TestEmpty, 16 > base_t;
    TestSortedArray() 
    {}
    ~TestSortedArray()
    {
    }
};

void test_NodeArray_insert(OP::utest::TestResult &tresult)
{
    std::cout << "test sorted array insert..." << std::endl;
    
    TestSortedArray tbl;
    tresult.assert_true(tbl.end() == tbl.insert(std::string("")));
    const atom_t a0[] = "a";
    const atom_t a1[] = "ab";
    const atom_t a2[] = "abc";
    const atom_t a3[] = "ac";
    auto p = std::begin(a1);
    auto tst_first = tbl.insert(p, std::end(a1));
    tresult.assert_true(0 == tst_first._debug());
    tresult.assert_true(1 == tbl.size());
    //insert the same second time
    p = std::begin(a1);
    tresult.assert_true( tst_first == tbl.insert(p, std::end(a1)) );

    p = std::begin(a3);
    auto tst_3 = tbl.insert(p, std::end(a3));
    tresult.assert_true(1 == tst_3._debug());

    p = std::begin(a2);
    auto tst_2 = tbl.insert(p, std::end(a2));
    tresult.assert_true(1 == tst_2._debug());
    tresult.assert_true(0 == tbl.find(std::begin(a1), std::end(a1))._debug());
    p = std::begin(a0);
    auto count_before_test = tbl.size();
    auto tst_0 = tbl.insert(p, p+1/*std::end(a0)*/);
    tresult.assert_true(0 == tst_0._debug());
    tresult.assert_true(count_before_test + 1 == tbl.size());
    // Test substring-length control
    const atom_t a08[] = "012345678";
    const atom_t a07[] = "01234567";

    p = std::begin(a08);
    auto tst_08 = tbl.insert(p, std::end(a08));
    tresult.assert_true('8' == *p);
    tresult.assert_true(0 == tst_08._debug());
    p = std::begin(a07);
    tresult.assert_true(tst_08 == tbl.insert(p, std::end(a07)));
    const atom_t templ[7] = "x";
    const atom_t *templ_end = templ + 1 + sizeof(short);
    for (unsigned short i = 0; tbl.size() < tbl.capacity(); ++i)
    {
        *((short*)(templ + 1)) = i;
        auto n = tbl.size();
        auto inspos = tbl.insert(p = templ, templ_end);
        auto v = *inspos;
        tresult.assert_true(OP::utest::tools::range_equals(templ, templ_end, v.first, v.second));
    }
    const atom_t ustr[] = "abcde";
    p = std::begin(ustr);
    auto fpos = tbl.best_match(p, std::end(ustr));
    tresult.assert_true(tbl.find(p = std::begin(a2), std::end(a2)) == fpos);
    
    if (1 == 1)
    {
        //NodeSortedArray<16, TestEmpty> tbl;
        tbl.clear();
        tbl.insert(std::string("abc"));
        tbl.insert(std::string("ac"));
        const std::string t1("a");
        std::string::const_iterator p = t1.begin();
        auto ins = tbl.best_match(p, t1.end());
        tresult.assert_true(ins == tbl.begin());

        const std::string t2("b");
        p = t2.begin();
        tresult.assert_true(tbl.end() == tbl.best_match(p, t2.end()));
        const std::string t3("abcde");
        p = t3.begin();
        ins = tbl.best_match(p, t3.end());
        tresult.assert_true(ins == tbl.begin());
        tresult.assert_true(*p == 'd');
        tresult.assert_true(*++p == 'e');
    }
    //
    //------
    if (allow_long_test)
    {
        TestSortedArray tbl64;
        //_random_test(tbl64);
    }
}



//using std::placeholders;
static auto module_suite = OP::utest::default_test_suite("Containers")
->declare(NodeHash_insert, "Hash(Insert)")
->declare(NodeHash_erase, "Hash(Erase)")
->declare(test_NodeArray_insert, "SortedArray(Insert)")
;