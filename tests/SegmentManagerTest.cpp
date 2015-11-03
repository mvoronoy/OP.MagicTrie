#include "unit_test.h"
#include <op/trie/Trie.h>

#include <op/trie/SegmentManager.h>
#include <op/trie/MemoryManager.h>
#include <op/trie/Containers.h>
#include "GenericMemoryTest.h"
using namespace OP::trie;

void test_SegmentManager(OP::utest::TestResult& result)
{
    const char seg_file_name[] = "segementation.test";
    test_MemoryManager<OP::trie::SegmentManager>(seg_file_name, result);
}
              

//using std::placeholders;
static auto module_suite = OP::utest::default_test_suite("SegmentManager")
->declare(test_SegmentManager, "base")
;