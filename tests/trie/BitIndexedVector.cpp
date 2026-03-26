#include <map>
#include <any>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/trie/BitIndexedVector.h>

#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>

#include <op/common/astr.h>
#include <op/common/StackAlloc.h>

#include "TrieTestUtils.h"

namespace
{
    using namespace OP::trie;
    using namespace OP::vtm;
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace OP::common;
    using namespace OP::trie::containers;

    const char* test_file_name = "bit-index.test";

    struct TestPayload
    {
        std::uintptr_t x = 0;

        static void my_payload_factory(TestPayload& to_construct, void* user_data)
        {
            to_construct = TestPayload{ reinterpret_cast<std::uintptr_t>(user_data) };
        }
    };

    struct DummyParentInfo
    {
    };

    void test_default(OP::utest::TestRuntime& tresult)
    {
        using bit_index_t = OP::trie::containers::BitIndexedVector<4, TestPayload, DummyParentInfo>;

        std::shared_ptr<SegmentManager> segment_manager(
            BaseSegmentManager::create_new(
                test_file_name, OP::vtm::SegmentOptions().segment_size(0x110000))
        );

        SegmentTopology<HeapManagerSlot> mngr_topology(segment_manager);

        auto& heap_manager = mngr_topology.slot<HeapManagerSlot>();
        auto initial_footprint = heap_manager.available(0);
        tresult.debug() << "Initial persisted heap footprint = 0x" << initial_footprint << "\n";

        bit_index_t new_instance(mngr_topology);
        auto new_address = new_instance.create();
        auto after_create_footprint = heap_manager.available(0);
        tresult.debug() << "Footprint after create = 0x" << std::hex << after_create_footprint << "\n";
        tresult.assert_that<less>(after_create_footprint, initial_footprint);
        
        // Insert out of order: 50, then 10, then 100
        tresult.assert_that<equals>(KvInsert::ok,
            new_instance.insert(50, TestPayload::my_payload_factory, reinterpret_cast<void*>(51)));
        tresult.assert_that<equals>(KvInsert::ok,
            new_instance.insert(10, TestPayload::my_payload_factory, reinterpret_cast<void*>(11)));
        tresult.assert_that<equals>(KvInsert::ok,
            new_instance.insert(100, TestPayload::my_payload_factory, reinterpret_cast<void*>(101)));

        tresult.assert_that<equals>(KvInsert::ok,
            new_instance.insert(255, TestPayload::my_payload_factory, reinterpret_cast<void*>(256)));
        //overflow
        tresult.assert_that<equals>(KvInsert::need_grow,
            new_instance.insert(0, TestPayload::my_payload_factory, reinterpret_cast<void*>(1)));

        tresult.assert_that<equals>(KvInsert::already_exists,
            new_instance.insert(10, TestPayload::my_payload_factory, reinterpret_cast<void*>(12)));

        auto good_result = new_instance.cget(50);
        tresult.assert_that<equals>(good_result->x, 51 );
        good_result = new_instance.cget(10);
        tresult.assert_that<equals>(good_result->x, 11 );

        auto failed_result = new_instance.cget(0); //there was attempt to add
        tresult.assert_false(failed_result.has_value());
        failed_result = new_instance.cget(200);
        tresult.assert_false(failed_result.has_value());

        new_instance.destroy(new_address);
        auto after_destroy_footprint = heap_manager.available(0);
        tresult.debug() << "Footprint after destroy = 0x" << std::hex << after_destroy_footprint << "\n";
        tresult.assert_that<less>(after_create_footprint, after_destroy_footprint);
    }

    void test_reopen(OP::utest::TestRuntime& tresult)
    {
        using bit_index_t = OP::trie::containers::BitIndexedVector<4, TestPayload, DummyParentInfo>;
        FarAddress container_residence;
        segment_pos_t after_create_footprint;
        {//scope of new instance
            std::shared_ptr<SegmentManager> segment_manager(
                BaseSegmentManager::create_new(
                    test_file_name, OP::vtm::SegmentOptions().segment_size(0x110000))
            );

            SegmentTopology<HeapManagerSlot> mngr_topology(segment_manager);

            auto& heap_manager = mngr_topology.slot<HeapManagerSlot>();
            auto initial_footprint = heap_manager.available(0);
            tresult.debug() << "Initial persisted heap footprint = 0x" << initial_footprint << "\n";

            bit_index_t new_instance(mngr_topology);
            container_residence = new_instance.create();
            after_create_footprint = heap_manager.available(0);
            tresult.debug() << "Footprint after create = 0x" << std::hex << after_create_footprint << "\n";
            tresult.assert_that<less>(after_create_footprint, initial_footprint);

            tresult.assert_that<equals>(KvInsert::ok,
                new_instance.insert(50, TestPayload::my_payload_factory, reinterpret_cast<void*>(51)));
            tresult.assert_that<equals>(KvInsert::ok,
                new_instance.insert(10, TestPayload::my_payload_factory, reinterpret_cast<void*>(11)));
            auto good_result = new_instance.cget(10);
            tresult.assert_that<equals>(good_result->x, 11);

            tresult.assert_true(new_instance.erase(10));
        } //new instace scope
        
        // re-open
        std::shared_ptr<SegmentManager> segment_manager(
            BaseSegmentManager::open(test_file_name)
        );
        SegmentTopology<HeapManagerSlot> mngr_topology(segment_manager);

        auto& heap_manager = mngr_topology.slot<HeapManagerSlot>();
        auto initial_footprint = heap_manager.available(0);
        tresult.debug() << "Initial persisted heap footprint = 0x" << initial_footprint << "\n";

        bit_index_t new_instance(mngr_topology, container_residence);
        tresult.assert_that<equals>(after_create_footprint, heap_manager.available(0));

        tresult.assert_that<equals>(KvInsert::already_exists,
            new_instance.insert(50, TestPayload::my_payload_factory, reinterpret_cast<void*>(52)));
        tresult.assert_that<equals>(KvInsert::ok,
            new_instance.insert(100, TestPayload::my_payload_factory, reinterpret_cast<void*>(101)));

        auto good_result = new_instance.cget(50);
        tresult.assert_that<equals>(good_result->x, 51);

        auto failed_result = new_instance.cget(10); //there was attempt to add
        tresult.assert_false(failed_result.has_value());
        failed_result = new_instance.cget(200);
        tresult.assert_false(failed_result.has_value());

        new_instance.destroy(container_residence);
        auto after_destroy_footprint = heap_manager.available(0);
        tresult.debug() << "Footprint after destroy = 0x" << std::hex << after_destroy_footprint << "\n";
        tresult.assert_that<less>(after_create_footprint, after_destroy_footprint);
    }

    template <size_t N>
    using indexing_table_t = OP::trie::containers::BitIndexedVector<N, TestPayload, DummyParentInfo>;

    void test_grow(OP::utest::TestRuntime& tresult)
    {
        using key_value_t = containers::KeyValueContainer<TestPayload, DummyParentInfo>;
        using wrap_key_value_t = OP::Multiimplementation<
            key_value_t, 
            indexing_table_t<4>, indexing_table_t<8>, indexing_table_t<16>,
            indexing_table_t<32>, indexing_table_t<64>, indexing_table_t<128>
        >;

        std::shared_ptr<SegmentManager> segment_manager(
            BaseSegmentManager::create_new(
                test_file_name, OP::vtm::SegmentOptions().segment_size(0x110000))
        );

        SegmentTopology<HeapManagerSlot> topology(segment_manager);
        indexing_table_t<4> xxx(topology);

        auto make_new_instance = [&](size_t capacity, wrap_key_value_t& result){

            switch (capacity)
            {
            case 4:
                result.template construct<indexing_table_t<4>>(topology, FarAddress{});
                break;
            case 8:
                result.template construct<indexing_table_t<8>>(topology);
                break;
            case 16:
                result.template construct<indexing_table_t<16>>(topology);
                break;
            case 32:
                result.template construct<indexing_table_t<32>>(topology);
                break;
            case 64:
                result.template construct<indexing_table_t<64>>(topology);
                break;
            default:
                assert(false);
                [[fallthrough]]; //for release build
            case 128:
                result.template construct<indexing_table_t<128>>(topology);
                break;
            };
        };
        

        std::vector<int> samples(127, 'x');
        std::iota(samples.begin(), samples.end(), 0);
        std::shuffle(samples.begin(), samples.end(),
            tresult.randomizer().generator());
        size_t current_capacity = 4;
        wrap_key_value_t dut;
        make_new_instance(current_capacity, dut);
        auto last_address = dut->create();
        for (const auto& v : samples)
        {
            auto ins_res = dut->insert(v, TestPayload::my_payload_factory, 
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(v)));
            tresult.assert_that<negate<equals>>(KvInsert::already_exists, ins_res);
            if (ins_res == KvInsert::need_grow)
            { //simulate several grows
                current_capacity <<= 1;
                wrap_key_value_t new_instance;
                make_new_instance(current_capacity, new_instance);
                auto new_address = new_instance->create();
                tresult.assert_true(new_instance->grow_from(*dut));
                dut->destroy(last_address);
                last_address = new_address;
                dut = std::move(new_instance);
                
                //at last insert the value
                ins_res = dut->insert(v, TestPayload::my_payload_factory,
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(v)));
                tresult.assert_that<equals>(KvInsert::ok, ins_res);
            }
        }

        for (const auto v : samples)
        {
            auto good_result = dut->cget(v);
            tresult.assert_true(good_result.has_value());
            tresult.assert_that<equals>(good_result->x, v);
        }
    }

    /** compare performance of BitVector against same sorted std::vector */
    void benchmark_container(OP::utest::TestRuntime& tresult)
    {
        constexpr size_t max_key_c = 256;
        constexpr size_t runs_c = 1000;
        constexpr size_t measurements_c = 1500;

        std::vector<uint16_t> keys(max_key_c);
        std::iota(keys.begin(), keys.end(), 0);
        std::shuffle(keys.begin(), keys.end(), tresult.randomizer().generator());

        std::shared_ptr<SegmentManager> segment_manager(
            BaseSegmentManager::create_new(
                test_file_name, OP::vtm::SegmentOptions().segment_size(0x110000))
        );

        SegmentTopology<HeapManagerSlot> mngr_topology(segment_manager);
        auto measure_bit_vector = tresult.measured_run([&]() {
            indexing_table_t<128> bit_vec(mngr_topology);
            std::ignore = bit_vec.create();
            for (int i = 0; i < runs_c; ++i) 
            {
                // Insert 111 unique elements repeatedly
                bit_vec.insert(keys[i % 111], TestPayload::my_payload_factory, 
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(i)));
            }
            }, measurements_c);

        auto measure_std_vector = tresult.measured_run([&]() {
            std::vector<std::pair<uint16_t, TestPayload>> ord_vec;
            for (int i = 0; i < runs_c; ++i)
            {
                uint16_t key = keys[i % 111];
                auto it = std::lower_bound(ord_vec.begin(), ord_vec.end(), key,
                    [](const auto& pair, uint16_t k) { return pair.first < k; });

                if (it == ord_vec.end() || it->first != key) {
                    ord_vec.insert(it, { key, TestPayload{static_cast<std::uintptr_t>(i)} });
                }
            }}, measurements_c);
        
        tresult.debug() << "Benchmark of bit indexed vector = " << measure_bit_vector << " ms,"
            << "\nBenchmark of ordered vector = " 
            << measure_std_vector << " ms.\n";
        tresult.assert_that<less>(measure_bit_vector, measure_std_vector,
            "Performance of BitVector lower than ordered vector, no sense to use it..."
        );
    }

    static auto& module_suite = OP::utest::default_test_suite("Trie.bitindexed")
        .declare("default", test_default)
        .declare("reopen", test_reopen)
        .declare("grow", test_grow)
        .declare("benchmark", benchmark_container, "benchmark", "slow")
       ;

}//ns:
