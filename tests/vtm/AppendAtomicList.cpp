#include <iostream>
#include <numeric>
#include <shared_mutex>
#include <array>

#include <op/common/OsDependedMacros.h>
#include <op/common/AppendAtomicList.h>
#include <op/common/atomic_utils.h>

#include <op/vtm/managers/BucketIndexedList.h>

#include <op/utest/unit_test.h>


#ifdef OP_COMMON_OS_WINDOWS
//additional hardening to check memory leaks

    #define _CRTDBG_MAP_ALLOC
    #include <stdlib.h>
    #include <crtdbg.h>

#endif 

namespace 
{
    using namespace OP::utest;
    using namespace OP::common;

    const size_t num_iterations = 100;
    const size_t num_threads = 50;

    template <class T>
    constexpr T sum_of_arithmetic_progression(T from, size_t count, T diff = 1) noexcept
    {
        return (2 * from + (count-1) * diff) * count / 2;
    }

    template <class TContainer, class ItemFactory>
    void concurrent_populate(OP::utest::TestRuntime& tresult, TContainer& dut, ItemFactory&& f)
    {
        const auto populate = [&](TContainer& dut, size_t from, size_t to) {
            for(;from != to; ++from)
                dut.append(f(from));
            };

        std::vector<std::thread> threads(num_threads);
        size_t account = 0;
        // Create multiple threads that perform random operations on the list
        for (auto& thread : threads) 
        {
            thread = std::thread(populate, std::ref(dut), account, account + num_iterations);
            account += num_iterations;
        }

        // Wait for all threads to complete their operations
        for (auto& thread : threads) 
        {
            thread.join();
        }
    }


    void _generic_impl(OP::utest::TestRuntime& tresult)
    {
        AppendAtomicList<int> al;
        tresult.assert_that<equals>(al.begin(), al.end());
        al.append(57);
        for (auto n : al)
            tresult.debug() << n << "\n";
        al.remove(al.begin());
        tresult.assert_that<equals>(al.begin(), al.end());

        AppendAtomicList<size_t> al2;
        concurrent_populate(tresult, al2, [](size_t step) {return step; });
        constexpr size_t expected_total = sum_of_arithmetic_progression(0, num_iterations * num_threads);

        size_t sum = std::accumulate(al2.begin(), al2.end(), size_t{ 0 });
        tresult.debug() << "sum list's items = " << sum << "\n";

        tresult.assert_that<equals>(expected_total, sum);

        tresult.debug() << "List of numbers that demonstrate parallel adding (not in sequential order):\n";
        size_t prev = 0;
        for (const auto n : al2)
        {
            if (n < prev)
                tresult.debug() << "{" << prev << ", " << n << "}\n";
            prev = n;
        }

        auto del_item = *al2.begin();
        al2.remove(al2.begin());
        size_t result = std::accumulate(al2.begin(), al2.end(), size_t{ 0 });
        tresult.assert_that<equals>(result, sum - del_item);

        //go last

        auto last = al2.begin();
        for (auto i = al2.begin(); i != al2.end(); ++i)
            last = i;
        tresult.assert_that<not_equals>(last, al2.end());

        auto del_item2 = *last;
        last = al2.remove(last);
        tresult.assert_that<equals>(last, al2.end());
        result = std::accumulate(al2.begin(), al2.end(), size_t{ 0 });
        tresult.assert_that<equals>(result, sum - del_item - del_item2);
    }

    void test_append_remove_clear(OP::utest::TestRuntime& tresult)
    {

#ifdef OP_COMMON_OS_WINDOWS
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        // Send all reports to STDOUT
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);

        _CrtMemState st_old, st_new, st_diff;

        // Take a snapshot of the memory state at the beginning
        _CrtMemCheckpoint(&st_old);

#endif //#ifdef OP_COMMON_OS_WINDOWS

        _generic_impl(tresult);
#ifdef OP_COMMON_OS_WINDOWS
        //new int(0x55aa); 
        // Take a snapshot of the memory state at the end
        _CrtMemCheckpoint(&st_new);

        // 3. Compare the two memory states
        if (_CrtMemDifference(&st_diff, &st_old, &st_new)) // Returns true if a difference (leak) is found
        {
            tresult.error() << "----------- Memory Leaks Detected ---------" << std::endl;

            // 4. Dump statistics about the difference to the debug output window
            _CrtMemDumpStatistics(&st_diff);

            // 5. Dump details of all objects allocated since sOld (useful for identifying where the leak happened)
            _CrtMemDumpAllObjectsSince(&st_old);

            // Optional: Dump all leaks detected since program start
            // _CrtDumpMemoryLeaks(); 
            tresult.fail();
        }
#endif //#ifdef OP_COMMON_OS_WINDOWS
    }


    struct TestRecord
    {
        explicit TestRecord(size_t v) noexcept
            :_value(v) 
        {
        }

        size_t _value;

    };


    struct TestRecordIndexerByBloom
    {
        std::atomic<size_t> _bloom_filter;
        std::atomic<size_t> _min = std::numeric_limits<size_t>::max(), _max = 0;

        constexpr static size_t hash(size_t v) noexcept
        {
            //good spreading of sequential bits for average case when transaction_id grows monotony
            //return v * 0x5fe14bf901200001ull;
            return v * 101;
        }


        void index(const TestRecord& r) noexcept
        {
            _bloom_filter.fetch_or(hash(r._value));
            //c++26?: _min.fetch_min(r._value);
            OP::utils::cas_extremum<std::less>(_min, r._value);
            //c++26?: _max.fetch_max(r._value);
            OP::utils::cas_extremum<std::greater>(_max, r._value);
        }
        
        bool check(const TestRecord& r) const noexcept
        {
            return check(r._value);
        }

        bool check(size_t query) const noexcept
        {
            const auto hash_v = hash(query);

            return (hash_v & _bloom_filter.load(std::memory_order_acquire)) == hash_v
                    && query >= _min 
                    && query <= _max
                ;
        }
    };

    void _test_indexing_impl(OP::utest::TestRuntime& tresult)
    {
        using indexed_container_t = OP::vtm::BucketIndexedList<TestRecord, TestRecordIndexerByBloom>;
        indexed_container_t co1;
        concurrent_populate(tresult, co1, 
            [](size_t i) {return std::unique_ptr<TestRecord>(new TestRecord(i)); }
        );
        constexpr size_t expected_total = sum_of_arithmetic_progression(0, num_iterations * num_threads);
        constexpr auto count_sum = [](auto& container)->size_t{
            size_t sum = 0;
            container.for_each([&](const TestRecord& r) { sum += r._value; });
            return sum;
        };
        
        tresult.assert_that<equals>(count_sum(co1), expected_total);
        const size_t expected_buckets = co1.buckets_count();

        constexpr size_t mid_test_value = num_iterations * num_threads / 2;
        size_t false_positive_count = 0;
        //match all TestRecord with mid_test_value
        co1.indexed_for_each(mid_test_value, [&](const auto& record) {
            if (record._value != mid_test_value)
                ++false_positive_count;
            }
        );
        auto percentage = (100 * false_positive_count) / (num_iterations * num_threads);
        tresult.info() 
            << "False positive indexing hit = "<< false_positive_count 
            << " it is " << percentage << "%"
            << "\n";
        tresult.assert_that<less>(percentage, 51, "false positive hit count must be less than 51%");
        
        const auto thread_soft_erase_function = [&](indexed_container_t& container, size_t from, size_t to) {

            std::vector<size_t> samples_to_erase(to - from);
            std::iota(samples_to_erase.begin(), samples_to_erase.end(), from);
            std::shuffle(
                samples_to_erase.begin(), samples_to_erase.end(),
                tresult.randomizer().generator()
            );
            for (auto i = samples_to_erase.begin(); i != samples_to_erase.end(); ++i)
            {
                // remove exact 1 number
                tresult.assert_true(co1.soft_remove_if_first(*i, [removing = *i](const auto& record) -> bool {
                    return (record._value == removing);
                    }), OP_CODE_DETAILS() << "soft remove couldn't find a value:" << *i << "\n");
                //auto sum = count_sum(co1);
                //i = samples_to_erase.erase(i);
                //tresult.assert_that<equals>(sum, std::accumulate(samples_to_erase.begin(), samples_to_erase.end(), size_t{ 0 }),
                //    "sum after erase must be equal to sum of sampling vector"
                //);
            }
        };
        
        std::array<std::thread, num_threads> threads;
        size_t thread_idx = 0;
        // Create multiple threads that perform random operations on the list
        for (auto& thread : threads)
        {
            thread = std::thread(thread_soft_erase_function, std::ref(co1), thread_idx, thread_idx + num_iterations);
            thread_idx += num_iterations;
        }

        // Wait for all threads to complete their operations
        for (auto& thread : threads)
        {
            thread.join();
        }
        auto sum = count_sum(co1);
        tresult.assert_that<equals>(sum, 0, "sum after erase must be 0");

        tresult.assert_that<equals>(co1.buckets_count(), expected_buckets, 
            "soft delete doesn't cause buckets shrinking");
        
        tresult.assert_that<equals>(co1.clean(1), 1);
        tresult.assert_that<equals>(co1.clean(), expected_buckets - 1);
        
        tresult.assert_that<equals>(co1.buckets_count(), 0);
    }

    void test_indexing(OP::utest::TestRuntime& tresult)
    {
#ifdef OP_COMMON_OS_WINDOWS
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        // Send all reports to STDOUT
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);

        _CrtMemState st_old, st_new, st_diff;

        // Take a snapshot of the memory state at the beginning
        _CrtMemCheckpoint(&st_old);

#endif //#ifdef OP_COMMON_OS_WINDOWS
        _test_indexing_impl(tresult);
#ifdef OP_COMMON_OS_WINDOWS
        //new int(0x55aa); 
        // Take a snapshot of the memory state at the end
        _CrtMemCheckpoint(&st_new);

        // 3. Compare the two memory states
        if (_CrtMemDifference(&st_diff, &st_old, &st_new)) // Returns true if a difference (leak) is found
        {
            tresult.error() << "----------- Memory Leaks Detected ---------" << std::endl;

            // 4. Dump statistics about the difference to the debug output window
            _CrtMemDumpStatistics(&st_diff);

            // 5. Dump details of all objects allocated since sOld (useful for identifying where the leak happened)
            _CrtMemDumpAllObjectsSince(&st_old);

            // Optional: Dump all leaks detected since program start
            // _CrtDumpMemoryLeaks(); 
            tresult.fail();
        }
#endif //#ifdef OP_COMMON_OS_WINDOWS
    }
    static auto& module_suite = OP::utest::default_test_suite("AppendAtomicList")
        .declare("general", test_append_remove_clear)
        .declare("indexing", test_indexing)
    ;
} //ns:
