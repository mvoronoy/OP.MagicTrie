#include <iostream>
#include <iomanip>
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