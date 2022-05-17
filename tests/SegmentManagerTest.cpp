#include <op/utest/unit_test.h>
#include <op/trie/Trie.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/trie/Containers.h>
#include "GenericMemoryTest.h"
using namespace OP::trie;

void test_SegmentManager(OP::utest::TestRuntime& result)
{
    const char seg_file_name[] = "segementation.test";
    GenericMemoryTest::test_MemoryManager<OP::trie::SegmentManager>(seg_file_name, result);
}
              

//using std::placeholders;
static auto& module_suite = OP::utest::default_test_suite("SegmentManager")
.declare("HeapManagerSlot", test_SegmentManager)
;