#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/EventSourcingSegmentManager.h>
#include <set>
#include <cassert>
#include <iterator>
using namespace OP::trie;
using namespace OP::utest;

static const char *node_file_name = "FixedSizeMemoryManager.test";
static OP_CONSTEXPR(const) unsigned test_nodes_count_c = 101;


template <class FixedSizeMemoryManager, class SegmentTopology>
void test_Generic(OP::utest::TestRuntime &tresult, SegmentTopology& topology)
{
    auto &mngr = topology.template slot<FixedSizeMemoryManager>();
    auto b100 = mngr.allocate();
    mngr.deallocate(b100);
    tresult.assert_true(topology.segment_manager().available_segments() == 1);
    topology._check_integrity();
    std::vector<FarAddress> allocated_addrs(test_nodes_count_c);
    //exhaust all nodes in single segment and check new segment allocation
    for (auto i = 0; i < test_nodes_count_c; ++i)
    {
        OP::vtm::TransactionGuard op_g(topology.segment_manager().begin_transaction());
        auto pos = mngr.allocate();
        auto &wr = *topology.segment_manager().template wr_at<typename FixedSizeMemoryManager::payload_t>(pos);
        
        tresult.assert_true(wr.inc == 57);
        wr.inc += i;
        op_g.commit();
        allocated_addrs[i] = pos;
        if ((i + 1) < test_nodes_count_c)
        {
            tresult.assert_true(
                topology.segment_manager().available_segments() == 1,
                OP_CODE_DETAILS(<< "There must be single segment"));
        }
    }
    //as soon FixedSizeMemoryManager contains ThreadPool folowing line became probabilistic
    //tresult.assert_true(topology.segment_manager().available_segments() == 1, 
    //    OP_CODE_DETAILS(<<"There must be single segment"));
    mngr.allocate();
    tresult.assert_true(topology.segment_manager().available_segments() == 2, 
        OP_CODE_DETAILS(<<"New segment must be allocated"));
    //test all values kept correct value
    for (auto i = 0; i < test_nodes_count_c; ++i)
    {
        auto to_test = view<typename FixedSizeMemoryManager::payload_t>(topology, allocated_addrs[i]);
        tresult.assert_true(i + 57 == to_test->inc, "Invalid value stored");
    }
    topology._check_integrity();
    // now free all in random order and exhaust again
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(allocated_addrs.begin(), allocated_addrs.end(), g);
    for (const auto& addr : allocated_addrs)
    {
        mngr.deallocate(addr);
    }
    allocated_addrs.clear();
    topology._check_integrity();
    for (auto i = 0; i < 2*test_nodes_count_c; ++i)
    {
        OP::vtm::TransactionGuard op_g(topology.segment_manager().begin_transaction());
        auto pos = mngr.allocate();
        auto& wr = *topology.segment_manager().template wr_at<typename FixedSizeMemoryManager::payload_t>(pos);

        tresult.assert_true(wr.inc == 57);
        wr.inc += i;
        op_g.commit();
        allocated_addrs.push_back(pos);
        if ((i + 2) < 2 * test_nodes_count_c)
        {
            tresult.assert_true(topology.segment_manager().available_segments() == 2,
                OP_CODE_DETAILS(<< "No new segments must be allocated"));
        }
    }
    //test all values kept correct value
    for (auto i = 0; i < 2 * test_nodes_count_c; ++i)
    {
        auto to_test = view<typename FixedSizeMemoryManager::payload_t>(topology, allocated_addrs[i]);
        tresult.assert_true(i + 57 == to_test->inc, "Invalid value stored");
    }
}

void test_NodeManager(OP::utest::TestRuntime &tresult)
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

    auto tmngr1 = OP::trie::SegmentManager::template create_new<EventSourcingSegmentManager>(
        node_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy (tmngr1);
    test_Generic<test_node_manager_t>(tresult, mngrToplogy);
}

void test_Multialloc(OP::utest::TestRuntime& tresult)
{
    struct TestPayload
    {
        TestPayload() = delete;
        TestPayload(
            double ax1, char ac1, int an1)
            : x1(ax1), c1(ac1), n1(an1)
        {}
        TestPayload(const TestPayload&) = default;

        double x1;
        char c1;
        int n1;
    };

    typedef FixedSizeMemoryManager<TestPayload, test_nodes_count_c> test_node_manager_t;

    auto tmngr1 = OP::trie::SegmentManager::template create_new<EventSourcingSegmentManager>(
        node_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy(tmngr1);
    auto &fmm = mngrToplogy.template slot<test_node_manager_t>();
    TestPayload generation(5.7, 'a', 11);

    OP::vtm::TransactionGuard op_g(mngrToplogy.segment_manager().begin_transaction());
    constexpr size_t N = 10;
    constexpr size_t const buf_size_c = test_nodes_count_c / N;
    FarAddress result[buf_size_c];
    fmm.allocate_n(result, std::extent_v<decltype(result)>, [&](size_t i, auto* p) {
        return new (p) TestPayload{ generation };
        });
    op_g.commit();

    for (auto i = 0; i < std::extent_v<decltype(result)>; ++i)
    {
        auto to_test = view<TestPayload>(mngrToplogy, result[i]);
        tresult.assert_that<equals>(to_test->c1, generation.c1);
        tresult.assert_that<equals>(to_test->n1, generation.n1);
        tresult.assert_that<equals>(to_test->x1, generation.x1);
    }
    mngrToplogy._check_integrity();
    for (auto p = std::begin(result); p != std::end(result); ++p)
    {
        fmm.deallocate(*p);
    }
    mngrToplogy._check_integrity();
    tresult.assert_that<equals>(1, mngrToplogy.segment_manager().available_segments(),
        OP_CODE_DETAILS(<<"There must be single segment"));
    auto usage = fmm.usage_info();
    tresult.assert_that<equals>(0, usage.first,
        OP_CODE_DETAILS(<< "All must be free"));
    tresult.assert_that<equals>(test_nodes_count_c, usage.second,
        OP_CODE_DETAILS(<< "Free count must be:" << test_nodes_count_c));
    FarAddress single;
    fmm.allocate_n(&single, 0, 
        [&](size_t i, auto* ) {
            tresult.fail("Lambda must not be called for 0-size");
        });
    tresult.assert_that<equals>(single, FarAddress{});
    usage = fmm.usage_info();
    tresult.assert_that<equals>(0, usage.first,
        OP_CODE_DETAILS(<< "All must be free"));
    tresult.assert_that<equals>(test_nodes_count_c, usage.second,
        OP_CODE_DETAILS(<< "Free count must be:" << test_nodes_count_c));

    generation.c1++, generation.n1++, generation.x1++;
    std::vector<FarAddress> buffer;
    //as soon as test_nodes_count_c is odd runing allocate_n several times 
    //going to surpass the segment threshold
    for (size_t i = 0, blsz = 1; i < N+N/2; ++i, ++blsz)
    {
        buffer.resize(buffer.size() + blsz);
        auto beg = buffer.end() - blsz;
        fmm.allocate_n(&*beg, blsz, [&](size_t i, auto* ptr) {
            return new (ptr) TestPayload(generation);
            });
        for (; beg != buffer.end(); ++beg)
        {
            auto to_test = view<TestPayload>(mngrToplogy, *beg);
            tresult.assert_that<equals>(to_test->c1, generation.c1);
            tresult.assert_that<equals>(to_test->n1, generation.n1);
            tresult.assert_that<equals>(to_test->x1, generation.x1);
        }
    }

    mngrToplogy._check_integrity();
    tresult.assert_that<equals>(2, mngrToplogy.segment_manager().available_segments(),
        OP_CODE_DETAILS(<< "There must be one more segment"));
    for (const auto& addr : buffer)
    {
        fmm.deallocate(addr);
    }
    mngrToplogy._check_integrity();
    usage = fmm.usage_info();
    tresult.assert_that<equals>(0, usage.first,
        OP_CODE_DETAILS(<< "All must be free"));
    tresult.assert_that<equals>(2 * test_nodes_count_c, usage.second,
        OP_CODE_DETAILS(<< "Free count must be:" << test_nodes_count_c));
}

void test_NodeManagerSmallPayload(OP::utest::TestRuntime &tresult)
{
    struct TestPayloadSmall
    {/*The size of Payload selected to be smaller than FixedSizeMemoryManager::ZeroHeader */
        TestPayloadSmall()
        {
            inc = 57;
        }
        std::uint32_t inc;
    };
    //The size of payload smaller than FixedSizeMemoryManager::ZeroHeader
    typedef FixedSizeMemoryManager<TestPayloadSmall, test_nodes_count_c> test_node_manager_t;

    auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(node_file_name, 
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    SegmentTopology<test_node_manager_t> mngrToplogy (tmngr1);
    test_Generic<test_node_manager_t>(tresult, mngrToplogy);
}

static auto& module_suite = OP::utest::default_test_suite("vtm.FixedSizeMemoryManager")
    .declare("general", test_NodeManager)
    .declare("multialloc", test_Multialloc)
    .declare("small-payload", test_NodeManagerSmallPayload)
;
