#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/common/DependencyManager.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/managers/EventSourcingSegmentManager.h>
#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/vtm/managers/InMemMemoryChangeHistory.h>

#include <op/trie/StringMemoryManager.h>
#include <op/trie/servants/TrieOptionsImpl.h>

#include <set>
#include <cassert>
#include <iterator>

#include "../vtm/MemoryChangeHistoryFixture.h"

namespace
{
    using namespace OP::trie;
    using namespace OP::vtm;
    using namespace OP::utest;
    using namespace OP::common;

    static const char* test_file_name = "StringManager.test";

    void test_StringManager(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;
        using str_manager_t = StringMemoryManager<>;

        OP::common::ServiceRegistry registry;

        OP::utils::ThreadPool threads;
        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);

        str_manager_t str_manager(toi);

        auto tran = toi.transaction_service().begin_transaction();
        auto a1 = str_manager.smart_insert(std::string("abc"));
        tran->commit();

        std::string result;
        str_manager.get(a1, std::back_insert_iterator(result));
        tresult.assert_that<equals>(result, "abc"s);

        result.clear();
        str_manager.get(a1, std::back_insert_iterator(result), 1, 2);
        tresult.assert_that<equals>(result, "bc"s);
        result.clear();
        str_manager.get(a1, std::back_insert_iterator(result), 1, 1);
        tresult.assert_that<equals>(result, "b"s);
        result.clear();
        str_manager.get(a1, std::back_insert_iterator(result), 1, 12);
        tresult.assert_that<equals>(result, "bc"s);
    }

    struct BigStringEmulatorIterator
    {
        size_t _pos;
        BigStringEmulatorIterator(size_t pos) :
            _pos(pos) {
        }

        char operator * () const
        {
            return '+';
        }
        BigStringEmulatorIterator& operator ++()
        {
            ++_pos;
            return *this;
        }
        BigStringEmulatorIterator operator ++(int)
        {
            return BigStringEmulatorIterator(_pos++);
        }
        std::int64_t operator - (BigStringEmulatorIterator other) const
        {
            return static_cast<std::int64_t>(_pos - other._pos);
        }
        bool operator == (const BigStringEmulatorIterator other) const
        {
            return _pos == other._pos;
        }
        bool operator != (const BigStringEmulatorIterator other) const
        {
            return _pos != other._pos;
        }
    };
    struct BigStringEmulator
    {
        size_t _emulate_size;

        BigStringEmulator(size_t emulate_size)
            : _emulate_size(emulate_size)
        {
        }

        BigStringEmulatorIterator begin() const
        {
            return BigStringEmulatorIterator(0);
        }

        BigStringEmulatorIterator end() const
        {
            return BigStringEmulatorIterator(_emulate_size);
        }
    };

    void string_manager_edge_case(TrieOptions& topology, OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;
        using str_manager_t = StringMemoryManager<>;

        str_manager_t str_manager(topology);

        std::string result;

        auto tran = topology.transaction_service().begin_transaction();
        auto a0 = str_manager.smart_insert(std::string{});
        str_manager.get(a0, std::back_insert_iterator(result), 1024, 2048);
        if(tran) tran->commit();

        tresult.assert_that<equals>(result, ""s);

        // @! At this stage there is no spec of string length, this exception should be managed
        // by memory alloc algorithm, not by string engine itself.
        // 
        //tresult.assert_exception<std::out_of_range>([&]() {
        //    OP::vtm::TransactionGuard g(topology.transaction_service().begin_transaction());
        //    str_manager.smart_insert(BigStringEmulator(std::numeric_limits<std::uint16_t>::max()));
        //    });
        std::string buffer;

        tran = topology.transaction_service().begin_transaction();
        auto a1 = str_manager.smart_insert(
            tools::RandomGenerator::instance().next_alpha_num(buffer, 4096, 4096));
        if (tran) tran->commit();

        str_manager.get(a1, std::back_insert_iterator(result));
        tresult.assert_that<equals>(result, buffer);
        result.clear();
        str_manager.get(a1, std::back_insert_iterator(result), 1024, 2048);
        tresult.assert_that<equals>(result, std::string(buffer.begin() + 1024, buffer.begin() + 2048 + 1024));

        
        tran = topology.transaction_service().begin_transaction();
        str_manager.destroy(a0);
        if (tran) tran->commit();

        tran = topology.transaction_service().begin_transaction();
        str_manager.destroy(a1);
        if (tran) tran->commit();

        tresult.info() << "random insert...\n";
        size_t destroy_idx = 0;
        std::unordered_map< typename str_manager_t::smart_str_address_t, std::string > model_reference;
        std::vector< typename str_manager_t::smart_str_address_t > allocated_strs;
        allocated_strs.reserve(1000);
        for (size_t i = 0; i < 1000; ++i)
        {
            OP::vtm::TransactionGuard g(topology.transaction_service().begin_transaction());
            auto rnd_str_addr = str_manager.smart_insert(
                tools::RandomGenerator::instance().next_alpha_num(buffer, 4096, str_manager_t::small_string_capacity_c + 1));
            g.commit();

            tresult.assert_true(model_reference.emplace(rnd_str_addr, buffer).second,
                "Non-unique addr allocated");
            allocated_strs.push_back(rnd_str_addr);
            if (i > 0 && !(i % 17)) //pseudo random de-alloc of previous string
            {//cicada rhythm
                typename str_manager_t::smart_str_address_t to_remove;
                std::swap(allocated_strs[destroy_idx++], to_remove);
                OP::vtm::TransactionGuard g(topology.transaction_service().begin_transaction());
                auto addr_copy = to_remove;
                str_manager.destroy(addr_copy);
                tresult.assert_true(addr_copy.is_nil(), "address wasn't invalidated");

                g.commit();
                tresult.assert_that<equals>(1, model_reference.erase(to_remove),
                    "Unknown address of persisted string");
            }
        }

        //check all strings persisted are valid
        for (const auto& to_check_addr : allocated_strs)
        {
            if (to_check_addr.is_nil()) //already erased
                continue;
            auto found = model_reference.find(to_check_addr);
            tresult.assert_that<not_equals>(found, model_reference.end(),
                "Something wrong - no such FarAddress in samples");
            result.clear();
            str_manager.get(to_check_addr, std::back_insert_iterator(result));
            tresult.assert_that<equals>(found->second, result,
                "String is not the same");
        }
        model_reference.clear(); //tear down
        //don't destroy str before `destroy_idx`
        allocated_strs.erase(allocated_strs.begin(), allocated_strs.begin() + destroy_idx);
        for (const auto& to_del : allocated_strs)
        {
            OP::vtm::TransactionGuard g(topology.transaction_service().begin_transaction());
            auto local = to_del;
            str_manager.destroy(local);
            g.commit();
            tresult.assert_true(local.is_nil());
        }
    }

    void test_StringManagerEdgeCase(OP::utest::TestRuntime& tresult,
        std::shared_ptr<test::ChangeHistoryFactory> mem_change_history)
    {
        using namespace std::string_literals;
        using str_manager_t = StringMemoryManager<>;

        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        string_manager_edge_case(toi, tresult);
    }

    void test_StringManagerEdgeCaseNoTran(OP::utest::TestRuntime& tresult)
    {

        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        string_manager_edge_case(toi, tresult);
    }

    void test_SmartStr(OP::utest::TestRuntime& tresult)
    {
        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        using str_manager_t = StringMemoryManager<>;
        str_manager_t smm(toi);
        auto& rndtool = tools::RandomGenerator::instance();


        for (size_t str_sz = 0; str_sz < sizeof(FarAddress) + 3; ++str_sz)
        {
            atom_string_t paste;
            if (str_sz)
                rndtool.next_alpha_num(paste, str_sz, str_sz);
            OP::vtm::TransactionGuard g(toi.transaction_service().begin_transaction(), true);

            auto smstr = smm.smart_insert(paste);
            for (OP::vtm::segment_pos_t offset = 0; offset <= (str_sz); ++offset)
            {
                for (OP::vtm::segment_pos_t len = 0; len < (str_sz + 2); ++len)
                {
                    auto sample = paste.substr(offset, len);
                    atom_string_t test_str;
                    smm.get(smstr, std::back_inserter(test_str), offset, len);
                    tresult.assert_that<equals>(test_str, sample);
                    test_str.clear();
                    smm.get(smstr, [&test_str](auto c)->bool {
                        test_str.append(1, c);
                        return true;
                        }, offset, len);
                    tresult.assert_that<equals>(test_str, sample);
                }
            }
            smm.destroy(smstr);
        }

    }
    
    void test_Truncate(OP::utest::TestRuntime& tresult)
    {
        
        OP::trie::servants::TrieOptionsImpl toi(test_file_name, OP::trie::OpenMode::create);
        using str_manager_t = StringMemoryManager<>;
        str_manager_t smm(toi);

        constexpr size_t test_limit_c = 512;

        const auto verify_expected = [&](const auto& smstr, const auto& expecting){
            atom_string_t test_str;
            smm.get(smstr, std::back_inserter(test_str));
            tresult.assert_that<equals>(test_str, expecting);
        };

        atom_string_t test_str;
        //sequentially increase string size from 0 to test_limit_c
        for(size_t i = 0; i < test_limit_c; ++i)
        {
            OP::vtm::TransactionGuard g(toi.transaction_service().begin_transaction(), true);
            auto smstr = smm.smart_insert(test_str);
            //test ultimate big cut
            smm.truncate(smstr, test_limit_c+1); 
            verify_expected(smstr, test_str);
            
            smstr = smm.smart_insert(test_str);
            for(size_t j = i;; --j)
            {
                smm.truncate(smstr, j); 
                verify_expected(smstr, OP::utils::subview<atom_string_view_t>(test_str, 0, j));
                if(j==0) break;
            }

            test_str += ' ';
            std::iota(test_str.begin(), test_str.end(), '0');
        }
    }

    static auto& module_suite = OP::utest::default_test_suite("trie.StringManager")
        .declare("basic", test_StringManager)
        .declare("edgecase-transactional", test_StringManagerEdgeCase)
        .declare("edgecase-no-tran", test_StringManagerEdgeCaseNoTran)
        .declare("smart-str", test_SmartStr)
        .declare("truncate", test_Truncate)
        // define scenario parameter with InMemory implementation
        .with_fixture("memory-only", test::memory_change_history_factory<test::InMemoryChangeHistoryFactory>)
        ;
} //ns:
