#include "unit_test.h"
#include "unit_test_is.h"
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/TransactedSegmentManager.h>
#include <algorithm>

using namespace OP::trie;
using namespace OP::utest;
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
template <class O, class T>
void print_hex(O& os, const T& t)
{
    auto b = std::begin(t), e = std::end(t);
    for (; b != e; ++b)
        os << std::setbase(16) << std::setw(2) << std::setfill('0') << (unsigned int)(unsigned char)*b;
    std::cout << '\n';
}
template <class Trie, class Map>
void compare_containers(OP::utest::TestResult &tresult, Trie& trie, Map& map)
{
    auto mi = std::begin(map);
    /*for (auto xp : map)
    {
        tresult.info() << xp.first << '=' << xp.second << '\n';
    }*/
    auto ti = trie.begin();
    int n = 0;
    //order must be the same
    for (; ti != trie.end(); ++ti, ++mi, ++n)
    {
        //print_hex(tresult.info() << "1)", ti.prefix());
        //print_hex(tresult.info() << "2)", mi->first);
        tresult.assert_true(ti.prefix().length() == mi->first.length(), 
            OP_CODE_DETAILS(<<"step#"<< n << "has:" << ti.prefix().length() << ", while expected:" << mi->first.length()));
        tresult.assert_true(
            std::equal(
            std::begin(ti.prefix()), std::end(ti.prefix()), std::begin(mi->first), [](atom_t left, atom_t right){return left == right; }),
            OP_CODE_DETAILS(<<"step#"<< n << ", for key="<<(const char*)mi->first.c_str() << ", while obtained:" << (const char*)ti.prefix().c_str()));
        tresult.assert_that(OP::utest::is::equals(*ti, mi->second), OP_CODE_DETAILS(<<"Associated value error, has:" << *ti << ", expected:" << mi->second ));
    }
    tresult.assert_true(mi == std::end(map), OP_CODE_DETAILS());
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
    const std::string stem1(260, 'a'); //total 260
    /*stem1_deviation1 - 1'a' for presence, 256'a' for exhausting stem container*/
    std::string stem1_deviation1{ std::string(257, 'a') + 'b'};
    //insert that reinforce long stem
    auto b1 = std::begin(stem1);
    auto ir1 = trie->insert(b1, std::end(stem1), v_order);
    tresult.assert_true(ir1.first, OP_CODE_DETAILS());
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir1.second.prefix()), std::end(ir1.second.prefix()),
        std::begin(stem1), std::end(stem1)
        ));
    tresult.assert_true(2 == trie->nodes_count(), "2 nodes must be create for long stems");
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 1);

    standard[stem1] = v_order++;
    compare_containers(tresult, *trie, standard);
    
    auto ir2 = trie->insert(b1 = std::begin(stem1), std::end(stem1), v_order + 101.0);
    tresult.assert_false(
        ir2.first, 
        OP_CODE_DETAILS("Duplicate insert must not be allowed"));
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 1);
    
    auto ir3 = trie->insert(b1 = stem1_deviation1.cbegin(), stem1_deviation1.cend(), v_order);
    tresult.assert_true(ir3.first, OP_CODE_DETAILS());
    tresult.assert_true(b1 == std::end(stem1_deviation1));
    //print_hex(std::cout << "1)", stem1_deviation1);
    //print_hex(std::cout << "2)", ir3.second.prefix());

    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir3.second.prefix()), std::end(ir3.second.prefix()),
        std::begin(stem1_deviation1), std::end(stem1_deviation1)
        ));
    standard[stem1_deviation1] = v_order++;
    compare_containers(tresult, *trie, standard);
    // test behaviour on range
    const std::string stem2(256, 'b');
    auto ir4 = trie->insert(b1 = std::begin(stem2), std::end(stem2), v_order);
    tresult.assert_true(ir4.first, OP_CODE_DETAILS());
    standard[stem2] = v_order++;
    tresult.assert_true(3 == trie->nodes_count(), "3 nodes must exists in the system");
    tresult.assert_true(b1 == std::end(stem2));
    tresult.assert_true(trie->size() == 3);
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir4.second.prefix()), std::end(ir4.second.prefix()),
        std::begin(stem2), std::end(stem2)
        ));

    compare_containers(tresult, *trie, standard);
    //
    // test diversification
    //
    tresult.status_details() << "test diversification\n";
    std::string stem3 = { std::string(1, 'c') + std::string(256, 'a') };
    const std::string& const_stem3 = stem3;
    auto ir5 = trie->insert(b1 = std::begin(const_stem3), std::end(const_stem3), v_order);
    tresult.assert_true(ir5.first, OP_CODE_DETAILS());
    standard[stem3] = v_order++;
    tresult.assert_true(4 == trie->nodes_count(), "4 nodes must exists in the system");
    tresult.assert_true(b1 == std::end(stem3));
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.second.prefix()), std::end(ir5.second.prefix()),
        std::begin(stem3), std::end(stem3)
        ));
    compare_containers(tresult, *trie, standard);
    //now make iteration back over stem3 and create diversification in 
    //std::cout << "***";
    for (auto i = 0; stem3.length() > 1; ++i)
    {
        //std::cout << "\b\b\b" << std::setbase(16) << std::setw(3) << std::setfill('0') << i;
        stem3.resize(stem3.length() - 1);
        stem3 += std::string(1, 'b');
        ir5 = trie->insert(b1 = std::begin(const_stem3), std::end(const_stem3), (double)stem3.length());
        tresult.assert_true(ir5.first, OP_CODE_DETAILS());
        standard[stem3] = (double)stem3.length();
        stem3.resize(stem3.length() - 1);
    }//
    std::cout << trie->nodes_count()<< "\n";
    compare_containers(tresult, *trie, standard);
    //
    tresult.status_details() << "test diversification#2\n";
    std::string stem4 = { std::string(1, 'd') + std::string(256, 'a') };
    const std::string& const_stem4 = stem4;
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    tresult.assert_true(ir5.first, OP_CODE_DETAILS());
    standard[stem4] = v_order++;
    tresult.assert_true(260 == trie->nodes_count(), "260 nodes must exists in the system");
    stem4.resize(stem4.length() - 1);
    stem4.append(std::string(258,'b'));
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    tresult.assert_true(ir5.first, OP_CODE_DETAILS());
    standard[stem4] = v_order++;
    tresult.assert_true(261 == trie->nodes_count(), "262 nodes must exists in the system");
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
    std::array<std::uint16_t, 255> rand_idx;
    std::iota(std::begin(rand_idx), std::end(rand_idx), 0);
    std::random_shuffle(std::begin(rand_idx), std::end(rand_idx));
    for (auto i : rand_idx)
    {
        std::string test = std::string(1, (std::string::value_type)i) + 
            stems[rand() % std::extent< decltype(stems) >::value];
        auto ins_res = trie->insert(std::begin(test), std::end(test), (double)test.length());
        tresult.assert_true(ins_res.first);
        tresult.assert_true(tools::container_equals(ins_res.second.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));
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
        auto ins_res = trie->insert(std::begin(test), std::end(test), x);
        tresult.assert_true(ins_res.first);
        tresult.assert_true(tools::container_equals(ins_res.second.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));

        test_values[test] = x;
        x += 1.0;
    }
    //

    compare_containers(tresult, *trie, test_values);
    const std::string& upd = test_seq[std::extent<decltype(test_seq)>::value / 2];
}
void test_TrieLowerBound(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::map<std::string, double> test_values;
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const std::string test_seq[] = { "abc", "bcd", "def", "fgh", "ijk", "lmn" };

    double x = 0.0;
    for (auto i : test_seq)
    {
        const std::string& test = i;
        auto ins_res = trie->insert(std::begin(test), std::end(test), x);
        tresult.assert_true(ins_res.first);
        tresult.assert_true(tools::container_equals(ins_res.second.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));

        test_values[test] = x;
        x += 1.0;
    }
    trie.reset();
    tmngr1 = OP::trie::SegmentManager::open<TransactedSegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    compare_containers(tresult, *trie, test_values);
    x = 0.0;
    const atom_t *np = nullptr;
    tresult.assert_true(trie->end() == trie->lower_bound(np, np));
    const atom_t az[] = "az";  auto b1 = std::begin(az);
    tresult.assert_true(tools::container_equals(test_seq[1], trie->lower_bound(b1, std::end(az)).prefix(), tools::sign_tolerant_cmp));

    const atom_t unexisting[] = "zzz";
    np = std::begin(unexisting);
    tresult.assert_true(trie->end() == trie->lower_bound(np, std::end(unexisting)));
    
    for (auto i = 0; i < std::extent<decltype(test_seq)>::value - 1; ++i, x += 1.0)
    {
        const std::string& test = test_seq[i];
        auto lbit = trie->lower_bound(std::begin(test), std::end(test));

        tresult.assert_true(tools::container_equals(lbit.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_true(x == *lbit);

        auto query = test_seq[i] + "a";
        auto lbit2 = trie->lower_bound(std::begin(query), std::end(query));

        //print_hex(std::cout << "1)", test_seq[i + 1]);
        //print_hex(std::cout << "2)", lbit2.prefix());
        tresult.assert_true(tools::container_equals(lbit2.prefix(), test_seq[i+1], &tools::sign_tolerant_cmp<atom_t>));

        auto lbit3 = trie->lower_bound(std::begin(test), std::end(test)-1);//take shorter key
        tresult.assert_true(tools::container_equals(lbit3.prefix(), test.substr(0, 2), &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_true(x == *lbit);
    }
}
void test_TrieSubtree(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::map<atom_string_t, double> test_values;
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const atom_string_t stems[] = { (atom_t*)"abc", (atom_t*)"x", atom_string_t(256, 'z') };
    std::array<std::uint16_t, 255> rand_idx;
    std::iota(std::begin(rand_idx), std::end(rand_idx), 0);
    std::random_shuffle(std::begin(rand_idx), std::end(rand_idx));
    auto n = 0;
    for (auto i : rand_idx)
    {
        atom_string_t root(1, (atom_string_t::value_type)i);
        decltype(root.begin()) b1;
        //for odd entries make terminal
        if ((i & 1) != 0)
        {
            auto ins_res = trie->insert(b1 = std::begin(root), std::end(root), (double)i);
            test_values[root] = (double)i;
        }
        //make inner loop to create all possible stems combinations
        for (auto j : stems)
        {
            atom_string_t test = root + j;
            std::cout << n++ << std::endl;
            auto ins_res = trie->insert(std::begin(test), std::end(test), (double)test.length());
            tresult.assert_true(ins_res.first);
            tresult.assert_true(tools::container_equals(ins_res.second.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));
            test_values[test] = (double)test.length();
        }
        //std::cout << std::setfill('0') << std::setbase(16) << std::setw(2) << (unsigned)i << "\n";
    }
    compare_containers(tresult, *trie, test_values);
    trie.reset();
    tmngr1 = OP::trie::SegmentManager::open<TransactedSegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    //
    compare_containers(tresult, *trie, test_values);
    std::set<atom_string_t> sorted_checks(std::begin(stems), std::end(stems));
    for (auto i : rand_idx)
    {
        atom_string_t test = atom_string_t(1, (std::string::value_type)i);
        decltype(test.begin()) b1;
        auto container_ptr = trie->subrange(b1 = test.begin(), test.end());
        trie_t::iterator begin_test = container_ptr->begin();

        if ((i & 1) != 0) //odd entries nust have a terminal
        {
            tresult.assert_true(
                tools::container_equals(begin_test.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));

            tresult.assert_true(*begin_test == (double)i);
            container_ptr->next(begin_test);
        }
        auto a = std::begin(sorted_checks);
        for (; container_ptr->is_end(begin_test); container_ptr->next(begin_test), ++a)
        {
            auto strain_str = (test + *a);
            //print_hex(std::cout << "1)", strain_str);
            //print_hex(std::cout << "2)", begin_test.prefix());
            tresult.assert_true(begin_test.prefix() == strain_str);
            tresult.assert_true(*begin_test == (double)strain_str.length());
        }
    }
}
void test_TrieNoTran(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<SegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<SegmentManager, double> trie_t;
    std::map<std::string, double> standard;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::string rnd_buf;
    for (auto i = 0; i < 1024; ++i)
    {
        tools::randomize(rnd_buf, 1023, 1);
        //if (rnd_buf.length() > 14 * 60)
        //{
        //    std::uint8_t c1 = rnd_buf[0], c2 = rnd_buf[1], c3 = rnd_buf[2]; //c1 == 0x41 && c2==0x4f && c3==0x4b
        //    atoi("78");
        //}
        auto b = std::begin(rnd_buf);
        auto post = trie->insert(b, std::end(rnd_buf), (double)rnd_buf.length());
        if (post.first)
        {
            standard.emplace(rnd_buf, (double)rnd_buf.length());
        }
    }
    compare_containers(tresult, *trie, standard);
    trie.reset();
    tmngr1 = OP::trie::SegmentManager::open<SegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    //
    compare_containers(tresult, *trie, standard);
}
static auto module_suite = OP::utest::default_test_suite("Trie")
    ->declare(test_TrieCreation, "creation")
    ->declare(test_TrieInsert, "insertion")
    ->declare(test_TrieInsertGrow, "insertion-grow")
    //->declare(test_TrieGrowAfterUpdate, "grow-after-update")
    ->declare(test_TrieLowerBound, "lower_bound")
    ->declare(test_TrieSubtree, "subtree of prefix")
    ->declare(test_TrieNoTran, "trie no tran")
    ;
