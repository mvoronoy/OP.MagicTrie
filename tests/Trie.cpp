#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/ranges/OrderedRange.h>
#include <op/trie/Trie.h>
#include <op/ranges/RangeUtils.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/TransactedSegmentManager.h>
#include <op/ranges/FlattenRange.h>
#include <op/trie/TrieRangeAdapter.h>

#include <algorithm>
#include "test_comparators.h"
#include "AtomStrLiteral.h"


using namespace OP::trie;
using namespace OP::utest;
const char* test_file_name = "trie.test";

template <class TStr = std::string>
struct lexicographic_less {
    bool operator() (const TStr& s1, const TStr& s2) const
    {
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end());
    }
};
void test_TrieCreation(OP::utest::TestResult& tresult)
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

#include "TrieTestUtils.h"
#include <random>
void test_TrieInsert(OP::utest::TestResult& tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::map<std::string, double> standard;
    double v_order = 0.0;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::string zero_ins{ '_' };
    auto ir0 = trie->insert(zero_ins, 0.0);
    standard[zero_ins] = 0.0;
    tresult.assert_that<equals>(trie->size(), 1, "Wrong counter val");

    const std::string stem1(260, 'a'); //total 260
    /*stem1_deviation1 - 1'a' for presence, 256'a' for exhausting stem container*/
    std::string stem1_deviation1{ std::string(257, 'a') + 'b' };
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
        OP_CODE_DETAILS()<<"Duplicate insert must not be allowed");
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
    stem4.append(std::string(258, 'b'));
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

void test_TrieInsertGrow(OP::utest::TestResult& tresult)
{
    std::random_device rd;
    std::mt19937 random_gen(rd());

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
    std::shuffle(std::begin(rand_idx), std::end(rand_idx), random_gen);
    for (auto i : rand_idx)
    {
        std::string test = std::string(1, (std::string::value_type)i) +
            stems[rand() % std::extent< decltype(stems) >::value];
        auto ins_res = trie->insert(std::begin(test), std::end(test), (double)test.length());
        tresult.assert_true(ins_res.second);
        tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));
        test_values[test] = (double)test.length();
    }
    tresult.assert_that<equals>(rand_idx.size(), trie->size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
}
void test_TrieUpdate(OP::utest::TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    auto tmp_end = trie->end();
    tresult.assert_that<equals>(0, trie->update(tmp_end, 0.0), "update of end is not allowed");

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
    tresult.assert_that<equals>(sizeof(ini_data) / sizeof(ini_data[0]), trie->size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //update all values 
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        auto pos = trie->find(s.first);
        tresult.assert_false(pos.is_end());

        trie->update(pos, 2.0);
        test_values[s.first] = 2.0;
        });
    compare_containers(tresult, *trie, test_values);
    // test update of erased
    auto to_erase = trie->find(ini_data[0].first);
    trie_t::iterator copy_of = to_erase;
    test_values.erase(ini_data[0].first);
    size_t n = 0;
    trie->erase(copy_of, &n);
    tresult.assert_that<equals>(1, n, "wrong item specified");
    tresult.assert_that<equals>(0, trie->update(to_erase, 3.0), "update must not operate erased item");
    tresult.assert_that<equals>(test_values.size(), trie->size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
}

void test_TrieGrowAfterUpdate(OP::utest::TestResult& tresult)
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
    //
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    const std::string& upd = test_seq[std::extent<decltype(test_seq)>::value / 2];
}
void test_TrieLowerBound(OP::utest::TestResult& tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    std::map<atom_string_t, double> test_values;
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const atom_string_t test_seq[] = { "abc"_astr, "bcd"_astr, "def"_astr, "fgh"_astr, "ijk"_astr, "lmn"_astr };

    double x = 0.0;
    for (auto i : test_seq)
    {
        const auto& test = i;
        auto ins_res = trie->insert(std::begin(test), std::end(test), x);
        tresult.assert_true(ins_res.second);
        tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));

        test_values[test] = x;
        x += 1.0;
    }
    trie.reset();
    tmngr1 = OP::trie::SegmentManager::open<TransactedSegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    x = 0.0;
    const atom_t* np = nullptr;
    tresult.assert_true(trie->end() == trie->lower_bound(np, np));
    const atom_t az[] = "az";  auto b1 = std::begin(az);
    tresult.assert_true(tools::container_equals(test_seq[1], trie->lower_bound(b1, std::end(az)).key(), 
        tools::sign_tolerant_cmp<typename std::string::value_type, typename atom_string_t::value_type>));

    const atom_t unexisting1[] = "zzz";
    np = std::begin(unexisting1);
    tresult.assert_true(trie->end() == trie->lower_bound(np, std::end(unexisting1)));

    const atom_t unexisting2[] = "jkl";
    np = std::begin(unexisting2);
    tresult.assert_true(trie->find(test_seq[5]) == trie->lower_bound(np, std::end(unexisting2)));

    for (auto i = 0; i < std::extent<decltype(test_seq)>::value - 1; ++i, x += 1.0)
    {
        const auto& test = test_seq[i];
        auto lbeg = std::begin(test);
        auto lbit = trie->lower_bound(lbeg, std::end(test));

        tresult.assert_true(tools::container_equals(lbit.key(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_that<equals>(x, *lbit, "value mismatch");

        auto query = test_seq[i] + "a"_astr;
        auto lbit2 = trie->lower_bound(query);

        //print_hex(std::cout << "1)", test_seq[i + 1]);
        //print_hex(std::cout << "2)", lbit2.key());
        tresult.assert_true(tools::container_equals(lbit2.key(), test_seq[i + 1], &tools::sign_tolerant_cmp<atom_t>));

        lbeg = std::begin(test);
        auto lbit3 = trie->lower_bound(lbeg, std::end(test) - 1);//take shorter key
        tresult.assert_true(tools::container_equals(lbit3.key(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_true(x == *lbit);
    }
    //handle case of stem_end
    atom_string_t test_long(258, 'k'_atom);
    auto long_ins_res = trie->insert(test_long, 7.5);
    test_long += "aa"_astr;
    auto lbegin = std::begin(test_long);
    auto llong_res = trie->lower_bound(lbegin, std::end(test_long));
    tresult.assert_true(tools::container_equals(llong_res.key(), test_seq[5], &tools::sign_tolerant_cmp<atom_t>));
    //handle case of no_entry
    for (auto fl : test_seq)
    {
        auto fl_div = fl + "0"_astr;
        tresult.assert_true(trie->insert(fl_div, 75.).second, "Item must not exists");
        fl += "xxx"_astr;
        auto fl_res = trie->lower_bound(fl);
        tresult.assert_that<equals>(fl_res, trie->end(), "Item must not exists");
        fl_res = trie->lower_bound(fl_div);
        tresult.assert_that<equals>(fl_res.key(), fl_div, "Item must exists");

    }
}
void test_PrefixedFind(OP::utest::TestResult& tresult)
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
    auto i_end = trie->end();
    tresult.assert_that<equals>(trie->lower_bound(i_end, std::string(".123")), i_root, "iterator must point to 'abc'");
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
    
    tresult.info()<<"check prefixed lower_bound after the range behaviour\n";
    i_root = trie->find("zzz"_astr);
    tresult.assert_false(trie->in_range(i_root), "iterator must be end()");
    auto find_after_end = trie->lower_bound(i_root, "abc"_astr);
    tresult.assert_that<equals>(find_after_end.key(), "abc"_astr, "iterator must point at 'abc'");

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

    tresult.info() << "check prefixed find after the range behaviour\n";
    i_root = trie->find("zzz"_astr);
    tresult.assert_false(trie->in_range(i_root), "iterator must be end()");
    find_after_end = trie->find(i_root, "abc"_astr);
    tresult.assert_that<equals>(find_after_end.key(), "abc"_astr, "iterator must point at 'abc'");
}

void test_TrieNoTran(OP::utest::TestResult& tresult)
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
    tresult.assert_that<equals>(trie->size(), standard.size(), "Size is wrong");
    trie.reset();
    tmngr1 = OP::trie::SegmentManager::open<SegmentManager>(test_file_name);
    trie = trie_t::open(tmngr1);
    //
    tresult.assert_that<equals>(trie->size(), standard.size(), "Size is wrong");
    compare_containers(tresult, *trie, standard);
}
void test_TrieSubtree(OP::utest::TestResult& tresult)
{

    std::random_device rd;
    std::mt19937 random_gen(rd());

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
    std::shuffle(std::begin(rand_idx), std::end(rand_idx), random_gen);
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
        auto container_ptr = trie->prefixed_range(b1 = test.begin(), test.end());
        auto begin_test = container_ptr->begin();

        if ((i & 1) != 0) //odd entries must have a terminal
        {
            tresult.assert_true(
                tools::container_equals(begin_test.key(), test, &tools::sign_tolerant_cmp<atom_t>));

            tresult.assert_true(begin_test.value() == (double)i);
            container_ptr->next(begin_test);
        }
        auto a = std::begin(sorted_checks);
        auto cnt = 0;
        for (; container_ptr->in_range(begin_test); container_ptr->next(begin_test), ++a, ++cnt)
        {
            auto strain_str = (test + *a);
            //print_hex(tresult.debug() << "1)", strain_str);
            //print_hex(tresult.debug() << "2)", begin_test.key());
            tresult.assert_true(tools::container_equals(begin_test.key(), strain_str), "! strain == prefix");
            tresult.assert_true(begin_test.value() == (double)strain_str.length());
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
        auto&& k = i.key();
        print_hex(os, k);
        os << '=' << *i << '\n';
    }
}

template <class R1, class R2, class Sample>
void test_join(
    OP::utest::TestResult& tresult, std::shared_ptr< R1 const > r1, std::shared_ptr< R2 const > r2, const Sample& expected)
{
    auto result1 = r1->join(r2);
    //print_co(std::cout << "===========>", r1);
    compare_containers(tresult, *result1, expected);
    //print_co(std::cout << "===========>", r2);
    //std::cout <<"<<<<<<<<<<<\n";
    auto result2 = r2->join(r1);
    compare_containers(tresult, *result2, expected);
}
void test_TrieSubtreeLambdaOperations(OP::utest::TestResult& tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    // Populate trie with unique strings in range from [0..255]
    // this must cause grow of root node
    const atom_string_t stems[] = {
        "adc"_atom,
        "x"_atom,
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
    atom_string_t query1("a"_atom);
    atom_string_t query2("ad"_atom);
    auto container1 = trie->prefixed_range(query1);
    //tresult.info() << "====== container:1\n";
    //container1->for_each([&tresult](auto& i) {
    //    print_hex(tresult.info(), i.key());
    //});

    auto container2 = trie->prefixed_range(query2);
    //tresult.info() << "====== container:2\n";
    //container2->for_each([&tresult](auto& i) {
    //    print_hex(tresult.info(), i.key());
    //});
    test_values.emplace(stems[0], 3.);
    test_join(tresult, container1, container2, test_values);

    //
    //  Test empty
    //
    test_values.clear();
    atom_string_t query3("x"_atom);
    test_join(tresult, container1, trie->prefixed_range(std::begin(query3), std::end(query3)), test_values);

    const atom_string_t stem_diver[] = {
        "ma"_atom,
        "madc"_atom,
        "mb"_atom,
        "mdef"_atom,
        "mg"_atom,
        "ma"_atom,
        "madc"_atom,
        "mb"_atom,
        "mdef"_atom,
        "ng"_atom,
        "na"_atom,
        "nad"_atom, //missed
        "nadc"_atom,
        "nb"_atom,
        "ndef"_atom,
        "nh"_atom,
        "x"_atom
    };
    std::for_each(std::begin(stem_diver), std::end(stem_diver), [&trie](const atom_string_t& s) {
        trie->insert(s, (double)s.length());
        });
    std::map<atom_string_t, double> join_src = {
        {"mdef"_atom, 4},
        {"mg"_atom, 2},
        {"ma"_atom, 2},
        {"madc"_atom, 4},
        {"mb"_atom, 2},
        {"mdef"_atom, 4},
        {"ng"_atom, 2},
        {"na"_atom, 2},
        {"nadc"_atom, 4},
        {"nb"_atom, 2},
        {"y"_atom, 1}
    };
    auto join_src_range = OP::ranges::make_range_of_map(join_src);

    test_values.emplace("mdef"_atom, 4);
    test_values.emplace("mg"_atom, 2);
    test_values.emplace("ma"_atom, 2);
    test_values.emplace("madc"_atom, 4);
    test_values.emplace("mb"_atom, 2);
    test_values.emplace("mdef"_atom, 4);
    /*test_values.emplace("ng"_atom, 2);
    test_values.emplace("na"_atom, 2);
    test_values.emplace("nadc"_atom, 4);
    test_values.emplace("nb"_atom, 2);*/

    atom_string_t query4("m"_atom), query5("a"_atom);
    test_join(tresult,
        trie->prefixed_range(query4),
        join_src_range,
        test_values);

    test_values.clear();
    test_join(tresult,
        trie->prefixed_range(query5),
        join_src_range,
        test_values);
}

void test_Flatten(OP::utest::TestResult& tresult)
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
    auto _1_range = trie->prefixed_range(atom_string_t((atom_t*)"1."));
    _1_range->for_each([](const auto& i) {
        std::cout << "{" << (const char*)i.key().c_str() << " = " << i.value() << "}\n";
        });
    auto suffixes_range = _1_range->map([](const typename decltype(_1_range)::element_type::iterator& i)->atom_string_t {
        return i.key().substr(2/*"1."*/);
        });
    suffixes_range->for_each([](const auto& i) {
        std::cout << "{{" << (const char*)i.key().c_str() << ", " << i.value() << "}}\n";
        });
    //-->>>>
    using or_t = OP::ranges::OrderedRange<typename trie_t::key_t, typename trie_t::value_t>;
    auto frange1 = suffixes_range->flatten([&trie](const auto& i) -> std::shared_ptr<or_t const> {
        return trie->prefixed_range(i.key());
        });
    std::cout << "Flatten result:\n";
    frange1->for_each([](const auto& i) {
        std::cout << "{" << (const char*)i.key().c_str() << ", " << i.value() << "}\n";
        });
    auto _1_flatten = trie->flatten_subrange(suffixes_range);
    std::map<atom_string_t, double> strain1 = {

        decltype(strain1)::value_type((atom_t*)"abc", 1.0),
        decltype(strain1)::value_type((atom_t*)"bcd", 1.0),
        decltype(strain1)::value_type((atom_t*)"def", 1.5),
    };
    tresult.assert_true(
        OP::ranges::utils::map_equals(*_1_flatten, strain1), OP_CODE_DETAILS(<< "Simple flatten failed"));
    tresult.debug() << "Scenario #2\n";

    typedef std::map<atom_string_t, double/*, lex_less*/> test_container_t;
    test_container_t src1 = {
        {"1."_atom, 1.0},
        {"2."_atom, 2.0},
        {"3."_atom, 3.0}
    };
    auto r1_src1 = OP::ranges::make_range_of_map(src1);
    auto fres2 = r1_src1->flatten([&trie](const auto& i) {
        const auto& k = i.key();
        return trie
            ->prefixed_range(i.key())
            /*[!] Uncoment to test compile time error "DeflateFunction must produce range that support ordering"
            ->map([&k](const auto& i) {
                return OP::ranges::key_discovery::key(i).substr(k.length());
            }) */
            ;
        });

    test_container_t strain_fm = {
        { "1.abc"_astr, 10.000000 },
        { "1.bcd"_astr, 10.000000 },
        { "1.def"_astr, 10.000000 },
        { "2.def"_astr, 20.000000 },
        { "2.fgh"_astr, 20.000000 },
        { "2.hij"_astr, 20.000000 },
        { "3.hij"_astr, 30.000000 },
        { "3.jkl"_astr, 30.000000 },
        { "3.lmn"_astr, 30.000000 },
    };
    fres2->for_each([](const auto& i) {
        //std::cout << "\\/{" << OP::ranges::key_discovery::key(i).c_str() << ", " << *i << "}\\/\n";
        });
    compare_containers(tresult, *fres2, strain_fm);

}
void test_TrieSectionRange(OP::utest::TestResult& tresult)
{

    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;

    const p_t ini_data[] = {
        p_t("1.abc"_astr, 1.0),
        p_t("1.abc.1"_astr, 1.1),
        p_t("1.abc.2"_astr, 1.2),
        p_t("1.abc.3"_astr, 1.3),
        p_t("1.def.1"_astr, 1.4),
        p_t("1.def"_astr, 1.5),
        p_t("1."_astr, 1.5),

        p_t("2.abc"_astr, 2.0),
        p_t("2.abc.1"_astr, 2.0),
        p_t("2."_astr, 2.0),

        p_t("3.abc"_astr, 3.1),
        p_t("3.abc.3"_astr, 3.2),
        p_t("3."_astr, 3.0),

        p_t("4."_astr, 4.0)
    };

    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        });

    tresult.debug() << "Test no suffixes with terminal string without branching\n";
    auto _1r = trie->section_range("4."_astr);
    tresult.assert_false(_1r->in_range(_1r->begin()), "Suffix of terminal string without branching must be empty");

    tresult.debug() << "Test base scenario\n";
    auto _2r = trie->section_range("1."_astr);
    std::map<atom_string_t, double> strain1 = {
        p_t("abc"_astr, 1.0),
        p_t("abc.1"_astr, 1.1),
        p_t("abc.2"_astr, 1.2),
        p_t("abc.3"_astr, 1.3),
        p_t("def.1"_astr, 1.4),
        p_t("def"_astr, 1.5),
    };
    //_2r->for_each([](const auto& i) {
    //    std::cout << "{" << (const char*)i.key().c_str() << ", " << *i << "}\n";
    //});
    tresult.assert_true(
        OP::ranges::utils::map_equals(*_2r, strain1), OP_CODE_DETAILS(<< "Simple section failed"));

    std::map<atom_string_t, double> strain2 = {
        p_t(".1"_astr, 1.1),
        p_t(".2"_astr, 1.2),
        p_t(".3"_astr, 1.3)

    };
    auto _3r = trie->section_range("1.abc"_astr);
    tresult.assert_true(
        OP::ranges::utils::map_equals(*_3r, strain2), OP_CODE_DETAILS(<< "Narrowed section failed"));

    tresult.debug() << "Test flatten-range scenario\n";
    auto lookup = trie->sibling_range("1."_astr);
    lookup->for_each([](const auto& i) {
        std::cout << "////" << (const char*)i.key().c_str() << ", " << i.value() << "}\n";
        });
    auto _4r = trie->sibling_range("1."_astr)->flatten([&](auto const& i) {
        return trie->section_range(i.key());
        });
    std::map<atom_string_t, std::set<double>> strain4 = {
        {"abc"_astr, {1, 2, 3.1}},
        {"abc.1"_astr, {1.1, 2}},
        {"abc.2"_astr, {1.2}},
        {"abc.3"_astr, {3.2, 1.3}},
        {"def"_astr, {1.5}},
        {"def.1"_astr, {1.4}}
    };
    size_t n = 0;
    const size_t n4r = _4r->count();
    tresult.assert_that<greater>(n4r, 0, OP_CODE_DETAILS(<< "Flatten range must not be empty"));
    _4r->for_each([&](const auto& i) {
        std::cout << "{" << (const char*)i.key().c_str() << "=" << i.value() << "}\n";
        //need manually compare
        auto& key = i.key();
        auto value = i.value();
        auto found = strain4.find(key);
        tresult.assert_that<logical_not<equals>>(strain4.end(), found, OP_CODE_DETAILS(<< "Key not found:" << (const char*)key.c_str()));
        auto& value_set = found->second;
        tresult.assert_that<string_equals>(key, found->first, OP_CODE_DETAILS(<< "Key compare failed:" << (const char*)key.c_str()));
        tresult.assert_that<logical_not<equals>>(value_set.end(), value_set.find(value),
            OP_CODE_DETAILS(<< "Value compare failed:" << value << " of key:" << (const char*)key.c_str()));
        ++n;
        value_set.erase(value);
        if (value_set.empty())
            strain4.erase(found);
        });
    tresult.assert_that<equals>(n, n4r, OP_CODE_DETAILS(<< "Result sets are not equal size:" << n << " vs " << n4r));
}
void test_Erase(OP::utest::TestResult& tresult)
{
    std::random_device rd;
    std::mt19937 random_gen(rd());

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
    trie->insert(lstr, lstr.length() + 0.0);
    tresult.assert_true(trie->nodes_count() == 2, OP_CODE_DETAILS(<< "only 2 nodes must be allocated"));
    auto f = trie->find(lstr);
    auto tst_next(f);
    ++tst_next;
    size_t cnt = 0;
    tresult.assert_that<equals>(tst_next, trie->erase(f, &cnt), "iterators aren't identical");
    tresult.assert_that<equals>(1, cnt, "Invalid count erased");

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
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

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
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
        std::shuffle(long_base.begin(), long_base.end(), random_gen);
        std::vector<atom_string_t> chunks;
        for (auto k = 1; k < str_limit; k *= ((i & 1) ? 4 : 3))
        {
            atom_string_t prefix = long_base.substr(0, k);
            chunks.emplace_back(prefix);
        }

        std::shuffle(chunks.begin(), chunks.end(), random_gen);

        std::for_each(chunks.begin(), chunks.end(), [&](const atom_string_t& pref) {
            auto t = trie->insert(pref, pref.length() + 0.0);
            std::string signed_str(pref.begin(), pref.end());
            auto m = test_values.emplace(signed_str, pref.length() + 0.0);
            tresult.assert_that<equals>(t.second, m.second, OP_CODE_DETAILS(<< " Wrong insert result"));
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
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
}
void test_Siblings(OP::utest::TestResult& tresult)
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
    tresult.assert_that<equals>(i_root.key(), "abc.2"_astr, "key mismatch");
    tresult.assert_that<equals>(*i_root, 1.2, "value mismatch");
    trie->next_sibling(i_root);
    tresult.assert_that<equals>(i_root.key(), "abc.3"_astr, "key mismatch");
    tresult.assert_that<equals>(*i_root, 1.3, "value mismatch");
    trie->next_sibling(i_root);
    tresult.assert_that<equals>(i_root, trie->end(), "run end failed");

    auto i_sub = trie->find(std::string("abc.1.2"));
    trie->next_sibling(i_sub);
    tresult.assert_that<equals>(i_sub.key(), "abc.2"_astr, "key mismatch");

    i_sub = trie->find(std::string("abc.3"));
    trie->next_sibling(i_sub);
    tresult.assert_that<equals>(i_sub, trie->end(), "run end failed");

    auto i_sup = trie->find(std::string("abc"));
    trie->next_sibling(i_sup);
    tresult.assert_that<equals>(i_sup, trie->end(), "run end failed");
}
void test_ChildSelector(OP::utest::TestResult& tresult)
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
        p_t((atom_t*)"abc.333", 1.33),
        p_t((atom_t*)"abd", 2.0)
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
    // .333 is not a direct child
    //r1_test.emplace(std::string(ini_data[4].first.begin(), ini_data[4].first.end()), ini_data[4].second);

    compare_containers(tresult, *r1, r1_test);

    r1 = trie->children_range(i_3);  //end()
    std::map<std::string, double> r1_empty;
    compare_containers(tresult, *r1, r1_empty);

}
void test_IteratorSync(OP::utest::TestResult& tresult)
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

void test_TrieUpsert(OP::utest::TestResult& tresult)
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
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //extend with upsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);

        s1.append(1, (atom_t)'x');
        trie->upsert(s1, 2.0);
        test_values.emplace(s1, 2.0);

        s1 = s1.substr(s1.length() - 2);

        trie->upsert(s1, 2.0);
        test_values.emplace(s1, 2.0);
        });
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //update with upsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->upsert(s.first, 3.0);
        test_values.find(s.first)->second = 3.0;
        });
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
}
void test_TriePrefixedInsert(OP::utest::TestResult& tresult)
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
        trie->prefixed_insert(start_pair.first, s.first, s.second);
        test_values.emplace(s0 + s.first, s.second);
        });
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //special case to recover after erase
    atom_string_t abc_str((const atom_t*)"abc");
    auto abc_iter = trie->find(abc_str);
    tresult.assert_false(abc_iter == trie->end(), "abc must exists");
    auto abc_copy = abc_iter;
    tresult.assert_that<logical_not<equals>>(trie->end(), trie->erase(abc_copy), "Erase failed");
    test_values.erase(abc_str);
    compare_containers(tresult, *trie, test_values);
    //copy again
    abc_copy = abc_iter;
    trie->prefixed_insert(abc_iter, atom_string_t((const atom_t*)"bc.12"), 5.0);
    test_values[abc_str + (const atom_t*)"bc.12"] = 5.0;

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);

    //extend with INsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);

        s1.append(1, (atom_t)'x');
        trie->prefixed_insert(start_pair.first, s1, 2.0);
        test_values.emplace(s0 + s1, 2.0);

        s1 = s1.substr(s1.length() - 2);

        trie->prefixed_insert(start_pair.first, s1, 2.0);
        test_values.emplace(s0 + s1, 2.0);
        });

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //fail update with insert
    tresult.assert_true(trie->insert(abc_str, 3.0).second);
    test_values.emplace(abc_str, 3.0);
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        auto r = trie->prefixed_insert(start_pair.first, s.first, 3.0);
        auto key_str = s0 + s.first;

        tresult.assert_false(r.second, "Value already exists");
        tresult.assert_that<string_equals>(key_str, r.first.key(), "Value already exists");

        });

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
}

void test_TriePrefixedUpsert(OP::utest::TestResult& tresult)
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

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
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
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);

    //special case to recover after erase
    atom_string_t abc_str((const atom_t*)"abc");
    auto abc_iter = trie->find(abc_str);
    tresult.assert_false(abc_iter == trie->end(), "abc must exists");
    auto abc_copy = abc_iter;
    tresult.assert_that<logical_not<equals>>(trie->end(), trie->erase(abc_copy), "Erase failed");
    test_values.erase(abc_str);

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //copy again
    abc_copy = abc_iter;
    trie->prefixed_upsert(abc_iter, atom_string_t((const atom_t*)"bc.12"), 5.0);
    test_values[abc_str + (const atom_t*)"bc.12"] = 5.0;

    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);

    //extend with upsert
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);

        s1.append(1, (atom_t)'x');
        trie->prefixed_upsert(start_pair.first, s1, 2.0);
        test_values.emplace(s0 + s1, 2.0);

        s1 = s1.substr(s1.length() - 2);

        trie->prefixed_upsert(start_pair.first, s1, 2.0);
        test_values.emplace(s0 + s1, 2.0);
        });
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
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
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
}

void test_TriePrefixedEraseAll(OP::utest::TestResult& tresult)
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
    auto check_iterator_restore{ start_pair.first };
    tresult.assert_that<equals>(1, trie->nodes_count(), OP_CODE_DETAILS());
    test_values.emplace(s0, -1.);
    compare_containers(tresult, *trie, test_values);

    tresult.assert_that<equals>(1, trie->prefixed_erase_all(start_pair.first), OP_CODE_DETAILS());
    tresult.assert_that<equals>(1, trie->nodes_count(), OP_CODE_DETAILS());
    tresult.assert_that<equals>(0, trie->size(), OP_CODE_DETAILS());
    trie->insert(s0, -1.0);
    tresult.assert_that<equals>(1, trie->prefixed_key_erase_all(s0), OP_CODE_DETAILS());
    tresult.assert_that<equals>(trie->begin(), trie->end(), OP_CODE_DETAILS());

    auto erase_from = trie->end();
    tresult.assert_that<equals>(0, trie->prefixed_erase_all(erase_from), OP_CODE_DETAILS());
    tresult.assert_that<equals>(1, trie->nodes_count(), OP_CODE_DETAILS());

    test_values.erase(s0);

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

    auto abc_iter = trie->prefixed_key_erase_all(s0);
    //prepare test map, by removing all string that starts from 'a' and bigger than 1 char
    for (auto wi = test_values.begin();
        (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'a' /*&& itm.first.length() > 1*/;})) != test_values.end();
        test_values.erase(wi++));
    compare_containers(tresult, *trie, test_values);
    //special case for restore iterator
    atom_string_t en2((const atom_t*)"bc");
    auto f2 = trie->find(en2);
    auto f2_copy = f2;
    tresult.assert_that<logical_not<equals>>(trie->end(), f2, OP_CODE_DETAILS());
    trie->erase(f2_copy);
    test_values.erase(en2);
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    f2_copy = f2;
    tresult.assert_that<equals>(0, trie->prefixed_erase_all(f2_copy), OP_CODE_DETAILS());
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //put back
    tresult.assert_true(trie->insert(en2, 0.).second);
    test_values.emplace(en2, 0.);

    //prepare test map, by removing all string that starts from 'bc' and bigger than 2 char
    for (auto wi = test_values.begin();
        (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'b' && itm.first[1] == (atom_t)'c' && itm.first.length() > 2;})) != test_values.end();
        test_values.erase(wi++));

    tresult.assert_that<equals>(3, trie->prefixed_erase_all(f2), OP_CODE_DETAILS());
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);

}

void test_TriePrefixedKeyEraseAll(OP::utest::TestResult& tresult)
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


    tresult.assert_that<equals>(1, trie->prefixed_key_erase_all(s0), OP_CODE_DETAILS());
    tresult.assert_that<equals>(trie->begin(), trie->end(), OP_CODE_DETAILS());
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
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);

    auto abc_iter = trie->prefixed_key_erase_all(s0);
    //prepare test map, by removing all string that starts from 'a' and bigger than 1 char
    for (auto wi = test_values.begin();
        (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'a' && itm.first.length() > 1;})) != test_values.end();
        test_values.erase(wi++));
    tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
    compare_containers(tresult, *trie, test_values);
    //special case for restore iterator
    atom_string_t en2((const atom_t*)"bc");
    auto f2 = trie->find(en2);
    auto f2_copy = f2;
    tresult.assert_that<logical_not<equals>>(trie->end(), f2, OP_CODE_DETAILS());
    trie->erase(f2_copy);
    test_values.erase(en2);
    compare_containers(tresult, *trie, test_values);
    f2_copy = f2;
    //put back
    tresult.assert_true(trie->insert(en2, 0.).second);
    test_values.emplace(en2, 0.);
    tresult.assert_that<equals>(4, trie->prefixed_key_erase_all(en2), OP_CODE_DETAILS());

    //prepare test map, by removing all string that starts from 'bc' and bigger than 2 char
    for (auto wi = test_values.begin();
        (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'b' && itm.first[1] == (atom_t)'c';})) != test_values.end();
        test_values.erase(wi++));

    compare_containers(tresult, *trie, test_values);
    //  erase partial
    atom_string_t en3((const atom_t*)"xy");
    tresult.assert_that<equals>(1, trie->prefixed_key_erase_all(en3), OP_CODE_DETAILS());
    test_values.erase((const atom_t*)"xyz");
    compare_containers(tresult, *trie, test_values);


    const p_t ini_data2[] = {
        p_t((atom_t*)"gh", 1.),
        p_t((atom_t*)"gh.", 1.),
        p_t((atom_t*)"gh.1", 1.),
        p_t((atom_t*)"gh.12", 1.),
        p_t((atom_t*)"gh.2", 1.),
    };
    std::for_each(std::begin(ini_data2), std::end(ini_data2), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(s.first, s.second);
        });
    compare_containers(tresult, *trie, test_values);

    // check lower-bound is less then key (avoid infinit loop)
    atom_string_t en4((const atom_t*)"efghijklm");
    tresult.assert_that<equals>(0, trie->prefixed_key_erase_all(en4), OP_CODE_DETAILS());
    compare_containers(tresult, *trie, test_values);

}
void test_Range(OP::utest::TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;
    std::map<atom_string_t, double> test_values;

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

    auto i = test_values.begin();

    tresult.assert_that<equals>(9, trie->range()
        ->for_each([&](auto const& kv) {
            tresult.assert_true(tools::container_equals(kv.key(), i->first, &tools::sign_tolerant_cmp<atom_t>),
                OP_CODE_DETAILS(<< "error for key=" << (const char*)i->first.c_str() << ", while obtained:" << (const char*)kv.key().c_str()));
            tresult.assert_that<equals>(kv.value(), i->second,
                OP_CODE_DETAILS(<< "Associated value error, has:" << kv.value() << ", expected:" << i->second));
            ++i;
            }), "wrong counter");
    //test take_while semantic
    atom_t last_letter = (atom_t)'z';
    tresult.assert_that<equals>(7, trie->range()
        ->awhile([&](auto const& kv)->bool {
            last_letter = kv.key()[0];
            return kv.key()[0] <= (atom_t)'b';
            }), "wrong counter");
    tresult.assert_true(last_letter == (atom_t)'k');
}
void test_NativeRangeSupport(OP::utest::TestResult& tresult)
{
    tresult.info() << "apply lower_bound on container with native support\n";
    std::map<atom_string_t, double> src1;
    src1.emplace("a"_atom, 1.0);
    src1.emplace("ab"_atom, 1.0);
    src1.emplace("b"_atom, 1.0);
    src1.emplace("bc"_atom, 1.0);
    src1.emplace("c"_atom, 1.0);
    src1.emplace("cd"_atom, 1.0);
    src1.emplace("d"_atom, 1.0);
    src1.emplace("def"_atom, 1.0);
    src1.emplace("g"_atom, 1.0);
    src1.emplace("xyz"_atom, 1.0);

    //note: no transactions!
    auto tmngr1 = OP::trie::SegmentManager::create_new<OP::trie::SegmentManager>("trie2range.test",
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    using trie_t = OP::trie::Trie<OP::trie::SegmentManager, double>;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
    for (const auto& pair : src1) trie->insert(pair.first, pair.second);

    auto flt_src = trie
        ->range()
        ->filter([](auto it) { return it.key().length() > 1/*peek long enough*/; })
        ;
    auto found1 = flt_src->lower_bound(reinterpret_cast<const atom_t*>("f")); //pretty sure 'f' not exists so correct answer are 'g' and 'xyz', but filter skips (len > 1)
    tresult.assert_true(flt_src->in_range(found1), OP_CODE_DETAILS(<< "end of the range is wrong"));

    tresult.assert_that<equals>(
        found1.key(),
        "xyz"_atom,
        OP_CODE_DETAILS(<< "lower_bound must point 'XYZ'")
        );

}
void test_TrieCheckExists(OP::utest::TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;
    std::map<atom_string_t, double> test_values;

    typedef std::pair<atom_string_t, double> p_t;

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
        trie->insert(s.first, s.second);
        test_values.emplace(s.first, s.second);
        });
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        atom_string_t s1(s.first);
        tresult.assert_true(trie->check_exists(s1));
        });

    atom_string_t probe((const atom_t*)"a");
    tresult.assert_false(trie->check_exists(probe));
    probe = ((const atom_t*)"a22");
    tresult.assert_false(trie->check_exists(probe));
    probe = ((const atom_t*)"bc.1");
    tresult.assert_false(trie->check_exists(probe));
    probe = (const atom_t*)"bd.1";
    tresult.assert_false(trie->check_exists(probe));
    probe = (const atom_t*)"bd.122";
    tresult.assert_false(trie->check_exists(probe));

    probe = (const atom_t*)"xyz";
    tresult.assert_false(trie->check_exists(probe));
}
void test_NextLowerBound(TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

    typedef std::pair<atom_string_t, double> p_t;
    std::map<atom_string_t, double> test_values;

    const p_t ini_data[] = {
        p_t("a"_astr, 1.),
        p_t("aa"_astr, 1.1),
        p_t("aab"_astr, 1.2),
        p_t("ab"_astr, 1.3),
        p_t("abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_astr, 1.31),
        p_t("abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_astr, 1.31),
        p_t("ba"_astr, 1.4),
    };
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(s.first, s.second);
        });
    compare_containers(tresult, *trie, test_values);
    trie->range()->for_each([](const auto& i){
        std::cout << (const char*)i.key().c_str() << "=" << i.value() << "\n";
    });
    auto start_i = trie->find("a"_astr);
    tresult.assert_true(trie->in_range(start_i), OP_CODE_DETAILS() << "wrong find");
    auto tsti_1 = start_i;
    trie->next_lower_bound_of(tsti_1, "aa"_astr);
    tresult.assert_true(trie->in_range(tsti_1), OP_CODE_DETAILS() << "wrong exact next_lower_bound");
    tresult.debug() << "lower_next =" << (const char*)tsti_1.key().c_str()<<"\n";
    tresult.assert_that<equals>(tsti_1.key(), "aa"_astr, OP_CODE_DETAILS() << "wrong exact next_lower_bound");
    auto test = start_i;
    //test through all with 1 step ahead (emulate pure `next` behaviour)
    for (size_t i = 1; i < (sizeof(ini_data) / sizeof(ini_data[0])); ++i)
    {
        trie->next_lower_bound_of(test, ini_data[i].first);
        tresult.debug() << "lower_next(of:" << (const char*)ini_data[i].first.c_str() << ")="<<(const char*)test.key().c_str() << "=>" << test.value() << "\n";;
        
        tresult.assert_that<equals>(test.key(), ini_data[i].first, OP_CODE_DETAILS() << "wrong exact next_lower_bound");
    }
    //test through all with all steps ahead
    //test through all with 1 step ahead (emulate pure `next` behaviour)
    for (size_t i = 1; i < (sizeof(ini_data) / sizeof(ini_data[0])); ++i)
    {
        auto test = start_i;
        trie->next_lower_bound_of(test, ini_data[i].first);
        tresult.assert_that<equals>(test.key(), ini_data[i].first, OP_CODE_DETAILS() << "wrong exact next_lower_bound");
    }
    test = start_i; //now it "a"
    trie->next_lower_bound_of(test, "b"_astr);
    tresult.assert_that<equals>(test.key(), "ba"_astr, OP_CODE_DETAILS() << "wrong unexisting next_lower_bound");
    trie->next_lower_bound_of(test, "c"_astr);
    tresult.assert_that<equals>(test, trie->end(), OP_CODE_DETAILS() << "wrong end() check");
}   

template <class Range1, class Range2, class Vector>
std::int64_t applyJoinRest(Range1 range1, Range2 range2, Vector &result)
{
    auto join_result = range1->join(range2);
    
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    //materialize
    for(auto i = join_result->begin(); join_result->in_range(i); join_result->next(i))
        result.emplace_back(std::make_pair(i.key(), i.value()));
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    
    auto retval = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
    return retval;
}

void test_JoinRangeOverride(TestResult& tresult)
{
    auto tmngr1 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    auto tmngr2 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>((std::string("2_")+test_file_name).c_str(),
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    auto tmngr3 = OP::trie::SegmentManager::create_new<TransactedSegmentManager>((std::string("3_") + test_file_name).c_str(),
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, size_t> trie_t;
    tresult.info()<<"create trie #1...\n";
    std::shared_ptr<trie_t> trie1 = trie_t::create_new(tmngr1);

    tresult.info()<<"create trie #2...\n";
    std::shared_ptr<trie_t> trie2 = trie_t::create_new(tmngr2);

    tresult.info()<<"create trie #3...\n";
    std::shared_ptr<trie_t> trie3 = trie_t::create_new(tmngr3);

    typedef std::pair<atom_string_t, size_t> p_t;
    std::map<atom_string_t, size_t> test_values;
    //create low-density trie
    constexpr size_t avg_str_len = 11;
    constexpr size_t trie_limit = 5000;
    atom_string_t rnd_val;
    rnd_val.reserve(avg_str_len);
    static const atom_string_t rand_str_base = "0123456789"\
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"\
                    "abcdefghijklmnopqrstuvwxyz"_astr;
    for (size_t i = 0; i < trie_limit; ++i)
    {
        tresult.info()<<".";
        if((i % 32) == 0 )
            tresult.info()<<i<<"\n";
        OP::utest::tools::randomize_str(rnd_val, avg_str_len, avg_str_len, [&](){
                
                return rand_str_base[OP::utest::tools::wrap_rnd() % rand_str_base.length()];
            });
        trie1->insert(rnd_val, i);
        tresult.info()<<":";
        if(((int)rnd_val[0]) % 2 == 0 )
            trie2->insert(rnd_val, i);
        if( ((int)rnd_val[0]) % 3 == 0)
            trie3->insert(rnd_val, i);
        test_values.emplace(rnd_val, i);
    }
    tresult.info()<<"\nDone populating #1, #2, #3 \n Compare with test-values...";
    compare_containers(tresult, *trie1, test_values);

    //thin-out map for later compare
    for (auto iter = test_values.begin(); iter != test_values.end(); ) {
        if ((iter->first[0] %2) != 0 || (iter->first[0] % 3) != 0)  {
            iter = test_values.erase(iter);
        }
        else {
            ++iter;
        }
    }
    using target_t = std::vector<std::pair<atom_string_t, size_t>>;
    target_t result1; 
    target_t result2;
    result1.reserve(100 + trie_limit / 3);
    result2.reserve(100 + trie_limit / 3);

    tresult.info()<<"Create filtered sub-range-1 for trie #1\n";
    auto t1r1 = trie1->range()->filter([&](const auto& i) {
        return ((int)i.key()[0]) % 2 == 0;
        });
    tresult.info()<<"Create filtered sub-range-2 for trie #1\n";
    auto t1r2 = trie1->range()->filter([&](const auto& i) {
        return ((int)i.key()[0]) % 3 == 0;
        });
    // filtered range uses default impl of join
    tresult.info() << "Join processed:" << applyJoinRest(
        t1r1,
        t1r2,
        result1) << "[ms]\n";
    compare_containers(tresult, *t1r1->join(t1r2), test_values);
    tresult.info() << "Splitted-default join processed:" << applyJoinRest(
        trie2->range()->filter([](const auto&){return true;}),
        trie3->range()->filter([](const auto&) {return true;}),
        result2) << "[ms]\n";
    result2.clear();
    // trie-range uses overriding impl of join
    tresult.info() << "Non-default join processed:" << applyJoinRest(
        trie2->range(),
        trie3->range(),
        result2) << "[ms]\n";

    tresult.assert_true(result1 == result2);
    auto t2r1 = trie2->range()->join(trie3->range());
    compare_containers(tresult, *t2r1, test_values);

}
void test_AllPrefixesRange(OP::utest::TestResult& tresult) {
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using testmap_t = std::map<atom_string_t, double, lexicographic_less<atom_string_t> >;
    testmap_t test_values{
        {"a"_astr, 1.},
        {"aa"_astr, 1.1},
        {"abc"_astr, 1.2},
        {"abca"_astr, 1.2},
        {"abd"_astr, 1.2},
    };

    for(const auto& pair : test_values){
        trie->insert(pair.first, pair.second);
    };
    auto arg_range = OP::ranges::make_range_of_map(testmap_t{{"a"_astr, 2.0}});
    compare_containers(tresult, *OP::trie::prefixes_continuation_range (trie->range(), arg_range), test_values);

    arg_range = OP::ranges::make_range_of_map(testmap_t{ {"ab"_astr, 2.0} });
    compare_containers(tresult, *OP::trie::prefixes_continuation_range(trie->range(), arg_range),
        testmap_t{ 
            {"abc"_astr, 1.2},
            {"abca"_astr, 1.2},
            {"abd"_astr, 1.2},
        });

    arg_range = OP::ranges::make_range_of_map(testmap_t{ {"0"_astr, 2.0}, {"aa"_astr, 2.0}, {"abcd"_astr, 2.0}, });
    compare_containers(tresult, *OP::trie::prefixes_continuation_range(trie->range(), arg_range),
        testmap_t{
            {"aa"_astr, 1.1},
        });

    testmap_t suffixes { {"aaa"_astr, 1.1},
        {"b"_astr, 1.2},
        {"ba"_astr, 1.2},
        {"bax"_astr, 1.2},
        {"x"_astr, 1.2},
        {"xy"_astr, 1.2},
    };
    test_values.clear();
    const auto control_pref = "klm"_astr;
    for(auto& s : {"k"_astr, "l"_astr, "m"_astr, "n"_astr })
    {   //create bunch of similar prefixes
        auto root_pair = trie->insert(s, 1.0);
        bool is_in_control = control_pref.find(s[0]) != control_pref.npos;
        if(is_in_control)
            test_values.emplace(s, 1.0);
        for(const auto& ent : suffixes){
            auto sufix_pair = trie->prefixed_insert(root_pair.first, ent.first, ent.second);
            auto long_suffix_pair = trie->prefixed_insert(sufix_pair.first, "aa"_astr, 1.3);
            if (is_in_control)
            {
                test_values.emplace(sufix_pair.first.key(), sufix_pair.first.value());
                test_values.emplace(long_suffix_pair.first.key(), long_suffix_pair.first.value());
            }
        }
    }
    arg_range = OP::ranges::make_range_of_map(testmap_t{ {"0"_astr, 2.0}, {"k"_astr, 2.0}, {"l"_astr, 2.0}, {"m"_astr, 2.0}, {"x"_astr, 2.0} });
    //atom_string_t previous;
    //OP::trie::prefixes_continuation_range(trie->range(), arg_range)->for_each([&tresult](const auto& i){
    //    tresult.debug() <<"{" << (const char*)i.key().c_str() << " = " << i.value() << "}\n";
    //    });
    compare_containers(tresult, *OP::trie::prefixes_continuation_range(trie->range(), arg_range), test_values);

    //erase from test_values all started with 'l' or not containing 'ba'
    for(auto er = test_values.begin(); er !=test_values.end(); )
    {
        if( er->first.find("l"_astr) == 0 || er->first.find("ba"_astr) == er->first.npos)
            er = test_values.erase(er);
        else
            ++er;
    }
    arg_range = OP::ranges::make_range_of_map(testmap_t{ {"0ba"_astr, 2.0}, {"kba"_astr, 2.0}, {"mba"_astr, 2.0}, {"xba"_astr, 2.0} });
    compare_containers(tresult, *OP::trie::prefixes_continuation_range(trie->range(), arg_range), test_values);
    //just curious ....
    auto k_range = 
        make_mixed_range(std::static_pointer_cast<trie_t const>(trie),
            typename Ingredient<trie_t>::PrefixedBegin("k"_astr),
            typename Ingredient<trie_t>::PrefixedLowerBound("k"_astr),
            typename Ingredient<trie_t>::PrefixedInRange(StartWithPredicate("k"_astr)))
        ;
    for (auto er = test_values.begin(); er != test_values.end(); )
    {
        if (er->first.find("k"_astr) != 0 )
            er = test_values.erase(er);
        else
            ++er;
    }
    arg_range = OP::ranges::make_range_of_map(testmap_t{ {"0ba"_astr, 2.0}, {"kba"_astr, 2.0}, {"mba"_astr, 2.0}, {"xba"_astr, 2.0} });
    compare_containers(tresult, *OP::trie::prefixes_continuation_range(k_range, arg_range), test_values);
}
void test_ISSUE_0001(OP::utest::TestResult& tresult) {
    OP::trie::atom_string_t source_seq[] = {
        { 0x13,0x04,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96 },
        { 0x14,0x44,0xa2,0xfa,0xdb,0x41,0xa1,0xd8,0x45,0xe0,0x12,0xfe,0x98,0x10,0x4d,0x55,0x87,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x00,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa,0x94,0x44,0xa2,0xfa,0xdb,0x41,0xa1,0xd8,0x45,0xe0,0x12,0xfe,0x98,0x10,0x4d,0x55,0x87 },
        { 0x02,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e },
        { 0x14,0x73,0x35,0xd0,0x43,0x41,0xb7,0xd8,0x45,0x00,0x12,0xfe,0x98,0xf2,0x2d,0x17,0x1f,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e },
        { 0x02,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e,0x94,0x73,0x35,0xd0,0x43,0x41,0xb7,0xd8,0x45,0x00,0x12,0xfe,0x98,0xf2,0x2d,0x17,0x1f },
        { 0x02,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f },
        { 0x14,0xc5,0xb6,0x70,0x61,0x41,0xb7,0xd8,0x45,0x60,0x12,0xfe,0x98,0xd0,0x6e,0xba,0x63,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f },
        { 0x02,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f,0x94,0xc5,0xb6,0x70,0x61,0x41,0xb7,0xd8,0x45,0x60,0x12,0xfe,0x98,0xd0,0x6e,0xba,0x63 },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x03,0x10,0x01,0x00,0x00,0x00,0x00 },
        { 0x00,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x11,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x03 },
        { 0x00,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x90,0x01,0x00,0x00,0x00,0x00 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x00 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x0e,0x24,0x00,0x73,0x00,0x79,0x00,0x73,0x00,0x64,0x00,0x65,0x00,0x66,0x00,0x01,0x00,0x00,0x00,0x00 },
        { 0x92,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x03,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4 },
        { 0x12,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4 },
        { 0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x03 },
        { 0x01,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x03 },
        { 0x81,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x03,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4 },
        { 0x02,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f,0x10,0x01,0x00,0x00,0x00,0x01 },
        { 0x00,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x11,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f },
        { 0x00,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x90,0x01,0x00,0x00,0x00,0x01 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x01 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x0e,0x24,0x00,0x73,0x00,0x79,0x00,0x73,0x00,0x64,0x00,0x65,0x00,0x66,0x00,0x01,0x00,0x00,0x00,0x01 },
        { 0x02,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f,0x10,0x01,0x00,0x00,0x00,0x02 },
        { 0x00,0x04,0x50,0xaa,0x2b,0xa8,0x4d,0xc3,0x95,0x14,0xfb,0x6e,0xbd,0xc1,0xfb,0x2d,0xb7,0x06,0x11,0x04,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f },
        { 0x00,0x04,0x50,0xaa,0x2b,0xa8,0x4d,0xc3,0x95,0x14,0xfb,0x6e,0xbd,0xc1,0xfb,0x2d,0xb7,0x06,0x90,0x01,0x00,0x00,0x00,0x02 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x02 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x06,0x69,0x00,0x73,0x00,0x61,0x00,0x01,0x00,0x00,0x00,0x02 },
        { 0x02,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e,0x10,0x01,0x00,0x00,0x00,0x03 },
        { 0x00,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x11,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e },
        { 0x00,0x04,0x61,0x2a,0x7c,0x1d,0x49,0xbf,0x7b,0x9c,0x3b,0xb0,0x8a,0x5e,0x89,0xf1,0xd6,0xa4,0x90,0x01,0x00,0x00,0x00,0x03 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x03 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x0e,0x24,0x00,0x73,0x00,0x79,0x00,0x73,0x00,0x64,0x00,0x65,0x00,0x66,0x00,0x01,0x00,0x00,0x00,0x03 },
        { 0x02,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e,0x10,0x01,0x00,0x00,0x00,0x04 },
        { 0x00,0x04,0x50,0xaa,0x2b,0xa8,0x4d,0xc3,0x95,0x14,0xfb,0x6e,0xbd,0xc1,0xfb,0x2d,0xb7,0x06,0x11,0x04,0xab,0x0b,0xad,0x8a,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x34,0x81,0x92,0x1e },
        { 0x00,0x04,0x50,0xaa,0x2b,0xa8,0x4d,0xc3,0x95,0x14,0xfb,0x6e,0xbd,0xc1,0xfb,0x2d,0xb7,0x06,0x90,0x01,0x00,0x00,0x00,0x04 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x04 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x0c,0x70,0x00,0x61,0x00,0x72,0x00,0x74,0x00,0x6f,0x00,0x66,0x00,0x01,0x00,0x00,0x00,0x04 },
        { 0x02,0x04,0xf4,0x73,0x6c,0xaf,0x41,0x99,0xd8,0x45,0x80,0x12,0xfe,0x98,0x30,0x64,0x99,0x84 },
        { 0x92,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde },
        { 0x01,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde },
        { 0x81,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x92,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x01,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x81,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x14,0x54,0x3a,0xb7,0xca,0x43,0xc4,0x7d,0x73,0xe7,0xc2,0x35,0x94,0x6d,0xaa,0x9a,0x68,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06 },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06,0x94,0x54,0x3a,0xb7,0xca,0x43,0xc4,0x7d,0x73,0xe7,0xc2,0x35,0x94,0x6d,0xaa,0x9a,0x68 },
        { 0x14,0x54,0x3a,0xb7,0xca,0x43,0xc4,0x7d,0x73,0xe7,0xc2,0x35,0x94,0x6d,0xaa,0x9a,0x68,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06 },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06,0x94,0x54,0x3a,0xb7,0xca,0x43,0xc4,0x7d,0x73,0xe7,0xc2,0x35,0x94,0x6d,0xaa,0x9a,0x68 },
        { 0x92,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde },
        { 0x12,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde },
        { 0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06 },
        { 0x01,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06 },
        { 0x81,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x06,0x04,0x3c,0x54,0xdc,0x08,0x49,0x23,0x95,0x89,0xa5,0x6b,0xb0,0x9e,0x1b,0x7b,0x1c,0xde },
        { 0x92,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x01,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x81,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x92,0x04,0x27,0x0d,0x76,0x0f,0x48,0x2c,0x57,0x0d,0x7a,0xca,0xb7,0x93,0x49,0xf4,0xfa,0x1c,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x12,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x04,0x27,0x0d,0x76,0x0f,0x48,0x2c,0x57,0x0d,0x7a,0xca,0xb7,0x93,0x49,0xf4,0xfa,0x1c },
        { 0x92,0x04,0x27,0x0d,0x76,0x0f,0x48,0x2c,0x57,0x0d,0x7a,0xca,0xb7,0x93,0x49,0xf4,0xfa,0x1c,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x27,0x0d,0x76,0x0f,0x48,0x2c,0x57,0x0d,0x7a,0xca,0xb7,0x93,0x49,0xf4,0xfa,0x1c },
        { 0x01,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a,0x04,0x27,0x0d,0x76,0x0f,0x48,0x2c,0x57,0x0d,0x7a,0xca,0xb7,0x93,0x49,0xf4,0xfa,0x1c },
        { 0x81,0x04,0x27,0x0d,0x76,0x0f,0x48,0x2c,0x57,0x0d,0x7a,0xca,0xb7,0x93,0x49,0xf4,0xfa,0x1c,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x92,0x04,0xce,0x92,0xcf,0x4c,0x4a,0x2b,0xaf,0xc3,0x5a,0x76,0xa3,0xa8,0xbe,0x53,0x8b,0x17,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x12,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x04,0xce,0x92,0xcf,0x4c,0x4a,0x2b,0xaf,0xc3,0x5a,0x76,0xa3,0xa8,0xbe,0x53,0x8b,0x17 },
        { 0x92,0x04,0xce,0x92,0xcf,0x4c,0x4a,0x2b,0xaf,0xc3,0x5a,0x76,0xa3,0xa8,0xbe,0x53,0x8b,0x17,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0xce,0x92,0xcf,0x4c,0x4a,0x2b,0xaf,0xc3,0x5a,0x76,0xa3,0xa8,0xbe,0x53,0x8b,0x17 },
        { 0x01,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a,0x04,0xce,0x92,0xcf,0x4c,0x4a,0x2b,0xaf,0xc3,0x5a,0x76,0xa3,0xa8,0xbe,0x53,0x8b,0x17 },
        { 0x81,0x04,0xce,0x92,0xcf,0x4c,0x4a,0x2b,0xaf,0xc3,0x5a,0x76,0xa3,0xa8,0xbe,0x53,0x8b,0x17,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x92,0x04,0x3a,0xcb,0xec,0x38,0x46,0x5c,0x6d,0x20,0xba,0xf9,0x1c,0xbd,0xdd,0xda,0x99,0x91,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x12,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x04,0x3a,0xcb,0xec,0x38,0x46,0x5c,0x6d,0x20,0xba,0xf9,0x1c,0xbd,0xdd,0xda,0x99,0x91 },
        { 0x01,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x04,0x3a,0xcb,0xec,0x38,0x46,0x5c,0x6d,0x20,0xba,0xf9,0x1c,0xbd,0xdd,0xda,0x99,0x91 },
        { 0x81,0x04,0x3a,0xcb,0xec,0x38,0x46,0x5c,0x6d,0x20,0xba,0xf9,0x1c,0xbd,0xdd,0xda,0x99,0x91,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x92,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x12,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07 },
        { 0x92,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07 },
        { 0x01,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07 },
        { 0x81,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x92,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x12,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c },
        { 0x01,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c },
        { 0x81,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x92,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x01,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x81,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x92,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07 },
        { 0x12,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07 },
        { 0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x92,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x12,0x04,0x19,0xb8,0x2c,0x7a,0x4d,0x45,0x95,0x6d,0x92,0xe5,0x65,0xa0,0x04,0x84,0xf2,0x2a },
        { 0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x92,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x01,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x81,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0xab,0x86,0x48,0xd1,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0x3d,0x8d,0x5e,0x07 },
        { 0x02,0x05,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f,0x00,0x00,0x00,0x00 },
        { 0x92,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x12,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x92,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x01,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e },
        { 0x81,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x92,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x12,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f },
        { 0x92,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f },
        { 0x01,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f },
        { 0x81,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x00,0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40,0x10,0x01,0x00,0x00,0x00,0x05 },
        { 0x00,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x11,0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40 },
        { 0x00,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x90,0x01,0x00,0x00,0x00,0x05 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x05 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x0e,0x40,0x04,0x43,0x04,0x41,0x04,0x41,0x04,0x3a,0x04,0x38,0x04,0x39,0x04,0x01,0x00,0x00,0x00,0x05 },
        { 0x92,0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c },
        { 0x12,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c },
        { 0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40 },
        { 0x92,0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x12,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40 },
        { 0x01,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c,0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40 },
        { 0x81,0x04,0x81,0x43,0x86,0xbe,0x49,0x2a,0x71,0xc0,0x9f,0x28,0x04,0x8c,0x71,0x14,0x28,0x40,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0c },
        { 0x02,0x05,0x53,0x2d,0xb9,0xa3,0x41,0xa1,0xd8,0x45,0x00,0x12,0xfe,0x98,0x57,0x8b,0xaa,0x3f,0x00,0x00,0x00,0x01 },
        { 0x00,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4,0x10,0x01,0x00,0x00,0x00,0x06 },
        { 0x00,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x11,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4 },
        { 0x00,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x90,0x01,0x00,0x00,0x00,0x06 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x06 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x0c,0x33,0x04,0x3b,0x04,0x30,0x04,0x33,0x04,0x3e,0x04,0x3b,0x04,0x01,0x00,0x00,0x00,0x06 },
        { 0x00,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4,0x10,0x01,0x00,0x00,0x00,0x07 },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x11,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4 },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x90,0x01,0x00,0x00,0x00,0x07 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x07 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x08,0x76,0x00,0x65,0x00,0x72,0x00,0x62,0x00,0x01,0x00,0x00,0x00,0x07 },
        { 0x92,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x12,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4 },
        { 0x92,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4 },
        { 0x01,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4 },
        { 0x81,0x04,0x31,0x82,0x43,0x6b,0x46,0xa0,0x3c,0xd1,0x22,0x56,0xe2,0xba,0x69,0xd1,0x07,0xc4,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x00, 0x04, 0x1d, 0xbf, 0xf4, 0x97, 0x40, 0xab, 0xde, 0xef, 0xcc, 0xe2, 0xd8, 0xbf, 0xad, 0x28, 0xbf, 0x8d, 0x10, 0x01, 0x00, 0x00, 0x00, 0x08 },
        { 0x00,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x11,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d },
        { 0x00,0x04,0xa9,0x3d,0xc9,0xd9,0x45,0x33,0x21,0x73,0xc3,0xea,0x3a,0x8d,0x83,0x79,0xbb,0x6e,0x90,0x01,0x00,0x00,0x00,0x08 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x08 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x1e,0x41,0x04,0x43,0x04,0x49,0x04,0x35,0x04,0x41,0x04,0x42,0x04,0x32,0x04,0x38,0x04,0x42,0x04,0x35,0x04,0x3b,0x04,0x4c,0x04,0x3d,0x04,0x3e,0x04,0x35,0x04,0x01,0x00,0x00,0x00,0x08 },
        { 0x00,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d,0x10,0x01,0x00,0x00,0x00,0x09 },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x11,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d },
        { 0x00,0x05,0x10,0x50,0x12,0xc1,0x5b,0xa1,0x0e,0x9e,0x16,0xdc,0xef,0x4c,0x6e,0x34,0x9b,0x96,0x00,0x00,0x00,0x0f,0x90,0x01,0x00,0x00,0x00,0x09 },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x09 },
        { 0x17,0x43,0x08,0x00,0x00,0x00,0x08,0x6e,0x00,0x6f,0x00,0x75,0x00,0x6e,0x00,0x01,0x00,0x00,0x00,0x09 },
        { 0x92,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x12,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d },
        { 0x92,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d },
        { 0x01,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d },
        { 0x81,0x04,0x1d,0xbf,0xf4,0x97,0x40,0xab,0xde,0xef,0xcc,0xe2,0xd8,0xbf,0xad,0x28,0xbf,0x8d,0x04,0x42,0x72,0x0a,0xaa,0x41,0xfb,0xd8,0x45,0x00,0x12,0xfe,0x98,0xc5,0x0a,0x58,0x0e },
        { 0x92,0x04,0xca,0xde,0x18,0x41,0x48,0x61,0xfc,0x3f,0x3c,0x01,0xc9,0xa1,0xda,0x7e,0x04,0x02,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x12,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x04,0xca,0xde,0x18,0x41,0x48,0x61,0xfc,0x3f,0x3c,0x01,0xc9,0xa1,0xda,0x7e,0x04,0x02 },
        { 0x01,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x04,0xca,0xde,0x18,0x41,0x48,0x61,0xfc,0x3f,0x3c,0x01,0xc9,0xa1,0xda,0x7e,0x04,0x02 },
        { 0x81,0x04,0xca,0xde,0x18,0x41,0x48,0x61,0xfc,0x3f,0x3c,0x01,0xc9,0xa1,0xda,0x7e,0x04,0x02,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x92,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x12,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x92,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x12,0x04,0xf5,0x02,0x87,0x52,0x41,0xa1,0xd8,0x45,0xc0,0x12,0xfe,0x98,0x9c,0xbf,0x89,0xaa },
        { 0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x01,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x81,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x92,0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x12,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1 },
        { 0x01,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1 },
        { 0x81,0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x92,0x04,0xb4,0xb4,0xb9,0x11,0x43,0x6b,0x76,0x64,0x82,0x56,0x89,0x8a,0xe7,0x9f,0x4c,0x48,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x12,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x04,0xb4,0xb4,0xb9,0x11,0x43,0x6b,0x76,0x64,0x82,0x56,0x89,0x8a,0xe7,0x9f,0x4c,0x48 },
        { 0x01,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x04,0xb4,0xb4,0xb9,0x11,0x43,0x6b,0x76,0x64,0x82,0x56,0x89,0x8a,0xe7,0x9f,0x4c,0x48 },
        { 0x81,0x04,0xb4,0xb4,0xb9,0x11,0x43,0x6b,0x76,0x64,0x82,0x56,0x89,0x8a,0xe7,0x9f,0x4c,0x48,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4 },
        { 0x14,0xc5,0xb6,0x70,0x61,0x41,0xb7,0xd8,0x45,0x60,0x12,0xfe,0x98,0xd0,0x6e,0xba,0x63,0x04,0xbd,0x57,0x9e,0xca,0x4c,0x14,0xf6,0x35,0xcc,0x16,0xe5,0x9c,0x5b,0x02,0xa0,0xbe },
        { 0x00,0x04,0xbd,0x57,0x9e,0xca,0x4c,0x14,0xf6,0x35,0xcc,0x16,0xe5,0x9c,0x5b,0x02,0xa0,0xbe,0x94,0xc5,0xb6,0x70,0x61,0x41,0xb7,0xd8,0x45,0x60,0x12,0xfe,0x98,0xd0,0x6e,0xba,0x63 },
        { 0x92,0x04,0xbd,0x57,0x9e,0xca,0x4c,0x14,0xf6,0x35,0xcc,0x16,0xe5,0x9c,0x5b,0x02,0xa0,0xbe,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x12,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x04,0xbd,0x57,0x9e,0xca,0x4c,0x14,0xf6,0x35,0xcc,0x16,0xe5,0x9c,0x5b,0x02,0xa0,0xbe },
        { 0x01,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf,0x04,0xbd,0x57,0x9e,0xca,0x4c,0x14,0xf6,0x35,0xcc,0x16,0xe5,0x9c,0x5b,0x02,0xa0,0xbe },
        { 0x81,0x04,0xbd,0x57,0x9e,0xca,0x4c,0x14,0xf6,0x35,0xcc,0x16,0xe5,0x9c,0x5b,0x02,0xa0,0xbe,0x04,0x3a,0xbf,0x32,0x1d,0x43,0xad,0xd8,0x48,0xc1,0x63,0x9d,0x9d,0x8b,0x56,0x26,0xcf },
        { 0x00,0x04,0x00,0xe8,0x0e,0xc7,0x4e,0x3a,0x44,0x0f,0x2e,0xb0,0x12,0xb3,0x71,0x95,0xbc,0xf1,0x10,0x01,0x00,0x00,0x00,0x0a },
        { 0x00,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x11,0x04,0x00,0xe8,0x0e,0xc7,0x4e,0x3a,0x44,0x0f,0x2e,0xb0,0x12,0xb3,0x71,0x95,0xbc,0xf1 },
        { 0x00,0x04,0x25,0xe4,0xda,0xfe,0x4f,0x04,0xa7,0xf7,0x24,0xdf,0xc8,0x86,0x45,0x95,0x9d,0xd4,0x90,0x01,0x00,0x00,0x00,0x0a },
        { 0x17,0xff,0x01,0x00,0x00,0x00,0x0a },
        { 0x17,0x80,0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1,0x01,0x00,0x00,0x00,0x0a },
        { 0x17,0x00,0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1,0x01,0x00,0x00,0x00,0x0a },

    };
    typedef std::pair<atom_string_t, double> p_t;
    std::map<atom_string_t, double> test_values;

    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    const atom_string_t to_erase
    { 0x17, 0x80, 0x04, 0x5d, 0x48, 0x29, 0x4b, 0x40, 0xad, 0xee, 0x40, 0xe3, 0x4c, 0x0b, 0x9f, 0x84, 0x37, 0x44, 0xf1, 0x01, 0x00, 0x00, 0x00, 0x0a };
    const atom_string_t to_add
    { 0x17,0x00,0x04,0x5d,0x48,0x29,0x4b,0x40,0xad,0xee,0x40,0xe3,0x4c,0x0b,0x9f,0x84,0x37,0x44,0xf1,0x01,0x00,0x00,0x00,0x0a };

    /*const atom_string_t
        comm_suffix{ 0x17,0x43,0x08,0x00,0x00,0x00,0x1e,0x41,0x04,0x43,0x04,0x49,0x04,0x35,0x04,0x41,0x04,0x42,0x04,0x32,0x04,0x38,0x04,0x42,0x04,0x35,0x04,0x3b,0x04,0x4c,0x04,0x3d,0x04,0x3e,0x04,0x35,0x04,0x01,0x00,0x00,0x00,0x08 };
    for (int i = 0; i < 10;++i) {
        atom_string_t s{ (unsigned char)i };
        //trie->insert(s, 10);
        //test_values.emplace(s, 10);
        s += comm_suffix;
        trie->insert(s, 20);
        test_values.emplace(s, 20);
    } */


    std::for_each(std::begin(source_seq), std::end(source_seq), [&](const atom_string_t& s) {
        trie->insert(s, 0);
        test_values.emplace(s, 0);
        });
    compare_containers(tresult, *trie, test_values);

    trie->erase(trie->find(to_erase));
    test_values.erase(to_erase);

    compare_containers(tresult, *trie, test_values);

    tresult.assert_that<equals>(trie->end(), trie->find(to_erase),
        OP_CODE_DETAILS(<< "erase failed"));
    trie->insert(to_add, 20);
    test_values.emplace(to_add, 20);

    compare_containers(tresult, *trie, test_values);
}

//*************************************************




static auto module_suite = OP::utest::default_test_suite("Trie")
->declare(test_TrieCreation, "creation")
->declare(test_TrieInsert, "insertion")
->declare(test_TrieInsertGrow, "insertion-grow")
->declare(test_TrieUpdate, "update values")
//->declare(test_TrieGrowAfterUpdate, "grow-after-update")
->declare(test_TrieLowerBound, "lower_bound")
->declare(test_PrefixedFind, "prefixed find")
->declare(test_TrieSubtree, "subtree of prefix")
->declare(test_TrieSubtreeLambdaOperations, "lambda on subtree")
->declare(test_TrieNoTran, "trie no tran")
->declare(test_Flatten, "flatten")
->declare(test_TrieSectionRange, "section range")
->declare(test_Erase, "erase")
->declare(test_Siblings, "siblings")
->declare(test_ChildSelector, "child")
->declare(test_IteratorSync, "sync iterator")
->declare(test_TrieUpsert, "upsert")
->declare(test_TriePrefixedInsert, "prefixed insert")
->declare(test_TriePrefixedUpsert, "prefixed upsert")
->declare(test_TriePrefixedEraseAll, "prefixed erase_all")
->declare(test_TriePrefixedKeyEraseAll, "prefixed erase_all by key")
->declare(test_Range, "iterate all by range")
->declare(test_NativeRangeSupport, "test native ranges of Trie")
->declare(test_TrieCheckExists, "check_exists")
->declare(test_NextLowerBound, "next_lower_bound")
->declare(test_JoinRangeOverride, "override_join_range")
->declare(test_AllPrefixesRange, "all_prefixes_range")
->declare(test_ISSUE_0001, "ISSUE_0001")

;
