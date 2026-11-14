#include <map>
#include <any>
#include <bit>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/trie/BitIndexedVector.h>

#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>
#include <op/trie/servants/TrieOptionsImpl.h>

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

    struct TestParentInfo
    {
        constexpr static inline size_t presence_table_size_c = 4;//256/64
        FarAddress _hash_table{};
        using presence_element_t = std::uint64_t;
        presence_element_t _presence[presence_table_size_c]{};

        /** method alters bit matrix and target container together, to simulate TrieNode::insert */
        template <class BitIndexed, class K>
        KvInsert simulate_insert(BitIndexed& instance, K key, std::uintptr_t value)
        {
            if (OP::rawbits::get(_presence, key))
                return KvInsert::already_exists;
            KvInsert result = instance.insert(
                key, TestPayload::my_payload_factory, reinterpret_cast<void*>(value));
            if (result == KvInsert::ok)
                OP::rawbits::set(_presence, key);
            return result;
        }

        FarAddress reindex_table() const
        {
            return this->_hash_table;
        }
        
        fast_dim_t size() const
        {
            fast_dim_t result = 0;

            for (size_t i = 0; i < presence_table_size_c; ++i)
                result += std::popcount(_presence[i]);

            return result;
        }
        
        fast_dim_t presence(fast_dim_t key) const
        {
            return (OP::rawbits::get(_presence, key) ? Terminality::term_has_child : 0);
        }

        fast_dim_t presence_first_set() const
        {
            return OP::rawbits::first_any_set(_presence);
        }
        /**@return next position where child or value exists, may return dim_nil_c if no more entries*/
        inline fast_dim_t presence_next_set(atom_t previous) const
        {
            return OP::rawbits::next_any_set(previous, _presence);
        }

        inline fast_dim_t count_bits_before(atom_t key) const
        {
            constexpr fast_dim_t bits_c = std::numeric_limits<presence_element_t>::digits; //=64

            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            // calculate rank: how many bits are set before this one?
            size_t pos = 0;
            for (size_t i = 0; i < word_idx; ++i)
            {
                pos += std::popcount(_presence[i]);
            }
            pos += std::popcount(_presence[word_idx] & (bit_mask - 1));

            return static_cast<fast_dim_t>(pos);
        }
    };

    void test_default(OP::utest::TestRuntime& tresult)
    {
        using bit_index_t = OP::trie::containers::BitIndexedVector<4, TestPayload, TestParentInfo>;

        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        OP::vtm::TransactionGuard t_guard(toi.transaction_service().begin_transaction());
        TestParentInfo test_parent;
        bit_index_t new_instance(toi, test_parent);
        test_parent._hash_table = new_instance.create();
        
        // Insert out of order: 50, then 10, then 100
        tresult.assert_that<equals>(KvInsert::ok, test_parent.simulate_insert(new_instance, 50, 51));
        tresult.assert_that<equals>(KvInsert::ok, test_parent.simulate_insert(new_instance, 10, 11));
        tresult.assert_that<equals>(KvInsert::ok, test_parent.simulate_insert(new_instance, 100, 101));

        tresult.assert_that<equals>(KvInsert::ok, test_parent.simulate_insert(new_instance, 255, 256));
        //overflow
        tresult.assert_that<equals>(KvInsert::need_grow, test_parent.simulate_insert(new_instance, 0, 1));

        //tresult.assert_that<equals>(KvInsert::already_exists, call_insert(10, 12));
        
        auto result1 = new_instance.cget(50);
        tresult.assert_that<equals>(result1->x, 51 );
        auto result2 = new_instance.cget(10);
        tresult.assert_that<equals>(result2->x, 11);

        new_instance.destroy(test_parent._hash_table);
    }

    void test_reopen(OP::utest::TestRuntime& tresult)
    {
        using bit_index_t = OP::trie::containers::BitIndexedVector<4, TestPayload, TestParentInfo>;

        TestParentInfo test_parent;
        segment_pos_t after_create_footprint;
        {//scope of new instance
            OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
            OP::vtm::TransactionGuard t_guard(toi.transaction_service().begin_transaction(), true);

            bit_index_t new_instance(toi, test_parent);
            test_parent._hash_table = new_instance.create();


            tresult.assert_that<equals>(KvInsert::ok,
                test_parent.simulate_insert(new_instance, 50, 51) );
            tresult.assert_that<equals>(KvInsert::ok,
                test_parent.simulate_insert(new_instance, 10, 11));

            auto result1 = new_instance.cget(10);
            tresult.assert_that<equals>(result1->x, 11);

            tresult.assert_true(new_instance.erase(10));
        } //new instace scope

        // re-open
        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::open);
        OP::vtm::TransactionGuard t_guard(toi.transaction_service().begin_transaction(), true);

        bit_index_t new_instance(toi, test_parent);

        tresult.assert_that<equals>(KvInsert::already_exists,
            test_parent.simulate_insert(new_instance, 50, 52));
        tresult.assert_that<equals>(KvInsert::ok,
            test_parent.simulate_insert(new_instance, 100, 101));

        auto result1 = new_instance.cget(50);
        tresult.assert_that<equals>(result1->x, 51);

        new_instance.destroy(test_parent._hash_table);
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(13s);
    }

    template <size_t N>
    using indexing_table_t = OP::trie::containers::BitIndexedVector<N, TestPayload, TestParentInfo>;

    void test_grow(OP::utest::TestRuntime& tresult)
    {

        using key_value_t = containers::KeyValueContainer<TestPayload, TestParentInfo>;
        using wrap_key_value_t = OP::Multiimplementation<
            key_value_t,
            indexing_table_t<4>, indexing_table_t<8>, indexing_table_t<16>,
            indexing_table_t<32>, indexing_table_t<64>, indexing_table_t<128>
        >;

        OP::trie::servants::TrieOptionsImpl toi(
            test_file_name, OpenMode::create);
        OP::vtm::TransactionGuard t_guard(toi.transaction_service().begin_transaction());

        TestParentInfo test_parent{};

        
        auto make_new_instance = [&](size_t capacity, wrap_key_value_t& result) {

            switch (capacity)
            {
            case 4:
                result.template construct<indexing_table_t<4>>(toi, test_parent);
                break;
            case 8:
                result.template construct<indexing_table_t<8>>(toi, test_parent);
                break;
            case 16:
                result.template construct<indexing_table_t<16>>(toi, test_parent);
                break;
            case 32:
                result.template construct<indexing_table_t<32>>(toi, test_parent);
                break;
            case 64:
                result.template construct<indexing_table_t<64>>(toi, test_parent);
                break;
            default:
                assert(false);
                [[fallthrough]]; //for release build
            case 128:
                result.template construct<indexing_table_t<128>>(toi, test_parent);
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
        test_parent._hash_table = dut->create();

        for (const auto& v : samples)
        {
            auto ins_res = test_parent.simulate_insert(*dut, v, static_cast<std::uintptr_t>(v));
            tresult.assert_that<negate<equals>>(KvInsert::already_exists, ins_res);
            if (ins_res == KvInsert::need_grow)
            { //simulate several grows
                current_capacity <<= 1;
                wrap_key_value_t new_instance;
                make_new_instance(current_capacity, new_instance);
                auto new_address = new_instance->create_from(*dut);
                tresult.assert_false(new_address.is_nil());
                dut->destroy(test_parent._hash_table);
                test_parent._hash_table = new_address;
                dut = std::move(new_instance);

                //at last insert the value
                ins_res = test_parent.simulate_insert(*dut, v, static_cast<std::uintptr_t>(v));
                tresult.assert_that<equals>(KvInsert::ok, ins_res);
            }
        }

        for (const auto v : samples)
        {
            auto good_result = dut->cget(v);
            tresult.assert_that<equals>(good_result->x, v);
        }
        t_guard.commit();
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(2s);
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

        OP::trie::servants::TrieOptionsImpl toi(
            test_file_name, OpenMode::create);

        auto measure_bit_vector = tresult.measured_run([&]() {
            auto tran_scope = toi.transaction_service().begin_transaction();
            TestParentInfo test_parent{};
            indexing_table_t<128> bit_vec(toi, test_parent);
            test_parent._hash_table = bit_vec.create();
            for (int i = 0; i < runs_c; ++i)
            {
                // Insert 111 unique elements repeatedly
                test_parent.simulate_insert(bit_vec, keys[i % 111], static_cast<std::uintptr_t>(i));
            }
            tran_scope->commit();
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

        tresult.info() << "Benchmark of bit indexed vector = " << measure_bit_vector << " ms,"
            << "\nBenchmark of ordered vector = "
            << measure_std_vector << " ms.\n";
        //tresult.assert_that<less>(measure_bit_vector, measure_std_vector,
        //    "Performance of BitVector lower than ordered vector, no sense to use it..."
        //);
    }

    static auto& module_suite = OP::utest::default_test_suite("Trie.bit_indexed")
        .declare("default", test_default)
        .declare("reopen", test_reopen)
        .declare("grow", test_grow)
        .declare("benchmark", benchmark_container, "benchmark", "slow")
        ;

}//ns:
