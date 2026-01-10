#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/StringMemoryManager.h>
#include <op/vtm/EventSourcingSegmentManager.h>
#include <op/vtm/InMemMemoryChangeHistory.h>

#include <set>
#include <cassert>
#include <iterator>

namespace
{
    using namespace OP::vtm;
    using namespace OP::utest;
    using namespace OP::common;

    static const char* node_file_name = "StringManager.test";

    void test_StringManager(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;
        using str_manager_t = OP::vtm::StringMemoryManager<>;

        auto tmngr1 = SegmentManager::template create_new<EventSourcingSegmentManager>(
            node_file_name,
            OP::vtm::SegmentOptions()
                .segment_size(0x110000),
            std::shared_ptr<MemoryChangeHistory>(new InMemoryChangeHistory)
        );

        SegmentTopology<
            HeapManagerSlot
        > mngr_toplogy(tmngr1);
        str_manager_t str_manager(mngr_toplogy);

        auto tran = tmngr1->begin_transaction();
        auto a1 = str_manager.insert(std::string("abc"));
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

    template <class TSegmentTopology>
    void string_manager_edge_case(TSegmentTopology& topology, OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;
        using str_manager_t = OP::vtm::StringMemoryManager<>;

        str_manager_t str_manager(topology);
        auto& heap_mngr = topology.template slot<HeapManagerSlot>();
        auto start_avail_size = heap_mngr.available(0);
        std::string result;


        auto tran = OP::vtm::resolve_segment_manager(topology).begin_transaction();
        auto a0 = str_manager.insert(std::string{});
        str_manager.get(a0, std::back_insert_iterator(result), 1024, 2048);
        if(tran) tran->commit();

        tresult.assert_that<equals>(result, ""s);

        tresult.assert_exception<std::out_of_range>([&]() {
            OP::vtm::TransactionGuard g(OP::vtm::resolve_segment_manager(topology).begin_transaction());
            str_manager.insert(BigStringEmulator(topology.segment_manager().segment_size()));
            });
        std::string buffer;

        tran = OP::vtm::resolve_segment_manager(topology).begin_transaction();
        auto a1 = str_manager.insert(
            tools::RandomGenerator::instance().next_alpha_num(buffer, 4096, 4096));
        if (tran) tran->commit();

        str_manager.get(a1, std::back_insert_iterator(result));
        tresult.assert_that<equals>(result, buffer);
        result.clear();
        str_manager.get(a1, std::back_insert_iterator(result), 1024, 2048);
        tresult.assert_that<equals>(result, std::string(buffer.begin() + 1024, buffer.begin() + 2048 + 1024));

        tresult.debug() << std::hex
            << "000 avail:" << start_avail_size << '\n';
        auto prev_avail = heap_mngr.available(0);
        tresult.debug() << std::hex
            << "1+0 avail:" << prev_avail << "\n";
        
        tran = OP::vtm::resolve_segment_manager(topology).begin_transaction();
        str_manager.destroy(a0);
        if (tran) tran->commit();

        tresult.assert_that<less>(prev_avail, heap_mngr.available(0));
        prev_avail = heap_mngr.available(0);
        tresult.debug() << std::hex
            << "-a0 avail:" << prev_avail << "\n";

        tran = OP::vtm::resolve_segment_manager(topology).begin_transaction();
        str_manager.destroy(a1);
        if (tran) tran->commit();

        tresult.assert_that<less>(prev_avail, heap_mngr.available(0));
        prev_avail = heap_mngr.available(0);

        tresult.debug() << std::hex
            << "-a1 avail:" << prev_avail << "\n";

        tresult.info() << "random insert...\n";
        size_t destroy_idx = 0;
        std::unordered_map< FarAddress, std::string > model_reference;
        std::vector< FarAddress > allocated_strs;
        allocated_strs.reserve(1000);
        for (size_t i = 0; i < 1000; ++i)
        {
            OP::vtm::TransactionGuard g(OP::vtm::resolve_segment_manager(topology).begin_transaction());
            auto rnd_str_addr = str_manager.insert(
                tools::RandomGenerator::instance().next_alpha_num(buffer, 4096, 0));
            g.commit();

            tresult.assert_true(model_reference.emplace(rnd_str_addr, buffer).second,
                "Non-unique addr allocated");
            allocated_strs.push_back(rnd_str_addr);
            if (i > 0 && !(i % 17)) //pseudo random de-alloc of previous string
            {//cicada rhythm
                FarAddress to_remove;
                std::swap(allocated_strs[destroy_idx++], to_remove);
                OP::vtm::TransactionGuard g(OP::vtm::resolve_segment_manager(topology).begin_transaction());
                str_manager.destroy(to_remove);
                g.commit();
                tresult.assert_that<equals>(1, model_reference.erase(to_remove),
                    "Unknown address of persisted string");
            }
        }
        //check all strings persisted are valid
        for (const FarAddress& to_check_addr : allocated_strs)
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
        for (const FarAddress& to_del : allocated_strs)
        {
            OP::vtm::TransactionGuard g(OP::vtm::resolve_segment_manager(topology).begin_transaction());
            str_manager.destroy(to_del);
            g.commit();
        }
        tresult.debug() << std::hex
            << "all avail:" << heap_mngr.available(0) << "\n";
    }

    void test_StringManagerEdgeCase(OP::utest::TestRuntime& tresult)
    {
        using namespace std::string_literals;
        using str_manager_t = OP::vtm::StringMemoryManager<>;

        auto tmngr1 = SegmentManager::template create_new<EventSourcingSegmentManager>(
            node_file_name,
            OP::vtm::SegmentOptions()
            .segment_size(0x110000),
            std::unique_ptr<MemoryChangeHistory>(new InMemoryChangeHistory)
        );

        SegmentTopology<HeapManagerSlot> mngr_toplogy(tmngr1);
        string_manager_edge_case(mngr_toplogy, tresult);
    }

    void test_StringManagerEdgeCaseNoTran(OP::utest::TestRuntime& tresult)
    {

        auto tmngr1 = OP::vtm::SegmentManager::create_new<SegmentManager>(
            node_file_name,
            OP::vtm::SegmentOptions()
            .segment_size(0x110000));

        SegmentTopology<HeapManagerSlot> mngr_toplogy(tmngr1);
        string_manager_edge_case(mngr_toplogy, tresult);
    }

    void test_SmartStr(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::vtm::SegmentManager::create_new<SegmentManager>(
            node_file_name,
            OP::vtm::SegmentOptions()
            .segment_size(0x110000));

        SegmentTopology<HeapManagerSlot> mngr_toplogy(tmngr1);
        using str_manager_t = OP::vtm::StringMemoryManager<>;
        str_manager_t smm(mngr_toplogy);
        auto& rndtool = tools::RandomGenerator::instance();


        for (size_t str_sz = 0; str_sz < sizeof(FarAddress) + 3; ++str_sz)
        {
            atom_string_t paste;
            if (str_sz)
                rndtool.next_alpha_num(paste, str_sz, str_sz);
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

    static auto& module_suite = OP::utest::default_test_suite("vtm.StringManager")
        .declare("basic", test_StringManager)
        .declare("edgecase-transactional", test_StringManagerEdgeCase)
        .declare("edgecase-no-tran", test_StringManagerEdgeCaseNoTran)
        .declare("smart-str", test_SmartStr)
        ;
} //ns:
