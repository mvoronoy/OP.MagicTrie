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
template <class Trie, class Map>
void compare_containers(OP::utest::TestResult &tresult, Trie& trie, Map& map)
{

}
void test_TrieInsert(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::map<std::string, double> standard;
    double v_order = 0.0;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    const std::string stem1(260, 'a');
    /*stem1_deviation1 - 1'a' for presence, 256'a' for exhausting stem container*/
    auto stem1_deviation1(std::string(257, 'a') + "b");
    //insert that reinforce long stem
    auto b1 = std::begin(stem1);
    tresult.assert_true(trie->insert(b1, std::end(stem1), v_order), OP_CODE_DETAILS());
    tresult.assert_true(2 == trie->nodes_count(), "2 nodes must be create for long stems");
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 1);
    standard[stem1] = v_order++;
    compare_containers(tresult, *trie, standard);

    tresult.assert_false(
        trie->insert(b1 = std::begin(stem1), std::end(stem1), v_order), 
        OP_CODE_DETAILS("Duplicate insert must not be allowed"));
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 2);
    tresult.assert_true(
        trie->insert(b1 = stem1_deviation1.cbegin(), stem1_deviation1.cend(), 0.0), OP_CODE_DETAILS());
    tresult.assert_true(b1 == std::end(stem1_deviation1));
    standard[stem1_deviation1] = v_order++;
    compare_containers(tresult, *trie, standard);
    // test behaviour on range
    const std::string stem2(256, 'b');
    tresult.assert_true(trie->insert(b1, std::end(stem1), v_order), OP_CODE_DETAILS());
    tresult.assert_true(2 == trie->nodes_count(), "2 nodes must exists in the system");
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 3);
    compare_containers(tresult, *trie, standard);
    
}
void test_TrieInsertGrow(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::map<std::string, double> test_values;
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const std::string stems[] = { "abc", "", "x", std::string(256, 'z') };
    std::array<std::uint16_t, 256> rand_idx;
    std::iota(std::begin(rand_idx), std::end(rand_idx), 0);
    std::random_shuffle(std::begin(rand_idx), std::end(rand_idx));
    for (auto i : rand_idx)
    {
        std::string test = std::string(1, (std::string::value_type)i) + 
            stems[rand() % std::extent< decltype(stems) >::value];
        trie->insert(std::begin(test), std::end(test), (double)test.length());
        test_values[test] = (double)test.length();
    }
}

static auto module_suite = OP::utest::default_test_suite("Trie")
->declare(test_TrieCreation, "creation")
->declare(test_TrieInsert, "insertion")
->declare(test_TrieInsertGrow, "insertion-grow")
;
