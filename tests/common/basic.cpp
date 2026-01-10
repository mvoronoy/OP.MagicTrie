// #define _SCL_SECURE_NO_WARNINGS 1

#include <iostream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/trie/Containers.h>

#include <set>
#include <cstdint>
#include <cassert>
#include <iterator>

#include <ctime>
#include <chrono>
#include <regex>

#include <op/utest/unit_test.h>
#include <op/utest/cmdln_unit_test.h>


namespace { //anonymose
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

        assert(10 == OP::vtm::align_on(3, 10));
        assert(20 == OP::vtm::align_on(11, 10));
        assert(0 == OP::vtm::align_on(0, 10));
        assert(10 == OP::vtm::align_on(10, 10));
        //check for 
        assert(17 == OP::vtm::align_on(16, 17));
        assert(17 == OP::vtm::align_on(3, 17));
        assert(0 == OP::vtm::align_on(0, 17));
        assert(34 == OP::vtm::align_on(18, 17));
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

    
    static void test_Abort(OP::utest::TestRuntime &result)
    {
        assert(false);
        result.fail("Exception must be raised");
    }

static auto& module_suite = OP::utest::default_test_suite("Arbitrary")
    .declare("align", test_align)
    .declare_disabled/*declare_exceptional*/("testAbort", test_Abort)
    ;
} //ns:_
