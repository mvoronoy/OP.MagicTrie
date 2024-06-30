#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

#include <op/common/ThreadPool.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;


namespace {

    void test_simple(TestRuntime& tresult)
    {
        OP::utils::ThreadPool pool;
        auto src1 = std::vector{3, 2, 1};

        auto a1 = src::of_container(std::cref(src1));

        auto res1 = a1 >> then::parallel_sort<2>(pool);
        tresult.assert_true(res1.compound().is_sequence_ordered());
        tresult.assert_that<eq_sets>(std::array{1, 2, 3}, res1);

        // test the same with ordered sequence
        auto res2 = src::of_container(std::set{ 1, 2, 3 }) >> then::parallel_sort<2>(pool);
        tresult.assert_true(res2.compound().is_sequence_ordered());
        tresult.assert_that<eq_sets>(std::array{ 1, 2, 3 }, res2);


        //test edge cases...
        std::vector<int> src2(100 * 2);
        auto half = src2.begin() + src2.size() / 2; 
        std::iota(src2.begin(), half, 0);
        std::iota(half, src2.end(), 0); //duplicate from 0 to half
        std::shuffle( 
            src2.begin(), src2.end(), tools::RandomGenerator::instance().generator());

        auto expected = std::multiset(src2.begin(), src2.end());
        tresult.assert_that<eq_sets>(
            expected,
            src::of_container(std::move(src2)) >> then::parallel_sort<1>(pool),
            "edge case with 1 thread and duplicates"
        );
        //
        // empties
        //
        expected.clear();
        tresult.assert_that<eq_sets>(
            expected,
            src::null<const int&>() >> then::parallel_sort<3>(pool),
            "empty case #1 failed"
        );

        tresult.assert_that<eq_sets>(
            expected,
            src::of_container(std::vector<int>{}) >> then::parallel_sort<3>(pool),
            "empty case #2 failed"
        );
    }

    void test_big(TestRuntime& tresult)
    {
        OP::utils::ThreadPool pool;
        //big random sequence
        std::vector<int> src2(10000);
        std::iota(src2.begin(), src2.end(), 0);
        std::shuffle(
            src2.begin(), src2.end(), tools::RandomGenerator::instance().generator());
        auto expected = std::set(src2.begin(), src2.end());

        tresult.assert_that<eq_sets>(
            expected,
            src::of_container(src2) >> then::parallel_sort<7>(pool)
        );
        
        tresult.info() << "7 threads to sort, avg takes = " <<
            tresult.measured_run([&]() {
                    size_t dummy = 0;
                    src::of_container(src2) >> then::parallel_sort<7>(pool)
                        >>= apply::count(dummy);
                }) << "ms \n";

        tresult.info() << "2 threads to sort, avg takes = " <<
            tresult.measured_run([&]() {
                    size_t dummy = 0;
                    src::of_container(src2) >> then::parallel_sort<2>(pool)
                        >>= apply::count(dummy);
                }) << "ms \n";

        tresult.info() << "1 threads to sort, avg takes = " <<
            tresult.measured_run([&]() {
                    size_t dummy = 0;
                    src::of_container(src2) >> then::parallel_sort<1>(pool)
                        >>= apply::count(dummy);
                }) << "ms \n";

        tresult.info() << "3 threads to sort, avg takes = " <<
            tresult.measured_run([&]() {
                    size_t dummy = 0;
                    src::of_container(src2) >> then::parallel_sort<3>(pool)
                        >>= apply::count(dummy);
                }) << "ms \n";

    }

    static auto& module_suite = OP::utest::default_test_suite("flur.then.parallel_sort")
        .declare("simple", test_simple)
        .declare("big", test_big, {"long"})
        ;
}//ns: empty
