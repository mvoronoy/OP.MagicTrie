#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/TransactedSegmentManager.h>
#include <set>
#include <cassert>
#include <iterator>
using namespace OP::trie;
const char *node_file_name = "FixedSizeMemoryManager.test";
static OP_CONSTEXPR(const) unsigned test_nodes_count_c = 101;


template <class FixedSizeMemoryManager, class SegmentTopology>
void test_Generic(OP::utest::TestResult &tresult, SegmentTopology& topology)
{
    auto &mngr = topology.slot<FixedSizeMemoryManager>();
    auto b100 = mngr.allocate();
    mngr.deallocate(b100);
    tresult.assert_true(topology.segment_manager().available_segments() == 1);
    topology._check_integrity();
    std::vector<FarAddress> allocated_addrs(test_nodes_count_c);
    //exhaust all nodes in sinle segment and check new segment allocation
    for (auto i = 0; i < test_nodes_count_c; ++i)
    {
        OP::vtm::TransactionGuard op_g(topology.segment_manager().begin_transaction());
        auto pos = mngr.allocate();
        auto &wr = *topology.segment_manager().wr_at<typename FixedSizeMemoryManager::payload_t>(pos);
        
        tresult.assert_true(wr.inc == 57);
        wr.inc += i;
        op_g.commit();
        allocated_addrs[i]=pos;
    }
    tresult.assert_true(topology.segment_manager().available_segments() == 1, 
        OP_CODE_DETAILS(<<"There must be single segment"));
    mngr.allocate();
    tresult.assert_true(topology.segment_manager().available_segments() == 2, 
        OP_CODE_DETAILS(<<"New segment must be allocated"));
    //test all values kept correct value
    for (auto i = 0; i < test_nodes_count_c; ++i)
    {
        auto to_test = view<typename FixedSizeMemoryManager::payload_t>(topology, allocated_addrs[i]);
        tresult.assert_true(i + 57 == to_test->inc, "Invalid value stored");
    }
}
void test_NodeManager(OP::utest::TestResult &tresult)
{

    struct TestPayload
    {/*The size of Payload selected to be bigger than FixedSizeMemoryManager::ZeroHeader */
        TestPayload()
        {
            v1 = 0;
            inc = 57;
            v2 = 3.;
        }
        std::uint64_t v1;
        std::uint32_t inc;
        double v2;
    };
    typedef FixedSizeMemoryManager<TestPayload, test_nodes_count_c> test_node_manager_t;

    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(node_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy (tmngr1);
    test_Generic<test_node_manager_t>(tresult, mngrToplogy);

}
void test_NodeManagerSmallPayload(OP::utest::TestResult &tresult)
{
    struct TestPayloadSmall
    {/*The size of Payload selected to be bigger than FixedSizeMemoryManager::ZeroHeader */
        TestPayloadSmall()
        {
            inc = 57;
        }
        std::uint32_t inc;
    };
    //The size of payload smaller than FixedSizeMemoryManager::ZeroHeader
    typedef FixedSizeMemoryManager<TestPayloadSmall, test_nodes_count_c> test_node_manager_t;

    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(node_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy (tmngr1);
    test_Generic<test_node_manager_t>(tresult, mngrToplogy);
}

static auto module_suite = OP::utest::default_test_suite("FixedSizeMemoryManager")
->declare(test_NodeManager, "general")
->declare(test_NodeManagerSmallPayload, "general-small-payload")
;
