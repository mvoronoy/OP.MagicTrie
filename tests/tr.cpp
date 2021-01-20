    // trie.cpp : Defines the entry point for the console application.
//
#define _SCL_SECURE_NO_WARNINGS 1

#include <iostream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/CacheManager.h>
#include <op/trie/Containers.h>

#include <set>
#include <cstdint>
#include <cassert>
#include <iterator>
//#include <windows.h>

#include <ctime>
#include <chrono>
#include <regex>

#include <op/utest/unit_test.h>
#include <op/utest/cmdln_unit_test.h>

using namespace OP::trie;



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

struct TestValue
{
    TestValue() :
        _value(0),
        _version(++version)
    {
    }
    explicit TestValue(int value) :
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
    assert(r._value == 3);
    //pop up some value (it is 3)
    r = cache.get(limit + 10, f2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(f2_called);
    assert(r._value == -1);
    //now test that 2 is there
    cache.get(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cache._check_integrity();
}
static void test_Abort(OP::utest::TestResult &result)
{
    assert(false);
    result.fail("Exception must be raised");
}

static auto module_suite = OP::utest::default_test_suite("Arbitrary")
->declare(test_align, "align")
->declare(test_CacheManager, "cacheManager")
->declare_disabled/*declare_exceptional*/(test_Abort, "testAbort")
;

int main(int argc, char* argv[])
{
    return OP::utest::cmdline::simple_command_line_run(argc, argv);
}
