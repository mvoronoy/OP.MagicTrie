#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/trie/Trie.h>
#include <op/trie/ranges/RangeUtils.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/TransactedSegmentManager.h>
#include <op/trie/ranges/FlattenRange.h>
#include <algorithm>
#include "test_comparators.h"


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
    os << '\n';
}
template <class Trie, class Map>
void compare_containers(OP::utest::TestResult &tresult, Trie& trie, Map& map)
{
    auto mi = std::begin(map);
    //for (auto xp : map)
    //{
    //    print_hex(tresult.info(), xp.first );
    //    tresult.info() << /*xp.first << */'=' << xp.second << '\n';
    //}
    auto ti = trie.begin();
    int n = 0;
    //order must be the same
    for (; trie.in_range(ti); trie.next(ti), ++mi, ++n)
    {
        //print_hex(tresult.info() << "1)", ti.key());
        //print_hex(tresult.info() << "2)", mi->first);
        tresult.assert_true(ti.key().length() == mi->first.length(), 
            OP_CODE_DETAILS(<<"step#"<< n << " has:" << ti.key().length() << ", while expected:" << mi->first.length()));
        tresult.assert_true(tools::container_equals(ti.key(), mi->first, &tools::sign_tolerant_cmp<atom_t>),
            OP_CODE_DETAILS(<<"step#"<< n << ", for key="<<(const char*)mi->first.c_str() << ", while obtained:" << (const char*)ti.key().c_str()));
        tresult.assert_that<equals>(*ti, mi->second,
            OP_CODE_DETAILS(<<"Associated value error, has:" << *ti << ", expected:" << mi->second ));
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
    std::string zero_ins { '_' };
    auto ir0 = trie->insert(zero_ins, 0.0);
    standard[zero_ins] = 0.0;

    const std::string stem1(260, 'a'); //total 260
    /*stem1_deviation1 - 1'a' for presence, 256'a' for exhausting stem container*/
    std::string stem1_deviation1{ std::string(257, 'a') + 'b'};
    //insert that reinforce long stem
    auto b1 = std::begin(stem1);
    auto ir1 = trie->insert(b1, std::end(stem1), v_order);
    tresult.assert_true(ir1.second, OP_CODE_DETAILS());
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir1.first.key()), std::end(ir1.first.key()),
        std::begin(stem1), std::end(stem1)
        ));
    tresult.assert_true(2 == trie->nodes_count(), "2 nodes must be create for long stems");
    tresult.assert_true(trie->size() == 2);

    standard[stem1] = v_order++;
    compare_containers(tresult, *trie, standard);
    
    auto ir2 = trie->insert(std::begin(stem1), std::end(stem1), v_order + 101.0);
    tresult.assert_false(
        ir2.second, 
        OP_CODE_DETAILS("Duplicate insert must not be allowed"));
    tresult.assert_true(trie->size() == 2);
    
    auto ir3 = trie->insert(stem1_deviation1.cbegin(), stem1_deviation1.cend(), v_order);
    tresult.assert_true(ir3.second, OP_CODE_DETAILS());
    //print_hex(std::cout << "1)", stem1_deviation1);
    //print_hex(std::cout << "2)", ir3.second.key());

    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir3.first.key()), std::end(ir3.first.key()),
        std::begin(stem1_deviation1), std::end(stem1_deviation1)
        ));
    standard[stem1_deviation1] = v_order++;
    compare_containers(tresult, *trie, standard);
    // test behaviour on range
    const std::string stem2(256, 'b');
    auto ir4 = trie->insert(std::begin(stem2), std::end(stem2), v_order);
    tresult.assert_true(ir4.second, OP_CODE_DETAILS());
    standard[stem2] = v_order++;
    tresult.assert_true(3 == trie->nodes_count(), "3 nodes must exists in the system");
    tresult.assert_true(trie->size() == 4);
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir4.first.key()), std::end(ir4.first.key()),
        std::begin(stem2), std::end(stem2)
        ));

    compare_containers(tresult, *trie, standard);
    //
    // test diversification
    //
    tresult.status_details() << "test diversification\n";
    std::string stem3 = { std::string(1, 'c') + std::string(256, 'a') };
    const std::string& const_stem3 = stem3;
    auto ir5 = trie->insert(std::begin(const_stem3), std::end(const_stem3), v_order);
    tresult.assert_true(ir5.second, OP_CODE_DETAILS());
    standard[stem3] = v_order++;
    tresult.assert_true(4 == trie->nodes_count(), "4 nodes must exists in the system");
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.first.key()), std::end(ir5.first.key()),
        std::begin(stem3), std::end(stem3)
        ));
    tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
    compare_containers(tresult, *trie, standard);
    //now make iteration back over stem3 and create diversification in 
    //std::cout << "***";
    for (auto i = 0; stem3.length() > 1; ++i)
    {
        //std::cout << "\b\b\b" << std::setbase(16) << std::setw(3) << std::setfill('0') << i;
        stem3.resize(stem3.length() - 1);
        stem3 += std::string(1, 'b');
        ir5 = trie->insert(b1 = std::begin(const_stem3), std::end(const_stem3), (double)stem3.length());
        tresult.assert_true(ir5.second, OP_CODE_DETAILS());
        standard[stem3] = (double)stem3.length();
        stem3.resize(stem3.length() - 1);
    }//
    //std::cout << trie->nodes_count()<< "\n";
    compare_containers(tresult, *trie, standard);
    //
    tresult.status_details() << "test diversification#2\n";
    std::string stem4 = { std::string(1, 'd') + std::string(256, 'a') };
    const std::string& const_stem4 = stem4;
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    tresult.assert_true(ir5.second, OP_CODE_DETAILS());
    standard[stem4] = v_order++;
    tresult.assert_that<equals>(260, trie->nodes_count(), "260 nodes must exists in the system");
    stem4.resize(stem4.length() - 1);
    stem4.append(std::string(258,'b'));
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    tresult.assert_true(ir5.second, OP_CODE_DETAILS());
    standard[stem4] = v_order++;
    tresult.assert_true(261 == trie->nodes_count(), "261 nodes must exists in the system");
    compare_containers(tresult, *trie, standard);
    stem4 += "zzzzz";
    ir5 = trie->insert(std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.first.key()), std::end(ir5.first.key()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
    compare_containers(tresult, *trie, standard);
    // test stem continuation
    // Sequence: "k", "kaa..a", "ka"
    stem4 = "k";
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.first.key()), std::end(ir5.first.key()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
    stem4 += std::string(256, 'a');
    ir5 = trie->insert(std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.first.key()), std::end(ir5.first.key()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
    compare_containers(tresult, *trie, standard);
    stem4 = "ka";
    ir5 = trie->insert(std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.first.key()), std::end(ir5.first.key()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
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
        tresult.assert_true(ins_res.second);
        tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));
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
        tresult.assert_true(ins_res.second);
        tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));

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
        tresult.assert_true(ins_res.second);
        tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));

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
    tresult.assert_true(tools::container_equals(test_seq[1], trie->lower_bound(b1, std::end(az)).key(), tools::sign_tolerant_cmp));

    const atom_t unexisting1[] = "zzz";
    np = std::begin(unexisting1);
    tresult.assert_true(trie->end() == trie->lower_bound(np, std::end(unexisting1)));
    
    const atom_t unexisting2[] = "jkl";
    np = std::begin(unexisting2);
    tresult.assert_true(trie->find(test_seq[5]) == trie->lower_bound(np, std::end(unexisting2)));

    for (auto i = 0; i < std::extent<decltype(test_seq)>::value - 1; ++i, x += 1.0)
    {
        const std::string& test = test_seq[i];
        auto lbit = trie->lower_bound(std::begin(test), std::end(test));

        tresult.assert_true(tools::container_equals(lbit.key(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_that<equals>(x, *lbit, "value mismatch");

        auto query = test_seq[i] + "a";
        auto lbit2 = trie->lower_bound(std::begin(query), std::end(query));

        //print_hex(std::cout << "1)", test_seq[i + 1]);
        //print_hex(std::cout << "2)", lbit2.key());
        tresult.assert_true(tools::container_equals(lbit2.key(), test_seq[i+1], &tools::sign_tolerant_cmp<atom_t>));

        auto lbit3 = trie->lower_bound(std::begin(test), std::end(test)-1);//take shorter key
        tresult.assert_true(tools::container_equals(lbit3.key(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_true(x == *lbit);
    }
    //handle case of stem_end
    std::string test_long(258, 'k');
    auto long_ins_res = trie->insert(test_long, 7.5);
    test_long += "aa";
    auto lbegin = std::begin(test_long);
    auto llong_res = trie->lower_bound(lbegin, std::end(test_long));
    tresult.assert_true(tools::container_equals(llong_res.key(), test_seq[5], &tools::sign_tolerant_cmp<atom_t>));
    //handle case of no_entry
    for (auto fl : test_seq)
    {
        auto fl_div = fl + "0";
        tresult.assert_true(trie->insert(fl_div, 75.).second, "Item must not exists");
        fl += "xxx";
        auto fl_res = trie->lower_bound(fl);
        tresult.assert_that<equals>(fl_res, trie->end(), "Item must not exists");
        fl_res = trie->lower_bound(fl_div);
        tresult.assert_that<equals>(fl_res.key(), fl_div, "Item must exists");

    }
}
void test_PrefixedFind(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t((atom_t*)"abc", 1.),
        p_t((atom_t*)"abc.1", 1.),
        p_t((atom_t*)"abc.12", 1.),
        p_t((atom_t*)"abc.122x", 1.9),
        p_t((atom_t*)"abc.123456789", 1.9),
        p_t((atom_t*)"abc.124x", 1.9),
        p_t((atom_t*)"abc.2", 2.),
        p_t((atom_t*)"abc.3", 1.3),
        p_t((atom_t*)"abc.333", 1.33),
        p_t((atom_t*)"def.", 2.0)
    };
    std::map<std::string, double> test_values;
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
    });
    tresult.info() << "prefixed lower_bound\n";
    auto i_root = trie->find(std::string("abc"));
    auto lw_ch1 = trie->lower_bound(i_root, std::string(".123"));
    tresult.assert_that<equals>(lw_ch1.key(), (const atom_t*)"abc.123456789", "key mismatch");
    tresult.assert_that<equals>(*lw_ch1, 1.9, "value mismatch");
    tresult.assert_that<equals>(trie->lower_bound(i_root, std::string(".xyz")), trie->end(), "iterator must be end");
    tresult.assert_that<equals>(trie->lower_bound(trie->end(), std::string(".123")), i_root, "iterator must point to 'abc'");
    lw_ch1 = trie->lower_bound(i_root, std::string(".1"));
    tresult.assert_that<equals>(lw_ch1.key(), (const atom_t*)"abc.1", "key mismatch");
    tresult.assert_that<equals>(*lw_ch1, 1., "value mismatch");
    //check case when no alternatives
    lw_ch1 = trie->lower_bound(i_root, std::string(".2"));
    tresult.assert_that<equals>(lw_ch1.key(), (const atom_t*)"abc.2", "key mismatch");
    tresult.assert_that<equals>(*lw_ch1, 2., "value mismatch");
    //check iterator recovery
    trie->insert(std::string("aa"), 3.0);
    trie->insert(std::string("ax"), 3.0);
    trie->insert(std::string("aa1"), 3.0);
    trie->insert(std::string("ax1"), 3.0);
    lw_ch1 = trie->lower_bound(i_root, std::string(".123"));
    tresult.assert_that<equals>(lw_ch1.key(), (const atom_t*)"abc.123456789", "key mismatch");

    tresult.info() << "prefixed find\n";
    auto fnd_aa = trie->find(std::string("aa"));
    tresult.assert_that<equals>(fnd_aa.key(), (const atom_t*)"aa", "key mismatch");
    auto fnd_copy = fnd_aa;
    size_t cnt = 0;
    trie->erase(fnd_copy, &cnt);
    tresult.assert_that<equals>(1, cnt, "Nothing was erased");
    //test that iterator recovers after erase
    lw_ch1 = trie->lower_bound(fnd_aa, std::string("1"));
    tresult.assert_that<equals>(lw_ch1.key(), (const atom_t*)"aa1", "key mismatch");

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
        if (post.second)
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
            //std::cout << n++ << std::endl;
            auto ins_res = trie->insert(std::begin(test), std::end(test), (double)test.length());
            tresult.assert_true(ins_res.second);
            tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));
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
        auto begin_test = container_ptr->begin();

        if ((i & 1) != 0) //odd entries must have a terminal
        {
            tresult.assert_true(
                tools::container_equals(begin_test.key(), test, &tools::sign_tolerant_cmp<atom_t>));

            tresult.assert_true(*begin_test == (double)i);
            container_ptr->next(begin_test);
        }
        auto a = std::begin(sorted_checks);
        auto cnt = 0;
        for (; container_ptr->in_range(begin_test); container_ptr->next(begin_test), ++a, ++cnt)
        {
            auto strain_str = (test + *a);
            //print_hex(std::cout << "1)", strain_str);
            //print_hex(std::cout << "2)", begin_test.key());
            tresult.assert_true(tools::container_equals(begin_test.key(), strain_str), "! strain == prefix");
            tresult.assert_true(*begin_test == (double)strain_str.length());
        }
        tresult.assert_true(a == sorted_checks.end());
        tresult.assert_true(cnt > 0);
    }
}
template <class Stream, class Co>
inline void print_co(Stream& os, const Co& co)
{
    for (auto i = co.begin(); co.in_range(i); co.next(i))
    {
        auto && k = i.key();
        print_hex(os, k);
        os << '=' << *i << '\n';
    }
}

template <class R1, class R2, class Sample>
void test_join(
    OP::utest::TestResult &tresult, std::shared_ptr< R1> r1, std::shared_ptr< R2> r2, const Sample& expected)
{
    auto comparator = [](const auto& left, const auto& right)->int {
        auto&&left_prefix = left.key(); //may be return by const-ref or by value
        auto&&right_prefix = right.key();//may be return by const-ref or by value
        return OP::trie::str_lexico_comparator(left_prefix.begin(), left_prefix.end(),
            right_prefix.begin(), right_prefix.end());
    };
    auto result1 = r1->join(r2, comparator);
    //print_co(std::cout << "===========>", r1);
    compare_containers(tresult, *result1, expected);
    //print_co(std::cout << "===========>", r2);
    //std::cout <<"<<<<<<<<<<<\n";
    auto result2 = r2->join(r1, comparator);
    compare_containers(tresult, *result2, expected);
}
void test_TrieSubtreeLambdaOperations(OP::utest::TestResult &tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const atom_string_t stems[] = { 
        
        (atom_t*)"adc", 
        (atom_t*)"x", 
        atom_string_t(256, 'a'),
        atom_string_t(256, 'a') + (atom_t*)("b")
    };
    auto n = 0;
    for (auto i : stems)
    {
        auto ins_res = trie->insert(std::begin(i), std::end(i), (double)i.length());
        tresult.assert_true(ins_res.second);
        tresult.assert_true(tools::container_equals(ins_res.first.key(), i, &tools::sign_tolerant_cmp<atom_t>));

        //std::cout << std::setfill('0') << std::setbase(16) << std::setw(2) << (unsigned)i << "\n";
    }

    std::map<atom_string_t, double> test_values;
    atom_string_t query1 ((const atom_t*)"a");
    atom_string_t query2 ((const atom_t*)"ad");
    auto container1 = trie->subrange(std::begin(query1), std::end(query1));
    //container1.for_each([&tresult](auto& i) {
    //    print_hex(tresult.info(), i.key());
    //});
    
    auto container2 = trie->subrange(std::begin(query2), std::end(query2));
    //tresult.info() << "======\n";
    //for (auto i = container2.begin(); container2.in_range(i); container2.next(i))
    //{
    //    print_hex(tresult.info(), i.key());
    //}
    test_values.emplace(stems[0], 3.);
    test_join(tresult, container1, container2, test_values);

    //
    //  Test empty
    //
    test_values.clear();
    atom_string_t query3((const atom_t*)"x");
    test_join(tresult, container1, trie->subrange(std::begin(query3), std::end(query3)), test_values);

    const atom_string_t stem_diver[] = {

        (atom_t*)"ma",
        (atom_t*)"madc",
        (atom_t*)"mb",
        (atom_t*)"mdef",
        (atom_t*)"mg",
        (atom_t*)"na",
        (atom_t*)"nad", //missed
        (atom_t*)"nadc",
        (atom_t*)"nb",
        (atom_t*)"ndef",
        (atom_t*)"nh",
        (atom_t*)"x",
    };
    std::for_each(std::begin(stem_diver), std::end(stem_diver), [&trie](const atom_string_t& s) {
        trie->insert(s, (double)s.length());
    });

    test_values.emplace((atom_t*)("a"), 2);
    test_values.emplace((atom_t*)"adc", 4);
    test_values.emplace((atom_t*)"b", 2);
    test_values.emplace((atom_t*)"def", 4);

    atom_string_t query4((const atom_t*)"m"), query5((const atom_t*)"n");
    test_join(tresult, 
        trie->subrange(std::begin(query4), std::end(query4))->map([](auto const& it) {
            return it.key().substr(1);
        }),
        trie->subrange(std::begin(query5), std::end(query5))->map([](auto const& it) {
            return it.key().substr(1);
        }),
        test_values);
}

void test_Flatten(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;
    /*Test data organized as group of prefixes each owns by 3 suffixes that intersect and have corresponding suffixes as well*/
    const p_t ini_data[] = {
        p_t((atom_t*)"1.abc", 10.0),
        p_t((atom_t*)"1.bcd", 10.0),
        p_t((atom_t*)"1.def", 10.0),

        p_t((atom_t*)"2.def", 20.0),
        p_t((atom_t*)"2.fgh", 20.0),
        p_t((atom_t*)"2.hij", 20.0),

        p_t((atom_t*)"3.hij", 30.0),
        p_t((atom_t*)"3.jkl", 30.0),
        p_t((atom_t*)"3.lmn", 30.0),

        p_t((atom_t*)"4.lmn", 0.0),
        p_t((atom_t*)"4.", -1.),//empty suffix

        //block of suffixes
        p_t((atom_t*)"abc", 1.0),
        p_t((atom_t*)"bcd", 1.0),
        p_t((atom_t*)"def", 1.5), //common suffix

        p_t((atom_t*)"fgh", 2.0),
        p_t((atom_t*)"hij", 2.5), //common suffix

        p_t((atom_t*)"jkl", 3.0),
        p_t((atom_t*)"lmn", 3.5) //common suffix for 3. & 4.
    };
    std::for_each(std::begin(ini_data), std::end(ini_data), [&trie](const p_t& s) {
        trie->insert(s.first, s.second);
    });
    auto _1_range = trie->subrange(atom_string_t((atom_t*)"1."));
    _1_range->for_each([](const auto& i) {
        std::cout << "{" << (const char*)i.key().c_str() << ", " << *i << "}\n";
    });
    auto suffixes_range = _1_range->map([](const auto& i) {
        return i.key().substr(2/*"1."*/);
    });
    suffixes_range->for_each([](const auto& i) {
        std::cout << "{" << (const char*)i.key().c_str() << ", " << *i << "}\n";
    });
    //-->>>>
    auto frange1 = make_flatten_range(suffixes_range, [&trie](const auto& i) {
        return trie->subrange(i.key());
    });
    std::cout << "Flatten result:\n";
    frange1->for_each([](const auto& i) {
        std::cout << "{" << (const char*)i.key().c_str() << ", " << *i << "}\n";
    });
    auto _1_flatten = trie->flatten_subrange(suffixes_range);
    std::map<atom_string_t, double> strain1 = {

        decltype(strain1)::value_type((atom_t*)"abc", 1.0),
        decltype(strain1)::value_type((atom_t*)"bcd", 1.0),
        decltype(strain1)::value_type((atom_t*)"def", 1.5),
    };
    tresult.assert_true(
        OP::trie::utils::map_equals(*_1_flatten, strain1), OP_CODE_DETAILS(<< "Simple flatten failed"));
}
void test_Erase(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t((atom_t*)"1.abc", 10.0),
        p_t((atom_t*)"2.abc", 10.0),
        p_t((atom_t*)"3.abc", 10.0)
    };
    std::map<std::string, double> test_values;
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
    });
    //add enormous long string
    atom_string_t lstr(270, 0);
    //make str of: 0,1,..255,0,1..13
    std::iota(lstr.begin(), lstr.end(), 0);
    trie->insert(lstr, lstr.length()+0.0);
    tresult.assert_true(trie->nodes_count() == 2, OP_CODE_DETAILS(<< "only 2 nodes must be allocated"));
    auto f = trie->find(lstr);
    auto tst_next ( f );
    ++tst_next;
    size_t cnt = 0;
    tresult.assert_that<equals>(tst_next, trie->erase(f, &cnt), "iterators aren't identical");
    tresult.assert_that<equals>(1, cnt, "Invalid count erased");

    compare_containers(tresult, *trie, test_values);

    const p_t seq_data[] = {
        p_t((atom_t*)"4.abc", 2.0),
        p_t((atom_t*)"4.ab", 3.0),
        p_t((atom_t*)"4.a", 4.0),
        p_t((atom_t*)"4", 1.0)
    };
    std::for_each(std::begin(seq_data), std::end(seq_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
    });
    const atom_string_t avg_key((const atom_t*)"4.ab");
    f = trie->find(avg_key);
    tst_next = f;
    ++tst_next;
    tresult.assert_that<equals>(tst_next, trie->erase(f), "iterators mismatch");
    test_values.erase(std::string((const char*)avg_key.c_str()));
    //
    std::cout << '\n';
    compare_containers(tresult, *trie, test_values);

    const atom_string_t no_entry_key((const atom_t*)"no-entry");
    f = trie->find(no_entry_key);
    tresult.assert_that<equals>(trie->end(), trie->erase(f, &cnt), "erase must 'end()'");
    tresult.assert_that<equals>(0, cnt, "erase must 'end()'");

    const atom_string_t edge_key((const atom_t*)"4.abc");
    f = trie->find(edge_key);
    tst_next = f;
    ++tst_next;
    tresult.assert_that<equals>(tst_next, trie->erase(f, &cnt), "iterator mismatch");
    tresult.assert_that<equals>(1, cnt, "iterator mismatch");

    test_values.erase(std::string((const char*)edge_key.c_str()));
    compare_containers(tresult, *trie, test_values);

    const atom_string_t short_key((const atom_t*)"4");
    f = trie->find(short_key);
    tst_next = f;
    ++tst_next;
    tresult.assert_that<equals>(tst_next, trie->erase(f), "iterator mismatch");
    tresult.assert_that<equals>(1, cnt, "count mismatch");

    test_values.erase(std::string((const char*)short_key.c_str()));
    compare_containers(tresult, *trie, test_values);
    //do random test
    constexpr int str_limit = 513;
    for (auto i = 0; i < 1024; ++i)
    {
        atom_string_t long_base(str_limit, 0);
        std::iota(long_base.begin(), long_base.end(), static_cast<std::uint8_t>(i));
        std::random_shuffle(long_base.begin(), long_base.end());
        std::vector<atom_string_t> chunks;
        for (auto k = 1; k < str_limit; k *= ((i & 1) ? 4:3))
        {
            atom_string_t prefix = long_base.substr(0, k);
            chunks.emplace_back(prefix);
        }

        std::random_shuffle(chunks.begin(), chunks.end());

        std::for_each(chunks.begin(), chunks.end(), [&](const atom_string_t& pref) {
            auto t = trie->insert(pref, pref.length()+0.0);
            std::string signed_str(pref.begin(), pref.end());
            auto m = test_values.emplace(signed_str, pref.length() + 0.0);
            tresult.assert_that<equals>(t.second, m.second, OP_CODE_DETAILS(<<" Wrong insert result"));
            tresult.assert_true(tools::container_equals(t.first.key(), m.first->first, &tools::sign_tolerant_cmp<atom_t>),
                OP_CODE_DETAILS(<< " for key=" << m.first->first << ", while obtained:" << (const char*)t.first.key().c_str()));
        });
        //take half of chunks vector
        auto n = chunks.size() / 2;
        //compare_containers(tresult, *trie, test_values);

        for (auto s : chunks)
        {
            //print_hex(std::cout << "[" << s.length() << "]", s);
            auto found = trie->find(s);
            auto next_i = found;
            ++next_i;
            tresult.assert_that<equals>(next_i, trie->erase(found, &cnt), "iterator mismatch");
            tresult.assert_that<less>(0, cnt, "wrong count");

            std::string signed_str(s.begin(), s.end());
            tresult.assert_true(test_values.erase(signed_str) != 0);
            if (!(--n))
                break;
            //std::cout << "~=>" << n << '\n';
            //compare_containers(tresult, *trie, test_values);
        }
    }
    compare_containers(tresult, *trie, test_values);
}
void test_Siblings(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t((atom_t*)"abc", 1.),
        p_t((atom_t*)"abc.1", 1.),
        p_t((atom_t*)"abc.1.2", 1.12),
        p_t((atom_t*)"abc.2", 1.2),
        p_t((atom_t*)"abc.3", 1.3),
        p_t((atom_t*)"abc.333", 1.33)
    };
    std::map<std::string, double> test_values;
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
    });
    auto i_root = trie->find(std::string("abc.1"));
    trie->next_sibling(i_root);
    tresult.assert_that<equals>(i_root.key(), (const atom_t*)"abc.2", "key mismatch");
    tresult.assert_that<equals>(*i_root, 1.2, "value mismatch");
    trie->next_sibling(i_root);
    tresult.assert_that<equals>(i_root.key(), (const atom_t*)"abc.3", "key mismatch");
    tresult.assert_that<equals>(*i_root, 1.3, "value mismatch");
    trie->next_sibling(i_root);
    tresult.assert_that<equals>(i_root, trie->end(), "run end failed");

    auto i_sub = trie->find(std::string("abc.1.2"));
    trie->next_sibling(i_sub);
    tresult.assert_that<equals>(i_sub, trie->end(), "run end failed");

    auto i_sup = trie->find(std::string("abc"));
    trie->next_sibling(i_sup);
    tresult.assert_that<equals>(i_sup, trie->end(), "run end failed");
}
void test_ChildSelector(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t((atom_t*)"abc", 1.),
        p_t((atom_t*)"abc.1", 1.),
        p_t((atom_t*)"abc.2", 1.),
        p_t((atom_t*)"abc.3", 1.3),
        p_t((atom_t*)"abc.333", 1.33)
    };
    std::map<std::string, double> test_values;
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
    });
    tresult.info() << "first/last child\n";
    auto i_root = trie->find(std::string("abc"));
    auto last_ch1 = trie->last_child(i_root);
    tresult.assert_that<equals>(last_ch1.key(), (const atom_t*)"abc.3", "key mismatch");
    tresult.assert_that<equals>(*last_ch1, 1.3, "value mismatch");
    auto first_ch1 = trie->first_child(i_root);//of 'abc'
    tresult.assert_that<equals>(first_ch1.key(), (const atom_t*)"abc.1", "key mismatch");
    tresult.assert_that<equals>(*first_ch1, 1., "value mismatch");

    auto last_ch1_3 = trie->last_child(last_ch1);
    tresult.assert_that<equals>((++last_ch1).key(), (const atom_t*)"abc.333", "origin iterator must not be changed while `last_child`");
    tresult.assert_that<equals>(last_ch1_3.key(), (const atom_t*)"abc.333", "next key mismatch");
    tresult.assert_that<equals>(*last_ch1_3, 1.33, "value mismatch");
    //check no-child case
    auto i_2 = trie->find(std::string("abc.2"));
    auto last_ch2 = trie->last_child(i_2);
    tresult.assert_that<equals>(last_ch2, trie->end(), "no-child case failed");
    auto first_ch2 = trie->first_child(i_2);
    tresult.assert_that<equals>(first_ch2, trie->end(), "no-child case failed");
    //check end() case
    auto i_3 = trie->find(std::string("x"));
    auto last_ch_end = trie->last_child(i_3);
    tresult.assert_that<equals>(last_ch_end, trie->end(), "end() case failed");
    auto first_ch_end = trie->first_child(i_3);
    tresult.assert_that<equals>(first_ch_end, trie->end(), "end() case failed");

    auto f_root = trie->find(std::string("abc.1"));
    auto first_ch1_end = trie->first_child(f_root);
    tresult.assert_that<equals>(first_ch1_end, trie->end(), "end() case failed");

    tresult.info() << "children ranges\n";
    auto r1 = trie->children_range(i_root);

    std::map<std::string, double> r1_test;
    r1_test.emplace(std::string(ini_data[1].first.begin(), ini_data[1].first.end()), ini_data[1].second);
    r1_test.emplace(std::string(ini_data[2].first.begin(), ini_data[2].first.end()), ini_data[2].second);
    r1_test.emplace(std::string(ini_data[3].first.begin(), ini_data[3].first.end()), ini_data[3].second);

    compare_containers(tresult, *r1, r1_test);

    r1 = trie->children_range(i_3);  //end()
    std::map<std::string, double> r1_empty;
    compare_containers(tresult, *r1, r1_empty);

}
void test_IteratorSync(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t((atom_t*)"a", 0.),
        p_t((atom_t*)"aa1", 1.),
        p_t((atom_t*)"aa2", 1.),
        p_t((atom_t*)"abc", 1.),
        p_t((atom_t*)"abc.12", 1.),
        p_t((atom_t*)"abc.122x", 1.9),
        p_t((atom_t*)"abc.123456789", 1.9),
        p_t((atom_t*)"abd.12", 1.),
    };
    std::map<std::string, double> test_values;
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
    });
    auto i_1 = trie->find(std::string("abc"));
    auto i_2 = trie->find(std::string("abc.12"));
    //check iterator recovery
    trie->insert(std::string("aa"), 3.0);
    trie->insert(std::string("ax"), 3.0);
    trie->insert(std::string("aa1"), 3.0);
    trie->insert(std::string("ax1"), 3.0);

    auto lw_ch1 = trie->lower_bound(i_1, std::string(".123"));
    tresult.assert_that<equals>(lw_ch1.key(), (const atom_t*)"abc.123456789", "key mismatch");
    auto lw_ch2 = trie->lower_bound(i_2, std::string(".122"));
    tresult.assert_that<equals>(lw_ch2.key(), (const atom_t*)"abc.122x", "key mismatch");
}

void test_TrieUpsert(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t((atom_t*)"aa1", 1.),
        p_t((atom_t*)"aa2", 1.),
        p_t((atom_t*)"abc", 1.),
        p_t((atom_t*)"abc.12", 1.),
        p_t((atom_t*)"abc.122x", 1),
        p_t((atom_t*)"abc.123456789", 1),
        p_t((atom_t*)"abd.12", 1.),
    };
    std::map<atom_string_t, double> test_values;
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);
        trie->insert(s.first, s.second);
        test_values.emplace(s.first, s.second);
    });
    compare_containers(tresult, *trie, test_values);
    //extend with upsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);
        
        s1.append(1, (atom_t)'x');
        trie->upsert(s1, 2.0);
        test_values.emplace(s1, 2.0);

        s1 = s1.substr(s1.length()-2);

        trie->upsert(s1, 2.0);
        test_values.emplace(s1, 2.0);
    });
    compare_containers(tresult, *trie, test_values);
    //update with upsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->upsert(s.first, 3.0);
        test_values.find(s.first)->second = 3.0;
    });
    compare_containers(tresult, *trie, test_values);
}
void test_TriePrefixedUpsert(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;
    std::map<atom_string_t, double> test_values;

    const atom_string_t s0((atom_t*)"a");
    auto start_pair = trie->insert(s0, -1.0);
    test_values.emplace(s0, -1.);
    compare_containers(tresult, *trie, test_values);

    const p_t ini_data[] = {
        p_t((atom_t*)"a1", 1.),
        p_t((atom_t*)"a2", 1.),
        p_t((atom_t*)"bc", 1.),
        p_t((atom_t*)"bc.12", 1.),
        p_t((atom_t*)"bc.122x", 1),
        p_t((atom_t*)"bc.123456789", 1),
        p_t((atom_t*)"bd.12", 1.),
    };
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);
        trie->prefixed_upsert(start_pair.first, s.first, s.second);
        test_values.emplace(s0 + s.first, s.second);
    });
    compare_containers(tresult, *trie, test_values);
    //special case to recover after erase
    atom_string_t abc_str((const atom_t*)"abc");
    auto abc_iter = trie->find(abc_str);
    tresult.assert_false(abc_iter == trie->end(), "abc must exists");
    auto abc_copy = abc_iter;
    tresult.assert_that<not<equals>>(trie->end(), trie->erase(abc_copy), "Erase failed");
    test_values.erase(abc_str);
    compare_containers(tresult, *trie, test_values);
    //copy again
    abc_copy = abc_iter;
    trie->prefixed_upsert(abc_iter, atom_string_t((const atom_t*)"bc.12"), 5.0);
    test_values[abc_str + (const atom_t*)"bc.12"] = 5.0;
    compare_containers(tresult, *trie, test_values);

    //extend with upsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);

        s1.append(1, (atom_t)'x');
        trie->prefixed_upsert(start_pair.first, s1, 2.0);
        test_values.emplace(s0+s1, 2.0);

        s1 = s1.substr(s1.length() - 2);

        trie->prefixed_upsert(start_pair.first, s1, 2.0);
        test_values.emplace(s0 + s1, 2.0);
    });
    compare_containers(tresult, *trie, test_values);
    //update with upsert
    tresult.assert_true(trie->insert(abc_str, 3.0).second);
    test_values.emplace(abc_str, 3.0);
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        auto r = trie->prefixed_upsert(start_pair.first, s.first, 3.0);
        auto key_str = s0 + s.first;

        tresult.assert_false(r.second, "Value already exists");
        tresult.assert_that<string_equals>(key_str, r.first.key(), "Value already exists");

        test_values.find(key_str)->second = 3.0;
    });
    compare_containers(tresult, *trie, test_values);
}

void test_TriePrefixedEraseAll(OP::utest::TestResult &tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;
    std::map<atom_string_t, double> test_values;

    const atom_string_t s0((atom_t*)"a");
    auto start_pair = trie->insert(s0, -1.0);
    tresult.assert_that<equals>(1, trie->nodes_count(), OP_CODE_DETAILS());
    test_values.emplace(s0, -1.);
    compare_containers(tresult, *trie, test_values);

    tresult.assert_that<equals>(0, trie->prefixed_erase_all(start_pair.first), OP_CODE_DETAILS());
    tresult.assert_that<equals>(1, trie->nodes_count(), OP_CODE_DETAILS());
    tresult.assert_that<equals>(0, trie->prefixed_erase_all(trie->end()), OP_CODE_DETAILS());
    tresult.assert_that<equals>(1, trie->nodes_count(), OP_CODE_DETAILS());

    const p_t ini_data[] = {
        p_t((atom_t*)"a1", 1.),
        p_t((atom_t*)"a2", 1.),
        p_t((atom_t*)"xyz", 1.),
        p_t((atom_t*)"klmnopqrstuffjfisdifsd sduf asdasjkdhasjhjkahaskdask asaskdhaskhdkasdasjdasjkdhaskasdjk hkasdjhdkashaskdaksdasjkhdjkash djkashkdashjkdhasjkhdkashdjkashdjkasklmnopqrstuffjfisdifsd sduf asdasjkdhasjhjkahaskdask asaskdhaskhdkasdasjdasjkdhaskasdjk hkasdjhdkashaskdaksdasjkhdjkash djkashkdashjkdhasjkhdkashdjkashdjkas", 11.1),
        p_t((atom_t*)"bc", 1.),
        p_t((atom_t*)"bc.12", 1.),
        p_t((atom_t*)"bc.122x", 1),
        p_t((atom_t*)"bc.123456789", 1),
        p_t((atom_t*)"bd.12", 1.),
    };
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(s.first, s.second);
    });
    compare_containers(tresult, *trie, test_values);

    auto abc_iter = trie->prefixed_erase_all(start_pair.first);
    //prepare test map, by removing all string that starts from 'a' and bigger than 1 char
    for (auto wi = test_values.begin();
        (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'a' && itm.first.length() > 1;})) != test_values.end();
        test_values.erase(wi++));
    compare_containers(tresult, *trie, test_values);
    //special case for restore iterator
    atom_string_t en2((const atom_t*)"bc");
    auto f2 = trie->find(en2);
    auto f2_copy = f2;
    tresult.assert_that<not<equals>>(trie->end(), f2, OP_CODE_DETAILS());
    trie->erase(f2_copy);
    test_values.erase(en2);
    compare_containers(tresult, *trie, test_values);

    tresult.assert_that<not<less>>(4, trie->prefixed_erase_all(f2), OP_CODE_DETAILS());
    //prepare test map, by removing all string that starts from 'bc' and bigger than 2 char
    for (auto wi = test_values.begin();
        (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'b' && itm.first[1] == (atom_t)'c' &&itm.first.length() > 1;})) != test_values.end();
        test_values.erase(wi++));

    compare_containers(tresult, *trie, test_values);

}

static auto module_suite = OP::utest::default_test_suite("Trie")
    ->declare(test_TrieCreation, "creation")
    ->declare(test_TrieInsert, "insertion")
    ->declare(test_TrieInsertGrow, "insertion-grow")
    //->declare(test_TrieGrowAfterUpdate, "grow-after-update")
    ->declare(test_TrieLowerBound, "lower_bound")
    ->declare(test_PrefixedFind, "prefixed find")
    ->declare(test_TrieSubtree, "subtree of prefix")
    ->declare(test_TrieSubtreeLambdaOperations, "lambda on subtree")
    ->declare(test_TrieNoTran, "trie no tran")
    ->declare(test_Flatten, "flatten")
    ->declare(test_Erase, "erase")
    ->declare(test_Siblings, "siblings")
    ->declare(test_ChildSelector, "child")
    ->declare(test_IteratorSync, "sync iterator")
    ->declare(test_TrieUpsert, "upsert")
    ->declare(test_TriePrefixedUpsert, "prefixed upsert")
    ->declare(test_TriePrefixedEraseAll, "prefixed erase_all")
    
    ;
