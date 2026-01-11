#include <unordered_set>
#include <unordered_map>
#include <execution>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/flur/flur.h>

#include <op/vtm/AppendOnlySkipList.h>
#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/Range.h>

namespace
{
    using namespace OP::utest;

    static const char* test_file_name = "a0skip.test";
    using MyRange = OP::Range<unsigned>;

    inline constexpr std::uint64_t bloom_calc(const MyRange& r) noexcept
    {
        auto bit_width = OP::trie::log2(r.count()) + 1;
        return ((1ull << bit_width) - 1)
            << OP::trie::log2(r.pos());
    }

    struct RandomRangeGenerator
    {
        tools::RandomGenerator& _rnd_gen;
        unsigned _from, _max_count;

        RandomRangeGenerator(tools::RandomGenerator& rnd_gen, unsigned max_count = 50, unsigned from = 0)
            : _rnd_gen(rnd_gen)
            , _from(from)
            , _max_count(max_count)
        {
        }

        MyRange operator()()
        {
            auto pos = _rnd_gen.next_in_range<std::uint32_t>(_from, _from + _max_count);
            auto dim = _rnd_gen.next_in_range<std::uint32_t>(1, _max_count);
            return MyRange(pos, dim);
        }
    };

    struct TestPayload
    {

        TestPayload(MyRange key,
            std::uint64_t a, std::uint32_t b, double c) noexcept
            : _key(key)
            , v1(a)
            , inc(b)
            , v2(c)
        {
        }

        TestPayload(MyRange key) noexcept
            :TestPayload(key, 0, 0, 0)
        {
        }

        MyRange _key;
        std::uint64_t v1;
        std::uint32_t inc;
        double v2;

    };

    struct PayloadBloomIndexer
    {
        std::uint64_t _bloom_filter = 0; //will be persisted in A0Log
        inline void index(const TestPayload& item_to_index) noexcept
        {
            _bloom_filter |= bloom_calc(item_to_index._key);
        }

        inline OP::vtm::BucketNavigation check(const TestPayload& item) const  noexcept
        {
            auto bc = bloom_calc(item._key);
            return (_bloom_filter & bc) //check just hit the bit, not full match
                ? OP::vtm::BucketNavigation::not_sure
                : OP::vtm::BucketNavigation::next
                ;
        }
        // check can be polymorph by query type
        inline OP::vtm::BucketNavigation check(const MyRange& item) const  noexcept
        {
            auto bc = bloom_calc(item);
            return (_bloom_filter & bc) //check just hit the bit, not full match
                ? OP::vtm::BucketNavigation::not_sure
                : OP::vtm::BucketNavigation::next
                ;
        }

        //inline bool filter(const TestPayload& item_in_bucket, const TestPayload& query) const noexcept
        //{
        //    return item_in_bucket._key.is_overlapped(query._key);
        //}
    };

    struct PayloadMinMaxIndexer
    {
        MyRange _min_max{ 0, 0 };

        inline void index(const TestPayload& item_to_index) noexcept
        {
            _min_max = OP::zones::unite_zones(_min_max, item_to_index._key);
        }

        inline OP::vtm::BucketNavigation check(const TestPayload& item) const noexcept
        {
            return item._key.is_overlapped(_min_max)
                ? OP::vtm::BucketNavigation::not_sure
                : OP::vtm::BucketNavigation::next
                ;
        }
        //inline bool filter(const TestPayload& item_in_bucket, const TestPayload& query) const noexcept
        //{
        //    return item_in_bucket._key.is_overlapped(query._key);
        //}
    };

    void test_Emplace(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool tp;
        std::shared_ptr<OP::vtm::AppendOnlyLog> a0l = OP::vtm::AppendOnlyLog::create_new(
            tp, test_file_name
            /*OS minimal allowed size for segment-size*/
        );

        auto [list_address, skplst] =
            OP::vtm::create_a0_skip_list<TestPayload, PayloadBloomIndexer, PayloadMinMaxIndexer>(a0l);

        RandomRangeGenerator rand_range(tresult.randomizer());

        constexpr double expected_value = 57.0;
        std::unordered_multimap<MyRange, TestPayload> bucket1, bucket2;
        // populate over 2 buckets with non intersecting ranges
        rand_range._from = 0; //from 0 to 50
        for (auto i = 0; i < skplst->bucket_capacity_c; ++i)
        {
            auto key = rand_range();
            auto payload = TestPayload(key, 0, i, expected_value);
            bucket1.emplace(key, payload);
            skplst->emplace(std::move(payload));
        }

        rand_range._from = 100; //from 100 to 150
        for (auto i = 0; i < skplst->bucket_capacity_c; ++i)
        {
            auto key = rand_range();
            auto payload = TestPayload(key, 1, i, expected_value);
            bucket2.emplace(key, payload);
            skplst->emplace(payload);
        }

        //start testing from bucket 2
        MyRange q1(100, 50);
        skplst->indexed_for_each(q1, [&](/*const TestPayload* */ auto begin, auto end) {
            for (; begin != end; ++begin)
            {
                if (!begin->_key.is_overlapped(q1))
                    continue;
                auto found = bucket2.find(begin->_key);
                tresult.assert_true(found != bucket2.end());
                bucket2.erase(found);
            }
            });
        tresult.assert_that<equals>(0, bucket2.size());

        //start testing bucket1
        MyRange q2(0, 50);
        skplst->indexed_for_each(q2, [&](const TestPayload& candidate) {
            if (candidate._key.is_overlapped(q2))
            {
                auto found = bucket1.find(candidate._key);
                tresult.assert_true(found != bucket1.end());
                bucket1.erase(found);
                tresult.assert_that<almost_eq>(expected_value, candidate.v2);
            }
            });
        tresult.assert_that<equals>(0, bucket1.size());
    }

    void test_NewSegment(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool tp;
        std::shared_ptr<OP::vtm::AppendOnlyLog> a0l = OP::vtm::AppendOnlyLog::create_new(
            tp, test_file_name
            /*OS minimal allowed size for segment-size*/
        );
        constexpr double expected_value = 57.0;
        RandomRangeGenerator rand_range(tresult.randomizer(), 1000);
        const size_t test_size = (3 * a0l->segment_size() / sizeof(TestPayload)) / 2;

        std::vector<MyRange> all_ranges(test_size);
        std::vector<std::pair<MyRange, std::unordered_multiset<MyRange>>> all_probes(test_size);

        std::generate_n(all_ranges.begin(), test_size, rand_range);
        auto [list_address, skplst] = OP::vtm::create_a0_skip_list<TestPayload, PayloadBloomIndexer>(a0l);

        size_t order = 0;
        for (const auto& test : all_ranges)// O(n^2)
        {
            auto payload = TestPayload(test, 1, ++order, expected_value);
            skplst->emplace(std::move(payload));
        }

        //build random probes that may overlap or not with all_ranges
        bool has_some_intersect = false; //indicate at least once all_probes & all_ranges was intersected
        for (auto& [probe_range, overlapped] : all_probes)
        {
            probe_range = rand_range();
            for (const auto& test : all_ranges)// O(n^2)
            {
                if (test.is_overlapped(probe_range))
                {
                    has_some_intersect = true;
                    overlapped.insert(test);
                }
            }
        }
        tresult.assert_true(has_some_intersect, "Joke of probability, test scope should not be empty");
        skplst = nullptr; //close
        a0l = nullptr;

        //re-open
        a0l = OP::vtm::AppendOnlyLog::open(tp, test_file_name);
        skplst = OP::vtm::open_a0_skip_list<TestPayload, PayloadBloomIndexer>(a0l, list_address);

        for (auto& [probe_range, overlapped] : all_probes)
        {
            skplst->indexed_for_each(probe_range, [&, &probe_range = probe_range, &overlapped = overlapped](auto from, auto to) -> void {
                for (auto i = from; i != to; ++i)
                {
                    if (!i->_key.is_overlapped(probe_range))
                        continue;
                    auto found_probe = overlapped.find(i->_key);
                    tresult.assert_false(found_probe == overlapped.end());
                    overlapped.erase(found_probe);
                }
                });
            tresult.assert_true(overlapped.empty(), "not all overlapped ranges were found");
        }
    }

    struct DummyIndexer
    {
        template <class T>
        inline void index(const T&) noexcept
        {/*nothing to store*/
        }

        template <class T>
        inline OP::vtm::BucketNavigation check(const T& item) const  noexcept
        {
            return OP::vtm::BucketNavigation::worth;
        }
    };

    void test_PolyList(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool tp;
        std::shared_ptr<OP::vtm::AppendOnlyLog> a0l = OP::vtm::AppendOnlyLog::create_new(
            tp, test_file_name
            /*OS minimal allowed size for segment-size*/
        );


        auto [list_address1, skplst1] = OP::vtm::create_a0_skip_list<std::int32_t, DummyIndexer>(a0l);
        auto [list_address2, skplst2] = OP::vtm::create_a0_skip_list<std::int64_t>(a0l); //no indexer
        //actually both list consumes same amount of memory because of alignment
        size_t counter = 0;
        //populate ...
        for (; a0l->segments_count() < 5; ++counter)
            if (counter & 1)
                skplst1->emplace(static_cast<std::int32_t>(counter));
            else
                skplst2->emplace(counter);

        //test...
        std::int32_t eval1 = 1;
        for (auto check_value : skplst1->indexed_for_each(0/*doesn't matter*/))
        {
            tresult.assert_that<equals>(eval1, check_value);
            eval1 += 2;
        }
        std::int64_t eval2 = 0;
        for (auto check_value : skplst2->indexed_for_each(0/*doesn't matter*/))
        {
            tresult.assert_that<equals>(eval2, check_value);
            eval2 += 2;
        }
        // simultaneous ...

        auto run_simultaneous_scan = [&](auto range1, auto range2) {
            std::int32_t i1 = 1;
            std::int64_t i2 = 0;
            auto seq1 = range1.compound();
            auto seq2 = range2.compound();
            for (seq1.start(), seq2.start(); i1 < eval1 || i2 < eval2; i1 += 2, i2 += 2)
            {
                if (i1 < eval1)
                {
                    tresult.assert_true(seq1.in_range());
                    tresult.assert_that<equals>(i1, seq1.current());
                    seq1.next();
                }
                if (i2 < eval2)
                {
                    tresult.assert_true(seq2.in_range());
                    tresult.assert_that<equals>(i2, seq2.current());
                    seq2.next();
                }
            }
            tresult.assert_false(seq1.in_range());
            tresult.assert_false(seq2.in_range());
            };

        run_simultaneous_scan(skplst1->indexed_for_each(0), skplst2->indexed_for_each(0));
        run_simultaneous_scan(skplst1->async_indexed_for_each(0), skplst2->async_indexed_for_each(0));
    }

    void test_Perf(OP::utest::TestRuntime& tresult)
    {
        OP::utils::ThreadPool tp;
        std::shared_ptr<OP::vtm::AppendOnlyLog> a0l = OP::vtm::AppendOnlyLog::create_new(
            tp, test_file_name
            /*OS minimal allowed size for segment-size*/
        );
        constexpr double expected_value = 57.0;
        RandomRangeGenerator rand_range(tresult.randomizer(), 1000);
        const size_t test_size = 15 * (3 * a0l->segment_size() / sizeof(TestPayload)) / 2;

        std::vector<MyRange> all_ranges(test_size);

        std::generate_n(all_ranges.begin(), test_size, rand_range);
        auto [list_address, skplst] =
            OP::vtm::create_a0_skip_list<TestPayload, PayloadMinMaxIndexer, PayloadBloomIndexer>(a0l);
        size_t i = 0;
        unsigned min_left = std::numeric_limits<unsigned>::max(),
            max_right = std::numeric_limits<unsigned>::min();
        for (const auto& r : all_ranges)
        {
            auto payload = TestPayload(r, 0, i++, expected_value);
            skplst->emplace(std::move(payload));
            min_left = std::min(min_left, r.pos());
            max_right = std::max(max_right, r.right());
        }
        MyRange median((max_right - min_left) / 2, 1);
        std::array<MyRange, 3> check_probes{
            MyRange(min_left, 1), 
            MyRange(max_right - 1, 2),
            MyRange(max_right + 1, 1)
        };
        auto run_test_scan_by_sequence = [&](auto range) {
            size_t checker = 0;
            for (decltype(auto) tst : range)
            {
                for (const auto& probe : check_probes) // just to perform
                    checker += tst._key.is_overlapped(probe)?1:0;
            }
            return checker;
            };

        size_t run1 = 0, run2 = 0;
        auto no_mt_ms = tresult.measured_run([&]() {
            run1 = run_test_scan_by_sequence(skplst->indexed_for_each(median));
            }, 20);
        auto mt_ms = tresult.measured_run([&]() {
            run2 = run_test_scan_by_sequence(skplst->async_indexed_for_each(median));
            }, 20);
        tresult.assert_that<equals>(run1, run2);
        tresult.info() << "Measured run("<<run1<<") for Non-mt indexed scan = " << no_mt_ms
            << "\nMeasured run("<<run2<<") for mt indexed scan = " << mt_ms << "\n";
        //---
        run1 = 0, run2 = 0;
        
        no_mt_ms = tresult.measured_run([&]() {
            run1 = 0;
            skplst->indexed_for_each(median, [&](const auto& payload) {
                for (const auto& probe : check_probes) // just to perform
                    run1 += payload._key.is_overlapped(probe) ? 1 : 0;
                });
            }, 20);
        mt_ms = tresult.measured_run([&]() {
            run2 = 0;
            skplst->indexed_for_each(median, [&](const auto& payload) {
                for (const auto& probe : check_probes) // just to perform
                    run2 += payload._key.is_overlapped(probe) ? 1 : 0;
                });
            }, 20);
        tresult.assert_that<equals>(run1, run2);
        tresult.info() << "Measured run(" << run1 << ") for callback Non-mt indexed scan = " << no_mt_ms
            << "\nMeasured run(" << run2 << ") for callback mt indexed scan = " << mt_ms << "\n";

    }
    static auto& module_suite = OP::utest::default_test_suite("vtm.AppendOnlySkipList")
        .declare("emplace", test_Emplace)
        .declare("new_segment", test_NewSegment)
        .declare("poly_list", test_PolyList)
        .declare("perf", test_Perf)
        ;
} //ns:
