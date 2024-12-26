#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/common/astr.h>

#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/EventSourcingSegmentManager.h>
#include <op/trie/JoinGenerator.h>

#include <algorithm>
#include "../test_comparators.h"
#include "TrieTestUtils.h"

namespace 
{
    using namespace OP::trie;
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace OP::common;

    const char* test_file_name = "trie.test";
    using test_trie_t = Trie<EventSourcingSegmentManager, OP::trie::PlainValueManager<double>>;

    void test_TrieCreation(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x1100000));
        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        tresult.assert_true(0 == trie->size());
        tresult.assert_true(1 == trie->nodes_count());
        auto nav1 = trie->begin();
        tresult.assert_true(nav1 == trie->end());
        trie.reset();

        //test reopen
        tmngr1 = OP::trie::SegmentManager::open<EventSourcingSegmentManager>(test_file_name);
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


    void test_TrieInsert(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = test_trie_t;
        std::map<std::string, double> standard;
        double v_order = 0.1;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        std::string zero_ins{ '_' };
        auto ir0 = trie->insert(zero_ins, v_order);
        tresult.assert_true(ir0.second);
        tresult.assert_that<eq_sets>(ir0.first.key(), zero_ins);
        tresult.assert_that<equals>(*ir0.first, v_order);
        standard[zero_ins] = v_order;
        tresult.assert_that<equals>(trie->size(), 1, "Wrong counter val");
        
        const std::string stem1(260, 'a'); //total 260
        /*stem1_deviation1 - 1'a' for presence, 256'a' for exhausting stem container*/
        std::string stem1_deviation1{ std::string(257, 'a') + 'b' };
        //insert that reinforce long stem
        auto b1 = std::begin(stem1);
        auto ir1 = trie->insert(b1, std::end(stem1), v_order);
        tresult.assert_true(ir1.second, OP_CODE_DETAILS());
        tresult.assert_that<eq_sets>(ir1.first.key(), stem1);

        tresult.assert_true(trie->size() == 2);

        standard[stem1] = v_order++;
        compare_containers(tresult, *trie, standard);

        auto ir2 = trie->insert(stem1, v_order + 101.0);
        tresult.assert_false(
            ir2.second,
            OP_CODE_DETAILS( << "insert must disallow dupplicates"));
        tresult.assert_true(trie->size() == 2);

        auto ir3 = trie->insert(stem1_deviation1, v_order);
        tresult.assert_true(ir3.second, OP_CODE_DETAILS());
        //print_hex(std::cout << "1)", stem1_deviation1);
        //print_hex(std::cout << "2)", ir3.second.key());
        tresult.assert_that<eq_sets>(
            ir3.first.key(), stem1_deviation1,
            OP_CODE_DETAILS());
        tresult.assert_that<equals>(
            *ir3.first, v_order, OP_CODE_DETAILS());
        tresult.assert_that<eq_sets>(
            ir3.first.key(), stem1_deviation1 );

        standard[stem1_deviation1] = v_order++;
        compare_containers(tresult, *trie, standard);
        // test behaviour on range
        const std::string stem2(256, 'b');
        auto ir4 = trie->insert(stem2, v_order);
        tresult.assert_true(ir4.second, OP_CODE_DETAILS());
        tresult.assert_that<eq_sets>(ir4.first.key(), stem2);
        tresult.assert_that<equals>(*ir4.first, v_order);
        standard[stem2] = v_order++;

        tresult.assert_true(trie->size() == 4);

        compare_containers(tresult, *trie, standard);
        //
        // test diversification
        //
        tresult.info() << "test diversification\n";
        std::string stem3 = { std::string(1, 'c') + std::string(256, 'a') };
        const std::string& const_stem3 = stem3;
        auto ir5 = trie->insert(const_stem3, v_order);
        tresult.assert_true(ir5.second, OP_CODE_DETAILS());
        tresult.assert_that<eq_sets>(ir5.first.key(), stem3);
        tresult.assert_that<equals>(*ir5.first, v_order, "Wrong iterator value");
        standard[stem3] = v_order++;

        compare_containers(tresult, *trie, standard);
        //now make iteration back over stem3 and create diversification in 
        for (auto i = 0; stem3.length() > 1; ++i)
        {
            //std::cout << "\b\b\b" << std::setbase(16) << std::setw(3) << std::setfill('0') << i;
            stem3.resize(stem3.length() - 1);
            stem3 += std::string(1, 'b');
            ir5 = trie->insert(stem3, (double)stem3.length());
            tresult.assert_true(ir5.second, OP_CODE_DETAILS(<< "multidiverse at:#"<<i));
            tresult.assert_that<eq_sets>(ir5.first.key(), const_stem3,
                OP_CODE_DETAILS(<< "multidivers at:#" << i)
                );
            tresult.assert_that<equals>(*ir5.first, (double)stem3.length(),
                OP_CODE_DETAILS(<< "iter-value at:#" << i)
                );
            standard[stem3] = (double)stem3.length();
            stem3.resize(stem3.length() - 1);
        }//
        tresult.debug() << "So far allocated: " << trie->nodes_count() << " trie-nodes\n";
        compare_containers(tresult, *trie, standard);
        //
        tresult.info() << "test diversification#2\n";
        std::string stem4 = { std::string(1, 'd') + std::string(256, 'a') };
        const std::string& const_stem4 = stem4;
        ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
        tresult.assert_true(ir5.second, OP_CODE_DETAILS());
        standard[stem4] = v_order++;
        tresult.debug() << "So far allocated: " << trie->nodes_count() << " expected less than 260\n";
        stem4.resize(stem4.length() - 1);
        stem4.append(std::string(258, 'b'));
        ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
        tresult.assert_true(ir5.second, OP_CODE_DETAILS());
        standard[stem4] = v_order++;

        compare_containers(tresult, *trie, standard);
        stem4 += "zzzzz";
        ir5 = trie->insert(const_stem4, v_order);
        standard[stem4] = v_order++;
        tresult.assert_that<eq_sets>(ir5.first.key(), stem4);
        tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
        compare_containers(tresult, *trie, standard);
        // test stem continuation
        // Sequence: "k", "kaa..a", "ka"
        stem4 = "k";
        ir5 = trie->insert(b1 = std::begin(const_stem4), std::end(const_stem4), v_order);
        standard[stem4] = v_order++;
        tresult.assert_that<eq_sets>(ir5.first.key(), stem4);
        tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
        stem4 += std::string(256, 'a');
        ir5 = trie->insert(const_stem4, v_order);
        standard[stem4] = v_order++;
        tresult.assert_that<eq_sets>(ir5.first.key(), stem4);
        tresult.assert_that<equals>(*ir5.first, (v_order - 1.0), "Wrong iterator value");
        compare_containers(tresult, *trie, standard);
        stem4 = "ka";
        ir5 = trie->insert(const_stem4, v_order);
        tresult.assert_that<eq_sets>(ir5.first.key(), stem4);
        tresult.assert_that<equals>(*ir5.first, v_order, "Wrong iterator value");
        
        standard[stem4] = v_order++;
        compare_containers(tresult, *trie, standard);
    }

    void test_TrieInsertGrow(OP::utest::TestSuite& suite, OP::utest::TestRuntime& tresult)
    {

        std::random_device rd;
        std::mt19937 random_gen(rd());

        auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        std::map<std::string, double> test_values;
        // Populate trie with unique strings in range from [0..255]
        // this must cause grow of root node
        const std::string stems[] = { "abc", "", "x", std::string(256, 'z') };
        std::array<std::uint16_t, 255> rand_idx{};
        std::iota(std::begin(rand_idx), std::end(rand_idx), 0);
        std::shuffle(std::begin(rand_idx), std::end(rand_idx), random_gen);
        for (auto i : rand_idx)
        {
            auto lookup_idx = tools::RandomGenerator::instance().next_in_range<size_t>(0, std::extent_v< decltype(stems)>);
            std::string test = std::string(1, (std::string::value_type)i) +
                stems[lookup_idx];
            auto ins_res = trie->insert(test, (double)test.length());
            tresult.assert_true(ins_res.second);
            tresult.assert_true(tools::container_equals(ins_res.first.key(), test, &tools::sign_tolerant_cmp<atom_t>));
            test_values[test] = (double)test.length();
        }
        tresult.assert_that<equals>(rand_idx.size(), trie->size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);
    }
    void test_TrieUpdate(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        auto tmp_end = trie->end();
        tresult.assert_that<equals>(0, trie->update(tmp_end, 0.0), "update of end is not allowed");

        typedef std::pair<atom_string_t, double> p_t;

        const p_t ini_data[] = {
            p_t("aa1"_atom, 1.),
            p_t("aa2"_atom, 1.),
            p_t("abc"_atom, 1.),
            p_t("abc.12"_atom, 1.),
            p_t("abc.122x"_atom, 1),
            p_t("abc.123456789"_atom, 1),
            p_t("abd.12"_atom, 1.),
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
        for (const p_t& s : ini_data)
        {
            auto pos = trie->find(s.first);
            tresult.assert_false(pos.is_end());

            trie->update(pos, 2.0);
            test_values[s.first] = 2.0;
        }
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

    void test_TrieUpdateStem(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
        trie->insert("abcdefghi"_astr, 1.0);
        trie->insert("a"_astr, 2.0);
        std::map<atom_string_t, double> test_values{
            {"abcdefghi"_astr, 1.0},
            {"a"_astr, 2.0},
        };
        compare_containers(tresult, *trie, test_values);
    }

    void test_TrieGrowAfterUpdate(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        std::map<std::string, double> test_values;
        // Populate trie with unique strings in range from [0..255]
        // this must cause grow of root node
        const std::string test_seq[] = { "abc", "bcd", "def", "fgh", "ijk", "lmn" };
        double x = 0.0;
        for (const auto& i : test_seq)
        {
            const std::string& test = i;
            auto ins_res = trie->insert(test, x);
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
    void test_TrieLowerBound(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        std::map<atom_string_t, double> test_values;
        // Populate trie with unique strings in range from [0..255]
        // this must cause grow of root node
        const atom_string_t test_seq[] = { "abc"_astr, "bcd"_astr, "def"_astr, "fgh"_astr, "ijk"_astr, "lmn"_astr };

        double x = 0.0;
        for (auto i : test_seq)
        {
            const auto& test = i;
            auto ins_res = trie->insert(test, x);
            tresult.assert_true(ins_res.second);
            tresult.assert_that<eq_sets>(ins_res.first.key(), test);

            test_values[test] = x;
            x += 1.0;
        }
        trie.reset();
        tmngr1 = OP::trie::SegmentManager::open<EventSourcingSegmentManager>(test_file_name);
        trie = trie_t::open(tmngr1);
        tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);
        x = 0.0;
        const atom_t* np = nullptr;
        tresult.assert_true(trie->end() == trie->lower_bound(np, np));
        const atom_t az[] = "az";  auto b1 = std::begin(az);
        tresult.assert_that<eq_sets>(test_seq[1], trie->lower_bound(b1, std::end(az)).key());

        const atom_t unexisting1[] = "zzz";
        np = std::begin(unexisting1);
        tresult.assert_that<equals>(
            trie->end(), trie->lower_bound(np, std::end(unexisting1)));

        const atom_t unexisting2[] = "jkl";
        np = std::begin(unexisting2);
        auto exact_find_result = trie->find(test_seq[5]);
        auto lower_result = trie->lower_bound(np, std::end(unexisting2));
        tresult.assert_that<equals>(
            exact_find_result, lower_result
            );

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
            if (fl_res != trie->end())
                tresult.assert_that<less>(fl, fl_res.key(), "Item must be less than ");
            fl_res = trie->lower_bound(fl_div);
            tresult.assert_that<equals>(fl_res.key(), fl_div, "Item must exists");

        }
    }
    void test_TrieLowerBound_ISSUE001(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);

        const atom_string_t test_seq[] = {
            "abc"_astr, "abcx"_astr, "bcd"_astr, "bcdx"_astr, "def"_astr, "fgh"_astr, "fghy"_astr };
        std::map<atom_string_t, double> test_values;

        double x = 0.0;
        for (auto i : test_seq)
        {
            const auto& test = i;
            auto ins_res = trie->insert(test, x);
            test_values.emplace(test, x);
        }

        tresult.assert_that<eq_sets>(trie->range()
            >> OP::flur::then::keep_order_mapping([](const auto& i) {return i.key(); }),
            std::set(std::begin(test_seq), std::end(test_seq))
            );
        //compare_containers(tresult, *trie, test_values);
        auto runex = trie->lower_bound("abcy"_astr);
        tresult.assert_that<logical_not<equals>>(runex, trie->end());
        tresult.assert_that<equals>(runex, trie->find("bcd"_astr));
        tresult.assert_that<equals>(trie->lower_bound("fghx"_astr), trie->find("fghy"_astr));
        tresult.assert_that<equals>(trie->lower_bound("fghz"_astr), trie->end());
    }

    void test_PrefixedFind(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;
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
        auto rdef = trie->lower_bound(i_root, std::string(".xyz"));
        tresult.assert_that<equals>(rdef, trie->find("def."_astr), "iterator must be at last");
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

        tresult.info() << "check prefixed lower_bound after the range behaviour\n";
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

    void test_TrieNoTran(OP::utest::TestRuntime& tresult)
    {
        auto tmngr1 = OP::trie::SegmentManager::create_new<SegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = Trie<SegmentManager, PlainValueManager<double>>;

        std::map<std::string, double> standard;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        std::string rnd_buf;
        for (auto i = 0; i < 1024; ++i)
        {
            tools::random(rnd_buf, 1023, 1);

            auto b = std::begin(rnd_buf);
            auto post = trie->insert(b, std::end(rnd_buf), (double)rnd_buf.length());
            if (post.second)
            {
                standard.emplace(rnd_buf, (double)rnd_buf.length());
            }
            else //test against dupplicates 
            {
                tresult.assert_true(standard.find(rnd_buf) != standard.end(),
                    OP_CODE_DETAILS(<< " standart map must also treate '"
                        << ((const char*)rnd_buf.c_str()) << "' as a duplicate\n"));
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

    void test_Erase(OP::utest::TestRuntime& tresult)
    {
        std::random_device rd;
        std::mt19937 random_gen(rd());
        if (tresult.run_options().random_seed()) //allow override random gen for debug purpose
            random_gen.seed(static_cast<std::mt19937::result_type>(
                *tresult.run_options().random_seed()));

        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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
        for(const p_t& s: seq_data) {
            trie->insert(s.first, s.second);
            test_values.emplace(std::string((const char*)s.first.c_str()), s.second);
        };
        const atom_string_t avg_key((const atom_t*)"4.ab");
        f = trie->find(avg_key);
        tst_next = f;
        ++tst_next;
        tresult.assert_that<equals>(tst_next, trie->erase(f), "iterators mismatch");
        test_values.erase(std::string((const char*)avg_key.c_str()));
        //

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

            for(const atom_string_t& pref: chunks) 
            {
                auto c = ((const char*)pref.c_str())[0];
                auto t = trie->insert(pref, pref.length() + 0.0);
                std::string signed_str(pref.begin(), pref.end());
                auto m = test_values.emplace(signed_str, pref.length() + 0.0);
                tresult.assert_that<equals>(t.second, m.second, OP_CODE_DETAILS(<< " Wrong insert result"));
                tresult.assert_true(tools::container_equals(t.first.key(), m.first->first, &tools::sign_tolerant_cmp<atom_t>),
                    OP_CODE_DETAILS(<< " step #" << i 
                        <<" for key=" << m.first->first 
                        << ", while obtained:" << (const char*)t.first.key().c_str()));
            }
        
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
        trie.reset();
    }
    void test_Siblings(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;
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

    void test_IteratorSync(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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

    void test_TrieUpsert(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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

    void test_TriePrefixedInsert(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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
        for(const p_t& s: ini_data) {
            atom_string_t s1(s.first);
            auto [ins_iter, brand_new] = trie->prefixed_insert(
                start_pair.first, s.first, s.second);
            tresult.assert_true(brand_new, "false duplicate");
            tresult.assert_that<equals>(s0 + s.first, ins_iter.key());
            tresult.assert_that<equals>(s.second, *ins_iter);
            test_values.emplace(s0 + s.first, s.second);
        }
        tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);
        //special case to recover after erase
        atom_string_t abc_str((const atom_t*)"abc");
        auto abc_iter = trie->find(abc_str);
        tresult.assert_false(abc_iter == trie->end(), "abc must exists");
        auto abc_copy = abc_iter; //make copy
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
            tresult.assert_that<equals>(key_str, r.first.key(), "Value already exists");

            });

        tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);
    }

    void test_TriePrefixedUpsert(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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
        for(const p_t& s: ini_data) {
            atom_string_t s1(s.first);
            trie->prefixed_upsert(start_pair.first, s.first, s.second);
            test_values.emplace(s0 + s.first, s.second);
        }
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
            tresult.assert_that<equals>(key_str, r.first.key(), "Value already exists");

            test_values.find(key_str)->second = 3.0;
            });
        tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);
    }

    void test_TriePrefixedIteratorStability(OP::utest::TestRuntime& rt)
    {
        rt.info() << "Test all mutable prefixed operations keep prefix iterator at the right position...\n";
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        auto prefix_a = trie->insert("prefix.a"_astr, 0.0);
        auto prefix_b = trie->insert("prefix.b"_astr, 0.0);

        auto key = "123"_astr;
        auto ins_res = trie->prefixed_insert(
            prefix_a.first, std::begin(key), std::end(key), 0.1);
        rt.assert_true(ins_res.second, "must be unique");
        rt.assert_that<equals>(prefix_a.first.key(), "prefix.a"_astr);

        ins_res = trie->prefixed_upsert(
            prefix_a.first, std::begin(key), std::end(key), 0.1);
        rt.assert_false(ins_res.second, "must be dupplicate");
        rt.assert_that<equals>(prefix_a.first.key(), "prefix.a"_astr);

        ins_res = trie->prefixed_upsert(
            prefix_b.first, std::begin(key), std::end(key), 0.1);
        rt.assert_true(ins_res.second, "must be unique");
        rt.assert_that<equals>(prefix_b.first.key(), "prefix.b"_astr);

    }

    void test_TriePrefixedEraseAll(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        typedef std::pair<atom_string_t, double> p_t;
        std::map<atom_string_t, double> test_values;

        const atom_string_t s0((atom_t*)"a");
        auto start_pair = trie->insert(s0, -1.0);
        auto check_iterator_restore{ start_pair.first };

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
            p_t((atom_t*)"b", 0.1),
            p_t((atom_t*)"bc", 1.),
            p_t((atom_t*)"bc.12", 1.),
            p_t((atom_t*)"bc.122x", 1),
            p_t((atom_t*)"bc.123456789", 1),
            p_t((atom_t*)"bd.12", 1.),
        };
        for(const p_t& s: ini_data) 
        {
            trie->insert(s.first, s.second);
            test_values.emplace(s.first, s.second);
        }
        compare_containers(tresult, *trie, test_values);

        auto abc_iter = trie->prefixed_key_erase_all(s0);
        //prepare test map, by removing all string that starts from 'a' and bigger than 1 char
        for (auto wi = test_values.begin();
            (wi = std::find_if(wi, test_values.end(), 
                [](auto const& itm) {
                    return itm.first[0] == (atom_t)'a' /*&& itm.first.length() > 1*/; 
                })) != test_values.end();
            test_values.erase(wi++));
        compare_containers(tresult, *trie, test_values);
        //special case for restore iterator
        atom_string_t en2("bc"_astr);
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

        //prepare test map, by removing all string that starts from 'bc' 
        for (auto wi = test_values.begin();
            (wi = std::find_if(wi, test_values.end(), 
                [](auto const& itm) {
                    return itm.first.length() >= 2 && itm.first[0] == (atom_t)'b' && itm.first[1] == (atom_t)'c' ;
                })) != test_values.end();
            test_values.erase(wi++));

        f2_copy = f2;
        auto deb_count = trie->prefixed_erase_all(f2_copy);

        tresult.assert_that<equals>(4, deb_count, OP_UTEST_DETAILS());
        f2_copy = f2;
        tresult.assert_that<equals>(0, trie->prefixed_erase_all(f2_copy), 
            OP_UTEST_DETAILS(<< "Check of use obsolete iterator"));
        auto [upsert_ptr, _] = trie->upsert(en2, 57.75); //reinsert "bc" again
        tresult.assert_that<eq_sets>(upsert_ptr.key(), en2);
        tresult.assert_that<equals>(*upsert_ptr, 57.75);
        tresult.assert_that<eq_sets>(trie->find(en2).key(), en2);

        tresult.assert_that<equals>(1, trie->prefixed_erase_all(upsert_ptr), OP_UTEST_DETAILS());

        tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);

        //repeat "bc" test for flag "erase_prefix" = false
        tresult.assert_true(trie->insert("bc.1"_astr, .1).second);
        tresult.assert_true(trie->insert("bc.12"_astr, .12).second);
        tresult.assert_true(trie->insert("bc"_astr, 1.).second);
        tresult.assert_true(trie->insert("bc.a"_astr, 1.).second);
        tresult.assert_true(trie->insert("bc.xbsbhdasdajs"_astr, 1.).second);

        test_values.emplace("bc"_astr, 1.);
        tresult.debug() << "*)========\n";

        f2 = trie->find(en2); //repeat search
        tresult.assert_that<equals>(4, trie->prefixed_erase_all(f2, false));
        tresult.assert_that<equals>(trie->size(), test_values.size(), "Size is wrong");
        compare_containers(tresult, *trie, test_values);
    }

    void test_TriePrefixedKeyEraseAll(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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
            (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'a' && itm.first.length() > 1; })) != test_values.end();
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
            (wi = std::find_if(wi, test_values.end(), [](auto const& itm) {return itm.first[0] == (atom_t)'b' && itm.first[1] == (atom_t)'c'; })) != test_values.end();
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

    void test_NativeRangeSupport(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
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

        using trie_t = OP::trie::Trie<OP::trie::SegmentManager, PlainValueManager<double>>;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr1);
        for (const auto& pair : src1) trie->insert(pair.first, pair.second);
        auto g_pos = trie->find("g"_astr);
        auto flt_src = (trie
            ->range()
            //>> then::filter([](const auto& it) { return it.key().length() > 1/*peek long enough*/; })
            ).compound();
        flt_src.lower_bound(g_pos); //pretty sure 'f' not exists so correct answer are 'g' and 'xyz', but filter skips (len > 1)
        tresult.assert_true(flt_src.in_range(), OP_CODE_DETAILS(<< "end of the range is wrong"));

        tresult.assert_that<equals>(
            flt_src.current().key(),
            "xyz"_atom,
            OP_CODE_DETAILS(<< "lower_bound must point 'XYZ'")
            );

    }

    void test_TrieCheckExists(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

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
    void test_NextLowerBound(TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        typedef std::pair<const atom_string_t, double> p_t;
        auto pair_extractor = [](const auto& i) {
            return p_t(i.key(), *i);
        };

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
        for(const p_t& s: ini_data) 
        {
            trie->insert(s.first, s.second);
            test_values.emplace(s.first, s.second);
        }
        tresult.assert_that<eq_sets>(
            trie->range() >> then::mapping(pair_extractor),
            test_values,
            OP_CODE_DETAILS());
        //for(const auto& i : *trie){
        //    tresult.debug() << (const char*)i.key().c_str() << "=" << i.value() << "\n";
        //});
        auto start_i = trie->find("a"_astr);
        tresult.assert_true(trie->in_range(start_i), 
            OP_CODE_DETAILS( << "wrong find"));
        auto tsti_1 = start_i;
        trie->next_lower_bound_of(tsti_1, "aa"_astr);
        tresult.assert_true(trie->in_range(tsti_1), OP_CODE_DETAILS(<< "wrong exact next_lower_bound"));
        tresult.debug() << "lower_next =" << (const char*)tsti_1.key().c_str() << "\n";
        tresult.assert_that<equals>(tsti_1.key(), "aa"_astr, OP_CODE_DETAILS( << "wrong exact next_lower_bound"));
        auto test = start_i;
        //test through all with 1 step ahead (emulate pure `next` behaviour)
        for (size_t i = 1; i < (sizeof(ini_data) / sizeof(ini_data[0])); ++i)
        {
            trie->next_lower_bound_of(test, ini_data[i].first);
            tresult.debug() << "lower_next(of:" << (const char*)ini_data[i].first.c_str() << ")=" << (const char*)test.key().c_str() << "=>" << test.value() << "\n";;

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
    static std::uint8_t g_test_sensor_seq = 0;
    struct DestroySensor
    {
        std::uint8_t uid;
        float v;
        double pad = 11.11;
        DestroySensor(float a)
            :v(a)
        {
            uid = ++g_test_sensor_seq;
        }
        DestroySensor()
        {
            uid = ++g_test_sensor_seq;
            v = 57.75f;
        }
        ~DestroySensor() 
        {
            //std::cout << "destroy sensor #:" << (int)uid << ", " << v << "\n";
        }
        friend inline std::ostream& operator << (std::ostream& os, const DestroySensor& d)
        {
            return os << "<sensor>:" << (int)d.uid << ", " << d.v;
        }
        bool operator == (const DestroySensor& right) const
        {
            return v == right.v;
        }
    };
    void issue_erase_seq_of_4(TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = Trie<EventSourcingSegmentManager, OP::trie::PlainValueManager<DestroySensor>>;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
        std::map<atom_string_t, DestroySensor> test_values;

        atom_string_t test_seq[] {
            "4sU7E6MZtGxjXTxvZl9biOvaOg85GCLJHeUoIKuZ2R3iQiHUfosGAjZRGhE8WJEDyCf7YJ5CFOi2Ap4xMJZGODVDGuOuy7heCZFnz"_astr,
            "4UbkzaWdUxNFiJLtpK7Ao7HPuEqkZX3uRBI4qznPoNBk5DcQAIuVpzOkN6GM6dtro9YvY4AOB58FNyYatum17ILUy9pTSOoLX1mjdy2SqCp9niCLKHNUUBbA3Qs5afGL4WV6OqGBuzO0gP4lKNYt0dBS9WVax6TKFLwBbeckkSi4ClKi"_astr,
            "4fITmCEZNCxhqbojW4iLcOeesKlHuZqNRk5z2WwhlojtCnqxL6DBx6NMUeylZJTGKbWQqZhWJInjjJYKlyJ1U7TYrBjVv8jvWF4OAj4STDbIncl7iqa2fcJN605yEIRpKSk30O"_astr,
            "4"_astr,
            "4IBLoxMgMVlO5VOQpDnnjo8Nu7gp6a5x2VMPje54NbnYupmbomzOwQbwUB2uAqpKZrsdZeUeQVWxxnXaL7RLcOLk4L4XFVuVeT8poRymOWSpKPtqPE4giYCbcn5Yuugjv1GcwczBeF3mUIcOrZ1RhYm9cdmxMAJOtgeuXSI1athvM8pYhI7"_astr,
            "4hlJLhtnZfexL3EvE2rcHM9ybVyWouBpfuWeIQB4LLJpi4UGEsaZQG4b5iS48bD"_astr,
            "4DdAVCU8ZGVxwOJ7PurlEIlJxUbuhW3ZaJTMO0o6vsvbcTfBQ5F9KgffKuY9h18gMXvrcg9GUqR0VkdhRZmW4sbzEeDhJHAJPSK8bLn4opGwXinar11I2yjLdvVxNPMK7SMqIN82SBK5JMR5aSlQi3dGfIHLSI4F3Ji7boq60N9MNZT1UlEGiKkfgTjtl"_astr,
            "4jyenCJOc5PQdFx11BjSzUydYmUhLoF1vfEoeLxk2DqfV3X6nxRGroHbhc6NjYL3em9RX21RNX9W0Ibzms5q7L99g8qW6hPAxEOvORq3zGfVAR"_astr,
            "4AJkXVQMp8oBx7x1fFGpSQG54ni56G0Uk9WjTBnPPRLay5QTCcc3WCNQBtj0ZobYeEP4Q2EoLYgfbCU163ePn8vOBpYCyMP"_astr,
            "4dTueDWE72V5OqoitnriodH7bfUCr8dINLibVCLySYue17v6HePhllZsd5obs8naubf51XN9SBtTeO9du3ADovNdSzEMiAfFxbHFH1LAdZUiHZ504TNQBzkBis2Kc1HSkZmWqUgsgTBw1BnhZwAYxnsOkru6T6ODgQAYauCnoGHGm0S"_astr,
            "4x8b2sDGwh4BqL9WCqn3XqDfnFxPTYY7F5LAa5SIsBqQeO65y8rML90LvAJykFPsrpjwmkSzVAYaZpqryVRhGH5zmOBj40xEEOMmMrumuwBp2N69vW2vm9Yp6I13kKwn2YFUiXCkwApibXOBkgs2zIxBFV6658rNRBWWt"_astr,
            "4kQ34q04Zi3CX4h8UM1BP4jJWjyI7RM7mkI"_astr,
            "4iEMZwm6Cjz1vLuViBoUDvJ56eNU2WpbZlh99g1pNiNukzRspHbiEcW2DvPjepBKvb6S60T1HNRBBk3kTgW7"_astr,
            "4gKmvHaENlfbFgCyHePind6olqzDtW0Ib52aHdNkm5COsWRlqwflS8LkbThryNLt0xEqR8oYnt8zpNq"_astr,
            "4vT9kwF6j6q8lcpdH9NLjyeUtLNVDlZtgfyVCBvpzlPuLbJeOB5ymZBK5BkRfnBkmYLyP7r5wU3xHYcedzFeSqVMMFJT8uDsRN10Bvsfw8dubfAViczAbwqJL277nSYfWnIrf9LS1aQdxxhuhpQRqrXvGIKUoeotJEg16ZMOb4Ia3QN28a6PziRLJD6hiMoYVv0gb30ou5brSKCM4KztFCZiO43uSC0Ea"_astr,
        };
        for(const auto& s: test_seq)
        {
            trie->insert(s, (float)s.size());
            test_values.emplace(s, (float)s.size());
        }

        size_t n_cnt = trie->nodes_count();
        //tresult.debug() << "Node count:" << n_cnt << "\n";
        compare_containers(tresult, *trie, test_values);
        //tresult.debug() << "*********delmode**********\n";
        trie->prefixed_key_erase_all("4"_astr);
        tresult.assert_that<equals>(n_cnt - 1, trie->nodes_count());
    }

    void issue_next_sibling(TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = test_trie_t;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
        auto [x, y] = trie->insert("a"_astr, 1);
        trie->insert("aa"_astr, 1);
        tresult.debug() << "x=" << (const char*)x.key().c_str();
        trie->next_lower_bound_of(x, "a"_astr);
        tresult.debug() << ", next(x)=" << (const char*)x.key().c_str() << "\n";

        atom_string_t test_seq[] {
            "gAAAAAAAAAI"_astr,
            "gAAAAAAAAAI\x86"_astr,
            "gAAAAAAAAAM"_astr,
            "gAAAAAAAAAM\x86"_astr,
            "gAAAAAAAAAQ"_astr,
            "gAAAAAAAAAU"_astr,
            "gAAAAAAAAAY"_astr,
            "gAAAAAAAAAU\x87"_astr,
            "gAAAAAAAAAU\x87gAAAAAAAAAI"_astr,
            "gAAAAAAAAAY\x87"_astr,
            "gAAAAAAAAAY\x87gAAAAAAAAAI"_astr,
            "gAAAAAAAAAY\x87gAAAAAAAAAM"_astr
        };
        const auto prefix = "\x80"_astr;
        auto [prefix_iter, _] = trie->insert(prefix, 0.0);
        for(const auto& k : test_seq)
        {
            trie->prefixed_insert(prefix_iter, k, 0.1);
        }
        auto dbg_prn =[&](const auto& range){
            for(auto it: range)
                tresult.debug() << '[' << it.key().size() << ']' 
                    << (const char*)it.key().c_str() << "\n";
        };

        dbg_prn(trie->range());
        std::set<atom_string_t> test_values = {
            {"\x80gAAAAAAAAAU\x87"_astr},
            {"\x80gAAAAAAAAAU\x87gAAAAAAAAAI"_astr},
            {"\x80gAAAAAAAAAY\x87"_astr},
            {"\x80gAAAAAAAAAY\x87gAAAAAAAAAI"_astr},
            {"\x80gAAAAAAAAAY\x87gAAAAAAAAAM"_astr}
        };

        auto only_nodes = src::of_container(std::set<atom_string_t>{
                "\x80gAAAAAAAAAI\x87"_astr,
                "\x80gAAAAAAAAAM\x87"_astr,
                "\x80gAAAAAAAAAQ\x87"_astr,
                "\x80gAAAAAAAAAU\x87"_astr/*must exists*/,
                "\x80gAAAAAAAAAY\x87"_astr/*must exists*/
        });
        //JoinGenerator<trie_t, decltype(only_nodes)> joins(
        //    std::const_pointer_cast<trie_t const>(trie), std::move(only_nodes));
        auto joins = only_nodes >> then::prefix_join(trie);
        tresult.debug() << "==========\n";
        dbg_prn(joins);

        tresult.assert_that<eq_sets>(
            joins >> then::mapping([](const auto& i) {return i.key(); }), test_values
            //OP::flur::src::of_container(test_values)
            //>> OP::flur::then::mapping([](const auto&))
            );
    }
    void test_insert_10k(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(
            "10k.trie",
            OP::trie::SegmentOptions()
            .segment_size(0x110000));
        using trie_t = Trie<EventSourcingSegmentManager, PlainValueManager<double>>;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
        constexpr std::uint32_t N = 0x3FFF;
        auto measure = [&](auto f)->double {
            //warm-up
            f(N/3);
            //measure
            auto start = std::chrono::steady_clock::now();
            f(N*2/3);
            auto end = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(end - start).count();
        };
        constexpr static atom_t hex[] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

        //produce exact 3 char size string from uint32 in hex format
        auto to_hext = [&](std::uint32_t from, atom_string_t& dest) {
            dest += hex[(from >> 16) & 0xFu];
            dest += hex[(from >> 12) & 0xFu];
            dest += hex[(from >> 8) & 0xFu];
            dest += hex[(from >> 4) & 0xFu];
            dest += hex[from & 0xFu];
        };
        auto bulk_insert = [&](trie_t::iterator top, std::uint32_t from, std::uint32_t to){
            atom_string_t instr;
            for (std::uint32_t i = from; i < to; ++i)
            {
                instr.clear();
                to_hext(i, instr);
                trie->prefixed_upsert(top, instr, i);
            }
        };
        auto plain = [&](std::uint32_t limit) {
            trie->prefixed_key_erase_all("a"_astr);
            auto [pos, _] = trie->upsert("a"_astr, 0.0);
            bulk_insert(pos, 0, limit);
        };
        std::cout << "plain=" << measure(plain) << "\n";

        if (1 == 1)
        {
            std::mutex mutex;
            std::condition_variable condition;
            std::atomic<bool> control_count = true;

            auto exec_single_thr = [&](trie_t::iterator top, std::uint32_t to) {
                //@!std::unique_lock<std::mutex> lock(mutex);
                //while (control_count) // Handle synchro-start.
                //    condition.wait(lock);
                //lock.unlock();
                bulk_insert(top, 0, to);
            };
            auto mt = [&](std::uint32_t limit) {
                //@!control_count = true;
                trie->prefixed_key_erase_all("a"_astr);
                auto [pos, _] = trie->upsert("a"_astr, -1.0);
                std::array<std::thread, 3> pool{
                    std::thread(exec_single_thr, 
                        trie->prefixed_insert(pos, "0"_astr, -1.1).first, 0xFFF & limit),
                    std::thread(exec_single_thr, 
                        trie->prefixed_insert(pos, "1"_astr, -1.1).first, 0xFFF & limit),
                    std::thread(exec_single_thr, 
                        trie->prefixed_insert(pos, "2"_astr, -1.1).first, 0xFFF & limit)
                };
                //@!control_count = false;
                //@!condition.notify_all();
                for (auto& th : pool)
                    th.join();
            };
            std::cout << "mt=" << measure(mt) << "\n";
        }

    }

    static auto& module_suite = OP::utest::default_test_suite("Trie.core")
        .declare("creation", test_TrieCreation)
        .declare("insertion", test_TrieInsert)
        .declare("insertion-grow", test_TrieInsertGrow)
        .declare("update values", test_TrieUpdate)
        ////.declare("grow-after-update", test_TrieGrowAfterUpdate)
        .declare("lower_bound", test_TrieLowerBound)
        .declare("lower_bound_ISSUE001", test_TrieLowerBound_ISSUE001)
        .declare("prefixed find", test_PrefixedFind)
        .declare("trie no tran", test_TrieNoTran)
        ////.declare("section range", test_TrieSectionRange)
        .declare("erase", test_Erase)
        .declare("siblings", test_Siblings)
        .declare("sync iterator", test_IteratorSync)
        .declare("upsert", test_TrieUpsert)
        .declare("updateStem", test_TrieUpdateStem)
        .declare("prefixed insert", test_TriePrefixedInsert)
        .declare("prefixed upsert", test_TriePrefixedUpsert)
        .declare("prefixed iterator", test_TriePrefixedIteratorStability)
        .declare("prefixed erase_all", test_TriePrefixedEraseAll)
        .declare("prefixed erase_all by key", test_TriePrefixedKeyEraseAll)
        .declare("check_exists", test_TrieCheckExists)
        .declare("next_lower_bound", test_NextLowerBound)
        .declare("issue_erase_seq_of_4", issue_erase_seq_of_4)
        .declare("issue_next_sibling", issue_next_sibling)
        .declare_disabled("insert-10k", test_insert_10k)
        ;
}//ns:""