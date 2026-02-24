#include <op/utest/unit_test.h>
#include <op/trie/Trie.h>

#include <op/vtm/managers/BaseSegmentManager.h>
#include <op/vtm/MemoryChunks.h>
#include <op/trie/Containers.h>
#include "GenericMemoryTest.h"

namespace
{

void test_SegmentManager(OP::utest::TestRuntime& result)
{
    using namespace OP::vtm;
    const char seg_file_name[] = "segmentation.test";
    GenericMemoryTest::test_MemoryManager(
        result,
        //create new
        [&]() {
            return BaseSegmentManager::create_new(seg_file_name,
                SegmentOptions().segment_size(0x110000));
        },
        //open 
        [&]() {
            return BaseSegmentManager::open(seg_file_name);
        }
    );
}
              

//using std::placeholders;
static auto& module_suite = OP::utest::default_test_suite("vtm.SegmentManager")
    .declare("HeapManagerSlot", test_SegmentManager)
;
}//ns: