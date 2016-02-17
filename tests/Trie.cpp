#include "unit_test.h"
#include "unit_test_is.h"
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/TransactedSegmentManager.h>
#include <algorithm>

using namespace OP::trie;
const char *test_file_name = "trie.test";

struct lexicographic_less : public std::binary_function<std::string, std::string, bool> {
    bool operator() (const std::string & s1, const std::string & s2) const 
    {
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end());
    }
};
void test_TrieCreation(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    tresult.assert_true(0 == trie->size());
    tresult.assert_true(1 == trie->nodes_count());
    auto nav1 = trie->begin();
    tresult.assert_true(nav1 == trie->end());
    trie.reset();

    //test reopen
    tmngr1 = OP::trie::SegmentManager::open<TransactedSegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    tresult.assert_true(0 == trie->size());
    tresult.assert_true(1 == trie->nodes_count());
    tresult.assert_true(nav1 == trie->end());
}
template <class Trie, class Map>
void compare_containers(OP::utest::TestResult &tresult, Trie& trie, Map& map)
{
    auto mi = std::begin(map);
    auto ti = trie.begin();
    //order must be the same
    for (; ti != trie.end(); ++ti, ++mi)
    {
        tresult.assert_true(ti.prefix().length() == mi->first.length(), 
            OP_CODE_DETAILS(<< "has:" << ti.prefix().length() << ", while expected:" << mi->first.length()));
        tresult.assert_true(
            std::equal(
                std::begin(ti.prefix()), std::end(ti.prefix()), std::begin(mi->first)), OP_CODE_DETAILS());
        tresult.assert_that(OP::utest::is::equals(*ti, mi->second), OP_CODE_DETAILS());
    }
    tresult.assert_true(mi == std::end(map), OP_CODE_DETAILS());
    //backward scenario - when map is used for key generation
    for (auto pair : map)
    {
    }
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
        trie->insert(b1 = std::begin(stem1), std::end(stem1), v_order+101.0), 
        OP_CODE_DETAILS("Duplicate insert must not be allowed"));
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 1);
    tresult.assert_true(
        trie->insert(b1 = stem1_deviation1.cbegin(), stem1_deviation1.cend(), 0.0), OP_CODE_DETAILS());
    tresult.assert_true(b1 == std::end(stem1_deviation1));
    standard[stem1_deviation1] = v_order++;
    compare_containers(tresult, *trie, standard);
    // test behaviour on range
    const std::string stem2(256, 'b');
    tresult.assert_true(trie->insert(b1 = std::begin(stem2), std::end(stem2), v_order), OP_CODE_DETAILS());
    tresult.assert_true(3 == trie->nodes_count(), "2 nodes must exists in the system");
    tresult.assert_true(b1 == std::end(stem2));
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
    std::array<std::uint16_t, 129> rand_idx;
    std::iota(std::begin(rand_idx), std::end(rand_idx), 0);
    std::random_shuffle(std::begin(rand_idx), std::end(rand_idx));
    for (auto i : rand_idx)
    {
        std::string test = std::string(1, (std::string::value_type)i) + 
            stems[rand() % std::extent< decltype(stems) >::value];
        trie->insert(std::begin(test), std::end(test), (double)test.length());
        test_values[test] = (double)test.length();
    }
    compare_containers(tresult, *trie, test_values);
}

void test_TrieGrowAfterUpdate(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::map<std::string, double> test_values;
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const std::string test_seq [] = { "abc", "bcd", "def", "fgh", "ijk", "lmn" };
    double x = 0.0;
    for (auto i : test_seq)
    {
        const std::string& test = i;
        trie->insert(std::begin(test), std::end(test), x);
        test_values[test] = x;
        x += 1.0;
    }
    //

    compare_containers(tresult, *trie, test_values);
    const std::string& upd = test_seq[std::extent<decltype(test_seq)>::value / 2];
}
static auto module_suite = OP::utest::default_test_suite("Trie")
->declare(test_TrieCreation, "creation")
->declare(test_TrieInsert, "insertion")
->declare(test_TrieInsertGrow, "insertion-grow")
;
