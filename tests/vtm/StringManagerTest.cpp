#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/StringMemoryManager.h>
#include <op/vtm/EventSourcingSegmentManager.h>
#include <set>
#include <cassert>
#include <iterator>
using namespace OP::trie;
using namespace OP::vtm;
using namespace OP::utest;

static const char *node_file_name = "StringManager.test";

void test_StringManager(OP::utest::TestRuntime &tresult)
{
    using namespace std::string_literals;
    using str_manager_t = OP::vtm::StringMemoryManager;

    auto tmngr1 = SegmentManager::OP_TEMPL_METH(create_new)<EventSourcingSegmentManager>(
        node_file_name, 
        OP::vtm::SegmentOptions()
        .segment_size(0x110000)
        );

    SegmentTopology<
        HeapManagerSlot
    > mngr_toplogy (tmngr1);
    str_manager_t str_manager(mngr_toplogy);
     
    auto a1 = str_manager.insert(std::string("abc"));
    std::string result;
    str_manager.get(a1, std::back_insert_iterator(result));
    tresult.assert_that<equals>(result, "abc"s);
    result.clear();
    str_manager.get(a1, std::back_insert_iterator(result), 1, 2);
    tresult.assert_that<equals>(result, "bc"s);
}

struct BigStringEmulator
{
    size_t _emulate_size;
    BigStringEmulator(size_t emulate_size) 
        : _emulate_size(emulate_size)
    {
    }

    size_t size() const
    {
        return _emulate_size;
    }
    
    const char* data() const
    {
        return nullptr;
    }
};

void test_StringManagerEdgeCase(OP::utest::TestRuntime& tresult)
{
    using namespace std::string_literals;
    using str_manager_t = OP::vtm::StringMemoryManager;

    auto tmngr1 = SegmentManager::OP_TEMPL_METH(create_new) < EventSourcingSegmentManager > (
        node_file_name,
        OP::vtm::SegmentOptions()
        .segment_size(0x110000)
        );

    SegmentTopology<HeapManagerSlot> mngr_toplogy(tmngr1);
    str_manager_t str_manager(mngr_toplogy);
    auto& heap_mngr = mngr_toplogy.slot<HeapManagerSlot>();
    auto start_avail_size = heap_mngr.available(0);
    std::string result;

    auto a0 = str_manager.insert(std::string{});
    str_manager.get(a0, std::back_insert_iterator(result), 1024, 2048);
    tresult.assert_that<equals>(result, ""s);


    tresult.assert_exception<std::out_of_range>([&]() {
        str_manager.insert(BigStringEmulator(tmngr1->segment_size()));
        });
    std::string buffer;

    auto a1 = str_manager.insert(
        tools::RandomGenerator::instance().next_alpha_num(buffer, 4096, 4096));

    str_manager.get(a1, std::back_insert_iterator(result));
    tresult.assert_that<equals>(result, buffer);
    result.clear();
    str_manager.get(a1, std::back_insert_iterator(result), 1024, 2048);
    tresult.assert_that<equals>(result, std::string(buffer.begin()+1024, buffer.begin()+2048 + 1024));

    tresult.debug() << std::hex
        << "000 avail:" << start_avail_size << '\n'
        << "1+0 avail:" << heap_mngr.available(0) << "\n";
    str_manager.destroy(a0);
    tresult.debug() << std::hex
        << "-a0 avail:" << heap_mngr.available(0) << "\n";
    str_manager.destroy(a1);

    tresult.debug() << std::hex
        << "-a1 avail:" << heap_mngr.available(0) << "\n";
    tresult.info() << "random insert...\n";
    size_t destroy_idx = 0;
    std::vector< FarAddress > allocated_strs;
    allocated_strs.reserve(1000);
    for (size_t i = 0; i < 1000; ++i)
    {
        auto rnd_str_addr = str_manager.insert(
            tools::RandomGenerator::instance().next_alpha_num(buffer, 4096, 0));
        allocated_strs.push_back(rnd_str_addr);
        if (i > 0 && !(i % 17))
        {//cycada rhitm
            FarAddress to_remove;
            std::swap(allocated_strs[destroy_idx++], to_remove);
            str_manager.destroy(to_remove);
        }
    }
    //don't destroy str before `destroy_idx`
    allocated_strs.erase(allocated_strs.begin(), allocated_strs.begin() + destroy_idx);
    for(const FarAddress& to_del: allocated_strs)
        str_manager.destroy(to_del);
    tresult.debug() << std::hex
        << "all avail:" << heap_mngr.available(0) << "\n";
}

static auto& module_suite = OP::utest::default_test_suite("StringManager")
    .declare("basic", test_StringManager)
    .declare("edgecase", test_StringManagerEdgeCase)
    ;
