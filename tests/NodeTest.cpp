#include "unit_test.h"
#include "unit_test_is.h"
#include <op/trie/Trie.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/CacheManager.h>
#include <op/trie/TransactedSegmentManager.h>
#include <set>
#include <cassert>
#include <iterator>
using namespace OP::trie;
const char *node_file_name = "nodemanager.test";
static OP_CONSTEXPR(const) unsigned test_nodes_count_c = 101;
template <class NodeManager, class SegmentTopology>
void test_Generic(OP::utest::TestResult &tresult, SegmentTopology& topology)
{
    auto &mngr = topology.slot<NodeManager>();
    auto b100 = mngr.allocate();
    mngr.deallocate(b100);
    tresult.assert_true(topology.segment_manager().available_segments() == 1);
    topology._check_integrity();
    //exhaust all nodes
    for (auto i = 0; i < test_nodes_count_c; ++i)
    {

    }
}
void test_NodeManager(OP::utest::TestResult &tresult)
{

    struct TestPayload
    {/*The size of Payload selected to be bigger than NodeManager::ZeroHeader */
        std::uint64_t v1;
        double v2;
    };
    typedef NodeManager<TestPayload, test_nodes_count_c> test_node_manager_t;

    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(node_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy (tmngr1);
    test_Generic<test_node_manager_t>(tresult, mngrToplogy);

}
void test_NodeManagerSmallPayload(OP::utest::TestResult &tresult)
{
    //The size of payload smaller than NodeManager::ZeroHeader
    typedef NodeManager<std::uint32_t, test_nodes_count_c> test_node_manager_t;

    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(node_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy (tmngr1);
    test_Generic<test_node_manager_t>(tresult, mngrToplogy);
}

static auto module_suite = OP::utest::default_test_suite("NodeManager")
->declare(test_NodeManager, "general")
->declare(test_NodeManagerSmallPayload, "general-small-payload")
;
