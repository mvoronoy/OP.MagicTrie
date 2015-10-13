#include <iostream>
#include <iomanip>
#include <op/trie/Trie.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/CacheManager.h>
#include <op/trie/TransactedSegmentManager.h>
#include <op/trie/MemoryManager.h>

using namespace OP::trie;
void test_TransactedSegmentManager()
{
    std::cout << "test Transacted Segment Manager..." << std::endl;
    if (1 == 1)
        return;
    const char seg_file_name[] = "segementation.test";
    auto mngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(seg_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(1024));
    SegmentTopology<MemoryManager>& mngrToplogy 
        = *new SegmentTopology<MemoryManager/*, NamedMap*/>(mngr1);

    struct TestAbc
    {
        int a;
        double b;
        char c[10];
        TestAbc(int a, double b, const char *c):
            a(a),
            b(b)
        {
            strncmp(this->c, c, sizeof(this->c));
        }
    };
    auto test_avail = mngrToplogy.slot<MemoryManager>().available(0);
    try{
        auto abc1_off = mngrToplogy.slot<MemoryManager>().make_new<TestAbc>(1, 1.01, "abc");
    }
    catch (OP::trie::Exception& e)
    {
        assert(e.code() == OP::trie::er_transaction_not_started);
    }
    assert(mngrToplogy.slot<MemoryManager>().available(0) == test_avail);
    mngr1->begin_transaction();
    auto abc1_off = mngrToplogy.slot<MemoryManager>().make_new<TestAbc>(1, 1.01, "abc");
    mngr1->commit();

}
