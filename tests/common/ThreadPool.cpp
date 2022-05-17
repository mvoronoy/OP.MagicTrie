#include <op/common/ThreadPool.h>
#include <op/utest/unit_test.h>

#include <chrono>
#include <thread>
#include <iostream>

using namespace OP::utest;
using namespace OP::utils;
using namespace std::string_literals;
using namespace std::chrono_literals;


namespace 
{    
void test_Integration(OP::utest::TestRuntime& tresult)
{
    
    ThreadPool tp(1, 1);
    auto start = std::chrono::steady_clock::now();
        
    auto f1 = tp.async([&](int arg, int c )->int{ 
        tresult.debug() <<  "Called from async<int>(" << arg << ',' << c << ")\n";
        std::this_thread::sleep_for(3000ms);
        return 75; }, 57, 75);
    auto f2 = tp.async([&](){ 
        tresult.debug() << "Called from void async<void>\n"; 
        std::this_thread::sleep_for(1000ms);
    });
    tp.one_way([&](float a){ 
        tresult.debug() << "Called from one-way:" << a << "\n"; 
        std::this_thread::sleep_for(1000ms);
    }, 5.1f);
    tresult.debug() << "waiter...\n" << std::flush;
    std::this_thread::sleep_for(2000ms);
    tresult.debug() << "\nComplete result from asybc: " << f1.get() << "\n";
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end-start;
    tresult.debug() << "Total delay mustn't exceed 3.5:" << diff.count() << "\n";
    tresult.assert_that<less>(diff.count(), 3500, OP_CODE_DETAILS("too long delay"));
    
   //std::this_thread::sleep_for(3000ms);
}


static auto& module_suite = OP::utest::default_test_suite("thread-pool")
.declare("integration", test_Integration)
;

}