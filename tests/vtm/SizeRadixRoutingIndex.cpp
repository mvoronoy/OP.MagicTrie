#include <latch>

#include <op/utest/unit_test.h>
#include <op/trie/Trie.h>

#include <op/common/ThreadPool.h>
#include <op/common/DependencyManager.h>
#include <op/common/Currying.h>
#include <op/common/FixedString.h>

#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/vtm/AppendOnlyMemoryPool.h>
#include <op/vtm/datastructure/SizeRadixRoutingIndex.h>

namespace {

    using namespace OP::utest;
    constexpr inline std::uint32_t unique_marker_c = 0x01234567;
    struct SimplePayload {
        ~SimplePayload()
        {
            _uid = ~_uid;
        }
        std::uint32_t _uid{ unique_marker_c };
        OP::vtm::FarAddress _next{};
        friend std::ostream& operator << (std::ostream& os, const SimplePayload& value)
        {
            os << "payload:{uid=" << value._uid << ", next=0x" << std::hex << value._next.address << "}";
            return os;
        }
    };


    void test_Create(OP::utest::TestRuntime& result)
    {
        using namespace OP::vtm;
        const char seg_file_name[] = "vtm.test";

        OP::common::ServiceRegistry registry;

        OP::utils::ThreadPool threads;
        auto sm = BaseSegmentManager::create_new(seg_file_name,
            SegmentOptions().segment_size(0x110000));
        auto& dep_graph = registry.resolve_dependencies(
            std::ref(threads),
            std::ref(*sm),
            std::make_unique<AppendOnlyMemoryPool>()
        );

        auto& pool = dep_graph.get<AppendOnlyMemoryPool>();
        auto alloc1 = pool.allocate(100);

        auto tree = SizeRadixRoutingIndex<SimplePayload>(*sm, pool);
        auto reuse_res = tree.claim(100, true);
        result.assert_true(reuse_res._block_address.is_nil() && reuse_res._block == nullptr,
            "nothing to claim, result must be nil"
        );
        result.assert_that<equals>(tree.add_block(alloc1, 100), decltype(tree)::AddStatus::ok);
        result.assert_that<equals>(sm->view<SimplePayload>(alloc1)->_uid, unique_marker_c,
            "adding new block means creation of SimplePayload, but signature doesn't matches"
        );
        reuse_res = tree.claim(100, true);
        result.assert_true(!reuse_res._block_address.is_nil() && reuse_res._block != nullptr);
        result.assert_that<equals>(
            sm->view<SimplePayload>(reuse_res._block_address)->_uid,
            ~unique_marker_c,
            "`claim` means ~SimplePayload been called, but signature doesn't matches"
        );

        constexpr OP::vtm::segment_pos_t large_block_c = 1ul << 16;// 2^2^2^2
        auto large = pool.allocate(large_block_c);
        std::unordered_map<FarAddress, segment_pos_t> addr2size;
        //chop big chunk
        segment_pos_t debug_sum = 0;
        auto chop_addr = large;
        for (segment_pos_t chop_size = large_block_c >> 1;
            chop_size >= OP::vtm::memory_requirement<SimplePayload>::requirement;
            chop_size >>= 1)
        {
            debug_sum += chop_size;
            result.assert_that<equals>(tree.add_block(chop_addr, chop_size), decltype(tree)::AddStatus::ok);
            auto [pos, succ] = addr2size.emplace(chop_addr, chop_size);
            result.assert_true(succ, "non-unique address");
            chop_addr = chop_addr + chop_size;
        }
        //tree.dump();
        //std::cout << "====================\n";
        //tree.add_block(alloc1, 100);
        //tree.dump();
        debug_sum = 0;
        constexpr segment_pos_t smallest_c = 8 * ((OP::vtm::memory_requirement<SimplePayload>::requirement + 8 - 1) / 8);
        for (segment_pos_t chop_size = smallest_c; chop_size < large_block_c; chop_size <<= 1)
        {
            debug_sum += chop_size;
            auto claim_result = tree.claim(chop_size, true);
            result.assert_that<equals>(claim_result._block_size, chop_size);
            auto sample_iter = addr2size.find(claim_result._block_address);
            result.assert_that<not_equals>(sample_iter, addr2size.end());
            result.assert_that<equals>(sample_iter->second, chop_size);
        }
        //second run of same sizes must fail
        for (segment_pos_t chop_size = smallest_c; chop_size < large_block_c; chop_size <<= 1)
        {
            auto claim_result = tree.claim(chop_size, true);
            result.assert_true(claim_result._block_address.is_nil());
        }
    }

    void test_LowerBound(OP::utest::TestRuntime& result)
    {
        using namespace OP::vtm;
        const char seg_file_name[] = "vtm.test";

        OP::common::ServiceRegistry registry;

        OP::utils::ThreadPool threads;
        auto sm = BaseSegmentManager::create_new(seg_file_name,
            SegmentOptions().segment_size(0x110000));
        auto& dep_graph = registry.resolve_dependencies(
            std::ref(threads),
            std::ref(*sm),
            std::make_unique<AppendOnlyMemoryPool>()
        );

        auto& pool = dep_graph.get<AppendOnlyMemoryPool>();

        auto tree = SizeRadixRoutingIndex<SimplePayload>(*sm, pool);

        constexpr OP::vtm::segment_pos_t large_block_c = 1ul << 16;// 2^2^2^2
        auto large = pool.allocate(large_block_c);

        std::unordered_map<FarAddress, segment_pos_t> addr2size;
        //chop big chunk
        auto [pos, succ] = addr2size.emplace(large, large_block_c);
        //single big block will be chopped by sequential querying of `claim`
        result.assert_that<equals>(tree.add_block(large, large_block_c), decltype(tree)::AddStatus::ok);
        //tree.dump();
        constexpr segment_pos_t smallest_c = 8 * ((OP::vtm::memory_requirement<SimplePayload>::requirement + 8 - 1) / 8);
        segment_pos_t allocated_sum = large_block_c;

        std::uint8_t unique_marker = 57;
        std::unordered_map<std::uint8_t, std::pair<std::uint8_t*, segment_pos_t>> mem_blocks_to_check_on_consistency;
        for (segment_pos_t chop_size = smallest_c; chop_size < large_block_c; chop_size <<= 1, ++unique_marker)
        {
            auto claim_result = tree.claim(chop_size, false);
            result.assert_false(claim_result._block_address.is_nil());
            //populate new block with unique number to validate later that add_block not break the boundary
            memset(claim_result._block, unique_marker, chop_size);
            //result.assert_that<greater>(claim_result._block_size, chop_size);
            result.assert_that<equals>(claim_result._block_size, allocated_sum);
            allocated_sum -= chop_size;
            //split result large block
            result.assert_that<equals>(
                tree.add_block(claim_result._block_address + chop_size, allocated_sum),
                decltype(tree)::AddStatus::ok
            );
            mem_blocks_to_check_on_consistency.emplace(
                unique_marker,
                std::make_pair(claim_result._block, chop_size)
            );
        }
        //check boundary breaking
        for (const auto& [c, claim] : mem_blocks_to_check_on_consistency)
        {
            for (segment_pos_t offset = 0; offset < claim.second; ++offset)
            {
                result.assert_that<equals>(claim.first[offset], c);
            }
        }
    }
    
    void test_MT(OP::utest::TestRuntime& result)
    {
        using namespace OP::vtm;
        constexpr OP::vtm::segment_pos_t large_block_c = 1ul << 16;// 2^2^2^2
        constexpr OP::vtm::segment_pos_t test_blocks_c = 0x40;
        constexpr OP::vtm::segment_pos_t segment_size_c = large_block_c * 0x20; //intentionally less than large_block_c * test_blocks_c
        constexpr OP::vtm::segment_pos_t min_block_c = sizeof(SimplePayload);

        OP::common::ServiceRegistry registry;

        OP::utils::ThreadPool threads;
        const char seg_file_name[] = "mt-vtm.test";
        auto sm = BaseSegmentManager::create_new(seg_file_name,
            SegmentOptions().segment_size(segment_size_c));
        auto& dep_graph = registry.resolve_dependencies(
            std::ref(threads),
            std::ref(*sm),
            std::make_unique<AppendOnlyMemoryPool>()
        );

        auto& pool = dep_graph.get<AppendOnlyMemoryPool>();

        auto tree = SizeRadixRoutingIndex<SimplePayload, large_block_c>(*sm, pool);

        std::atomic<std::int64_t> target_sum = 0;
        std::latch synchro_start{ 1 };

        auto large = pool.allocate(large_block_c); //make first allocation
        result.assert_that<equals>(decltype(tree)::AddStatus::ok, tree.add_block(large, large_block_c));

        const auto thread_proc = [&](segment_pos_t begin, segment_pos_t end) {
            synchro_start.wait();
            while (begin < end)
            {
                segment_pos_t n = static_cast<segment_pos_t>(end - begin);
                //n = (n > 8) ? (n / 2) : n;
                segment_pos_t buffer_size = OP::utils::align_on(static_cast<segment_pos_t>(n * sizeof(segment_pos_t)), min_block_c);

                auto claim_result = tree.claim(buffer_size, false);
                if (!claim_result)
                {
                    auto r = tree.add_block(pool.allocate(large_block_c), large_block_c);
                    result.assert_that<equals>(
                        decltype(tree)::AddStatus::ok,
                        r
                    );
                    continue; //give another try
                }
                else if (auto rest = claim_result._block_size - buffer_size; rest >= min_block_c)
                { // can chop and return part for other use
                    result.assert_that<equals>(
                        decltype(tree)::AddStatus::ok,
                        tree.add_block(
                            claim_result._block_address + buffer_size,
                            rest));
                }

                segment_pos_t* first = reinterpret_cast<segment_pos_t*>(claim_result._block);
                std::iota(first, first + n, static_cast<segment_pos_t>(begin));
                begin += n;
                for (size_t i = 0; i < n; ++i)
                    target_sum.fetch_add(first[i], std::memory_order_relaxed);
                //free buffer
                result.assert_that<equals>(
                    decltype(tree)::AddStatus::ok,
                    tree.add_block(
                        claim_result._block_address,
                        buffer_size));
            }


            };
        std::vector<std::future<void>> jobs;
        constexpr segment_pos_t limit_c = large_block_c * test_blocks_c;
        constexpr std::uint64_t expected_value = static_cast<std::uint64_t>(limit_c) * (limit_c - 1) / 2;

        //        OP::utils::ThreadPool small_tp(1);
        for (int i = 0; i < limit_c;)
        {
            segment_pos_t rnd = min_block_c + min_block_c * result.randomizer().next_in_range<segment_pos_t>(0, 0x10);
            segment_pos_t step = std::min(rnd, limit_c - i);
            jobs.emplace_back(threads.async(
                thread_proc, i, i + step
            ));
            i += step;
        }
        synchro_start.count_down();
        for (auto& j : jobs)
            j.get();
        result.assert_that<equals>(
            target_sum,
            expected_value
        );

    }
    
    void test_add_multiple(OP::utest::TestRuntime& result)
    {
        using namespace OP::vtm;
        const char seg_file_name[] = "vtm.test";

        OP::common::ServiceRegistry registry;

        OP::utils::ThreadPool threads;
        auto sm = BaseSegmentManager::create_new(seg_file_name,
            SegmentOptions().segment_size(0x110000));
        auto& dep_graph = registry.resolve_dependencies(
            std::ref(threads),
            std::ref(*sm),
            std::make_unique<AppendOnlyMemoryPool>()
        );

        auto& pool = dep_graph.get<AppendOnlyMemoryPool>();

        struct MinimalPayload
        {
            FarAddress _next{};
        };

        constexpr OP::vtm::segment_pos_t min_block_c = 8;//sizeof(MinimalPayload)
        constexpr OP::vtm::segment_pos_t max_block_c = 1024;
        auto tree = SizeRadixRoutingIndex<MinimalPayload, max_block_c>(*sm, pool);

        auto large = pool.allocate(max_block_c);
        auto chunk_maker = OP::flur::src::of_iota(large, large + max_block_c, min_block_c);

        //chop big chunk
        result.assert_that<equals>(
            tree.add_multiple_blocks(min_block_c, chunk_maker),
            decltype(tree)::AddStatus::ok);
        //check blocks availability
        size_t n = 0;
        for (;; ++n)
        {
            auto claim_result = tree.claim(min_block_c, true/*strong block size*/);
            if (!claim_result)
                break;
            result.assert_that<equals>(claim_result._block_size, min_block_c);
        }
        result.assert_that<equals>(n, max_block_c / min_block_c);
    }


    static auto& module_suite = OP::utest::default_test_suite("radix-routing-index")
        .declare("create", test_Create)
        .declare("claim_lower_bound", test_LowerBound)
        .declare("mt", test_MT)
        .declare("add_multiple", test_add_multiple)
        ;

} //ns:
