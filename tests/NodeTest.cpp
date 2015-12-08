#include "unit_test.h"
#include "unit_test_is.h"
#include <op/trie/Trie.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/CacheManager.h>
#include <set>
#include <cassert>
#include <iterator>
using namespace OP::trie;
const char *node_test = "nodemanager.test";
void test_NodeManager()
{
    typedef NodeManager<double> test_node_manager_t;

    std::unique_ptr<test_node_manager_t> node_manager = test_node_manager_t::create_new(node_test,
        NodeManagerOptions()
        .node_count(3)
    );
}

static auto module_suite = OP::utest::default_test_suite("NodeManager")
->declare(test_NodeManager, "general")
;
