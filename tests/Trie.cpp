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
    trie = trie_t::open(tmngr1);
    tresult.assert_true(0 == trie->size());
    tresult.assert_true(1 == trie->nodes_count());
    tresult.assert_true(nav1 == trie->navigator_end());
}

static auto module_suite = OP::utest::default_test_suite("Trie")
->declare(test_TrieCreation, "creation")
;
