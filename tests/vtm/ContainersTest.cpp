#include <op/utest/unit_test.h>

#include <op/trie/Containers.h>

using namespace OP::trie;

bool allow_long_test = false;

struct TestEmpty{};

template <node_size_t capacity_c>
struct TestHashTable : public NodeHashTable< TestEmpty, capacity_c >
{
    typedef NodeHashTable < TestEmpty, capacity_c > base_t;
};

template <class T, class Tbl, class F>
std::set<T> _randomize(Tbl& tbl, F f = OP::utest::tools::random<T> )
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
        auto sample = _randomize<Key>(tbl, OP::utest::tools::random<Key>);
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

void NodeHash_erase(OP::utest::TestRuntime &tresult)
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
    
    {//nested block
        TestHashTable<16> nested_tbl;
        nested_tbl.insert(0);
        tresult.assert_true(1 == nested_tbl.insert(16).first._debug()); //force place
        tresult.assert_true(2 == nested_tbl.insert(1).first._debug()); //force place
        tresult.assert_true(3 == nested_tbl.insert(17).first._debug()); //force place
        tresult.assert_true(1 == nested_tbl.erase(0));
        tresult.assert_true(nested_tbl.size() == 3);
        auto p = nested_tbl.begin();
        tresult.assert_true(16 == *p++);
        tresult.assert_true(1 == *p++);
        tresult.assert_true(17 == *p++);
    }
    
    {//nested block
        TestHashTable<16> nested_tbl;
        nested_tbl.insert(0);
        tresult.assert_true(0 == nested_tbl.insert(0).first._debug()); //force place
        tresult.assert_true(1 == nested_tbl.insert(1).first._debug()); //force place
        tresult.assert_true(2 == nested_tbl.insert(16).first._debug()); //force place
        tresult.assert_true(1 == nested_tbl.erase(1));
        tresult.assert_true(nested_tbl.size() == 2);
        {
            auto p = nested_tbl.begin();
            tresult.assert_true(0 == *p++);
            tresult.assert_true(16 == *p++);
        }
        nested_tbl.erase(0);
        nested_tbl.erase(16);
        //erase test on range cases
        tresult.assert_true(14 == nested_tbl.insert(0xe).first._debug());
        tresult.assert_true(15 == nested_tbl.insert(0xf).first._debug());
        tresult.assert_true(0 == nested_tbl.insert(0x1e).first._debug());
        tresult.assert_true(1 == nested_tbl.erase(0xf));
        tresult.assert_true(nested_tbl.size() == 2);
        
        {
            auto p = nested_tbl.begin();
            tresult.assert_true(14 == *p++);
            tresult.assert_true(0x1e == *p++);
        }
        nested_tbl.erase(14);
        nested_tbl.erase(0x1e);
        //
        tresult.assert_true(14 == nested_tbl.insert(0xe).first._debug()); 
        tresult.assert_true(15 == nested_tbl.insert(0xf).first._debug()); 
        tresult.assert_true(0 == nested_tbl.insert(0x1e).first._debug()); 
        tresult.assert_true(1 == nested_tbl.insert(0).first._debug()); 
        tresult.assert_true(1 == nested_tbl.erase(0xe));
        tresult.assert_true(nested_tbl.size() == 3);
        {
            auto p = nested_tbl.begin();
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
}

//using std::placeholders;
static auto& module_suite = OP::utest::default_test_suite("vtm.Containers")
.declare("Hash(Insert)", NodeHash_insert)
.declare("Hash(Erase)", NodeHash_erase)
;