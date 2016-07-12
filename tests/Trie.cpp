#include "unit_test.h"
#include "unit_test_is.h"
#include <op/trie/Trie.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/CacheManager.h>
#include <op/vtm/TransactedSegmentManager.h>
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
    std::cout << '\n';
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
    for (; trie.in_range(ti); ++ti, ++mi, ++n)
    {
        print_hex(tresult.info() << "1)", ti.prefix());
        print_hex(tresult.info() << "2)", mi->first);
        tresult.assert_true(ti.prefix().length() == mi->first.length(), 
            OP_CODE_DETAILS(<<"step#"<< n << " has:" << ti.prefix().length() << ", while expected:" << mi->first.length()));
        tresult.assert_true(tools::container_equals(ti.prefix(), mi->first),
            OP_CODE_DETAILS(<<"step#"<< n << ", for key="<<(const char*)mi->first.c_str() << ", while obtained:" << (const char*)ti.prefix().c_str()));
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
    tresult.assert_true(ir1.first, OP_CODE_DETAILS());
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir1.second.prefix()), std::end(ir1.second.prefix()),
        std::begin(stem1), std::end(stem1)
        ));
    tresult.assert_true(2 == trie->nodes_count(), "2 nodes must be create for long stems");
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 2);

    standard[stem1] = v_order++;
    compare_containers(tresult, *trie, standard);
    
    auto ir2 = trie->insert(b1 = std::begin(stem1), std::end(stem1), v_order + 101.0);
    tresult.assert_false(
        ir2.first, 
        OP_CODE_DETAILS("Duplicate insert must not be allowed"));
    tresult.assert_true(b1 == std::end(stem1));
    tresult.assert_true(trie->size() == 2);
    
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
    tresult.assert_true(trie->size() == 4);
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
    tresult.assert_true(*ir5.second == (v_order - 1), "Wrong iterator value");
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
    tresult.assert_true(261 == trie->nodes_count(), "261 nodes must exists in the system");
    compare_containers(tresult, *trie, standard);
    stem4 += "zzzzz";
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(b1 == std::end(const_stem4));
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.second.prefix()), std::end(ir5.second.prefix()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_true(*ir5.second == (v_order - 1), "Wrong iterator value");
    compare_containers(tresult, *trie, standard);
    // test stem continuation
    // Sequence: "k", "kaa..a", "ka"
    stem4 = "k";
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(b1 == std::end(const_stem4));
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.second.prefix()), std::end(ir5.second.prefix()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_true(*ir5.second == (v_order - 1), "Wrong iterator value");
    stem4 += std::string(256, 'a');
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(b1 == std::end(const_stem4));
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.second.prefix()), std::end(ir5.second.prefix()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_true(*ir5.second == (v_order - 1), "Wrong iterator value");
    compare_containers(tresult, *trie, standard);
    stem4 = "ka";
    ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
    standard[stem4] = v_order++;
    tresult.assert_true(b1 == std::end(const_stem4));
    tresult.assert_true(OP::utest::tools::range_equals(std::begin(ir5.second.prefix()), std::end(ir5.second.prefix()),
        std::begin(stem4), std::end(stem4)
    ));
    tresult.assert_true(*ir5.second == (v_order - 1), "Wrong iterator value");
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

        tresult.assert_true(tools::container_equals(lbit.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_true(x == *lbit);

        auto query = test_seq[i] + "a";
        auto lbit2 = trie->lower_bound(std::begin(query), std::end(query));

        //print_hex(std::cout << "1)", test_seq[i + 1]);
        //print_hex(std::cout << "2)", lbit2.prefix());
        tresult.assert_true(tools::container_equals(lbit2.prefix(), test_seq[i+1], &tools::sign_tolerant_cmp<atom_t>));

        auto lbit3 = trie->lower_bound(std::begin(test), std::end(test)-1);//take shorter key
        tresult.assert_true(tools::container_equals(lbit3.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));
        tresult.assert_true(x == *lbit);
    }
    //handle case of stem_end

    std::string test_long(258, 'k');
    auto long_ins_res = trie->insert(test_long, 7.5);
    test_long += "aa";
    auto lbegin = std::begin(test_long);
    auto llong_res = trie->lower_bound(lbegin, std::end(test_long));
    tresult.assert_true(tools::container_equals(llong_res.prefix(), test_seq[5], &tools::sign_tolerant_cmp<atom_t>));
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
        trie_t::iterator begin_test = container_ptr.begin();

        if ((i & 1) != 0) //odd entries nust have a terminal
        {
            tresult.assert_true(
                tools::container_equals(begin_test.prefix(), test, &tools::sign_tolerant_cmp<atom_t>));

            tresult.assert_true(*begin_test == (double)i);
            container_ptr.next(begin_test);
        }
        auto a = std::begin(sorted_checks);
        auto cnt = 0;
        for (; container_ptr.in_range(begin_test); container_ptr.next(begin_test), ++a, ++cnt)
        {
            auto strain_str = (test + *a);
            //print_hex(std::cout << "1)", strain_str);
            //print_hex(std::cout << "2)", begin_test.prefix());
            tresult.assert_true(tools::container_equals(begin_test.prefix(), strain_str), "! strain == prefix");
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
        auto && k = i.prefix();
        print_hex(os, k);
        os << '=' << *i << '\n';
    }
}
template <class R1, class R2, class Sample>
void test_join(
    OP::utest::TestResult &tresult, const R1& r1, const R2& r2, const Sample& expected)
{
    auto result1 = r1.join(r2);
    print_co(std::cout << "===========>", r1);
    compare_containers(tresult, result1, expected);
    print_co(std::cout << "===========>", r2);
    std::cout <<"<<<<<<<<<<<\n";
    auto result2 = r2.join(r1);
    compare_containers(tresult, result2, expected);
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
        tresult.assert_true(ins_res.first);
        tresult.assert_true(tools::container_equals(ins_res.second.prefix(), i, &tools::sign_tolerant_cmp<atom_t>));

        //std::cout << std::setfill('0') << std::setbase(16) << std::setw(2) << (unsigned)i << "\n";
    }

    std::map<atom_string_t, double> test_values;
    atom_string_t query1 ((const atom_t*)"a");
    atom_string_t query2 ((const atom_t*)"ad");
    auto container1 = trie->subrange(std::begin(query1), std::end(query1));
    //for (auto i = container1.begin(); container1.in_range(i); container1.next(i))
    //{
    //    print_hex(tresult.info(), i.prefix());
    //}
    auto container2 = trie->subrange(std::begin(query2), std::end(query2));
    //tresult.info() << "======\n";
    //for (auto i = container2.begin(); container2.in_range(i); container2.next(i))
    //{
    //    print_hex(tresult.info(), i.prefix());
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
        trie->subrange(std::begin(query4), std::end(query4)).map([](auto const& it) {
            return it.prefix().substr(1);
        }),
        trie->subrange(std::begin(query5), std::end(query5)).map([](auto const& it) {
            return it.prefix().substr(1);
        }),
        test_values);
}

static auto module_suite = OP::utest::default_test_suite("Trie")
    ->declare(test_TrieCreation, "creation")
    ->declare(test_TrieInsert, "insertion")
    ->declare(test_TrieInsertGrow, "insertion-grow")
    //->declare(test_TrieGrowAfterUpdate, "grow-after-update")
    ->declare(test_TrieLowerBound, "lower_bound")
    ->declare(test_TrieSubtree, "subtree of prefix")
    ->declare(test_TrieSubtreeLambdaOperations, "lambda on subtree")
    ->declare(test_TrieNoTran, "trie no tran")
    ;
