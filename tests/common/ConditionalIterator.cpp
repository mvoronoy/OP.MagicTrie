#include <array>
#include <concepts>

#include <op/common/ConditionalIterator.h>
#include <op/utest/unit_test.h>

namespace 
{

    using namespace OP::utest;

    // Validate against the C++20 output_iterator concept at compile time
    static_assert(std::output_iterator<OP::common::filtered_back_insert_iterator<std::vector<int>, bool(*)(int)>, int>);

    void test_Basic(OP::utest::TestRuntime& tresult) 
    {
        std::vector<int> result;
        auto out = OP::common::filter_back_inserter(result, [](int x)->bool{return x & 1;});
        
        // Works with classic assignment loops
        *out = 42; //even!
        ++out;
        *out = 100;//even!

        // Works with standard STL algorithms
        int data[] = {1, 2, 3};
        std::copy(std::begin(data), std::end(data), out);
        tresult.assert_that<eq_sets>(result, std::array{1, 3}, "basic compare failed"); //odd
    }

    static auto& module_suite = OP::utest::default_test_suite("filtered_back_insert_iterator")
        .declare("basic", test_Basic)
    ;

}//ns:

