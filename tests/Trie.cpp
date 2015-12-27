#include "unit_test.h"
#include "unit_test_is.h"
#include <op/trie/Trie.h>
#include <op/trie/SegmentManager.h>
#include <op/trie/CacheManager.h>
#include <op/trie/TransactedSegmentManager.h>

using namespace OP::trie;
const char *test_file_name = "trie.test";

void test_TrieCreation(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    tresult.assert_true(0 == trie->size());
    tresult.assert_true(1 == trie->nodes_count());
    auto nav1 = trie->navigator_begin();
    tresult.assert_true(nav1 == trie->navigator_end());
    trie.reset();

    //test reopen
    tmngr1 = OP::trie::SegmentManager::open<TransactedSegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    tresult.assert_true(0 == trie->size());
    tresult.assert_true(1 == trie->nodes_count());
    tresult.assert_true(nav1 == trie->navigator_end());
}
void test_TrieInsert(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    const std::string stem1(260, 'a');
    /*stem1_deviation1 - 1'a' for presence, 256'a' for exhausting stem container*/
    auto stem1_deviation1(std::string(257, 'a') + "b");
    //insert that reinforce long stem
    auto b1 = std::begin(stem1);
    tresult.assert_true(trie->insert(b1, std::end(stem1), 0.0), OP_CODE_DETAILS());
    tresult.assert_true(2 == trie->nodes_count(), "2 nodes must be create for long stems");
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 1);

    tresult.assert_false(
        trie->insert(b1 = std::begin(stem1), std::end(stem1), 0.0), 
        OP_CODE_DETAILS("Duplicate insert must not be allowed"));
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 1);

    tresult.assert_true(trie->insert(b1 = std::begin(stem1_deviation1), std::end(stem1_deviation1), 0.0), OP_CODE_DETAILS());
    tresult.assert_true(b1 == std::end(stem1_deviation1));
}
static auto module_suite = OP::utest::default_test_suite("Trie")
->declare(test_TrieCreation, "creation")
->declare(test_TrieInsert, "insertion")
;
