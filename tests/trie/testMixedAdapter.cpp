#if _MSC_VER > 1000
#pragma warning(disable:4503)
#endif // _MSC_VER > 1000

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/EventSourcingSegmentManager.h>

#include <op/trie/MixedAdapter.h>
#include <op/common/NamedArgs.h>
#include <op/common/astr.h>
#include <op/trie/JoinGenerator.h>

#include <algorithm>
#include "../test_comparators.h"
#include "TrieTestUtils.h"

using namespace OP::trie;
using namespace OP::utest;
using namespace OP::common;
static const char* test_file_name = "trie.test";

void testDefault(OP::utest::TestRuntime& tresult)
{
    using namespace OP::flur;
    auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    using trie_t = Trie<EventSourcingSegmentManager, PlainValueManager<double>> ;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using mix_ns = Ingredient<trie_t>;

    auto test_range0 = OP::trie::make_mixed_sequence_factory(
        std::static_pointer_cast<trie_t const>(trie));
    tresult.assert_true(std::empty(test_range0), "Wrong empty case");

    std::map<atom_string_t, double> test_values;

    using p_t = std::pair<atom_string_t, double>;

    const p_t ini_data[] = {
        p_t("ab.c"_astr, 1.),
        p_t("abc"_astr, 1.),
        p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.333"_astr, 1.33), //not a child since only 'abc.3' in the result
        p_t("abc.444"_astr, 1.444), // a child
        p_t("abcdef"_astr, 2.0),
    };

    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        test_values.emplace(s);
        });
    auto range_default = (
        OP::trie::make_mixed_sequence_factory(std::const_pointer_cast<const trie_t>(trie))
        >> then::mapping([](const auto& i) {
            return std::pair<const atom_string_t, double>(i.key(), i.value());
            })
        );
    tresult.assert_that<eq_sets>(range_default, test_values, OP_CODE_DETAILS());
}

void testChildConfig(OP::utest::TestRuntime& tresult)
{
    using namespace OP::flur;
    auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    using trie_t = Trie<EventSourcingSegmentManager, PlainValueManager<double>>;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using mix_ns = Ingredient<trie_t>;

    using p_t = std::pair<const atom_string_t, double>;

    const p_t ini_data[] = {
        p_t("ab.c"_astr, 1.),
        p_t("abc"_astr, 1.),
        p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.333"_astr, 1.33), //not a child since only 'abc.3' in the result
        p_t("abc.444"_astr, 1.444), // a child
        p_t("abcdef"_astr, 2.0),
    };

    for(const p_t& s: ini_data) 
    {
        trie->insert(s.first, s.second);
    }
    tresult.info() << "first/last child\n";

    atom_string_t common_prefix = "abc"_astr;

    auto test_range = OP::trie::make_mixed_sequence_factory(
        std::const_pointer_cast<const trie_t>(trie)
        ,
        typename mix_ns::ChildBegin(trie->find(common_prefix)),
        typename mix_ns::SiblingNext{},
        typename mix_ns::ChildInRange(StartWithPredicate(common_prefix))
    );

    std::map<atom_string_t, double> test_values = {
        p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.444"_astr, 1.444),
        p_t("abcdef"_astr, 2.0),
    };
    tresult.assert_that<eq_sets>(
        (test_range
            >> then::mapping([](const auto& i) {
                return p_t(i.key(), *i);
                })
            ), test_values, "key mismatch");

    auto test_range2 = OP::trie::make_mixed_sequence_factory(
        std::const_pointer_cast<const trie_t>(trie),
        mix_ns::ChildBegin(trie->find(common_prefix))
    ) >> then::mapping([](const auto& i) {
        return p_t(i.key(), *i);
        });
    test_values.clear();
    test_values = {
    p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.333"_astr, 1.33), //not a child since only 'abc.3' in the result
        p_t("abc.444"_astr, 1.444), // a child
        p_t("abcdef"_astr, 2.0),
    };
    tresult.assert_that<eq_sets>(test_range2, test_values, OP_CODE_DETAILS());
}
/**Issue with MixedRange when not all next_sibling returned*/
void test_ISSUE_0002(OP::utest::TestRuntime& tresult)
{
    using namespace OP::flur;
    auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    using trie_t = Trie<EventSourcingSegmentManager, PlainValueManager<double>>;

    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using mix_ns = Ingredient<trie_t>;

    using p_t = std::pair<const atom_string_t, double>;

    const p_t ini_data[] = {
        p_t("\0x80AAAAAg"_astr, 1.),
        p_t("\0x80AAAAAg\0x81"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC"_astr, 0.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC\0x82"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC\0x82AAAAAg"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC\0x83"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC\0x83AAAAAg"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD"_astr, 0.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD\0x82"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD\0x82AAAAAg"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD\0x83"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD\0x83AAAAAw"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD"_astr, 0.),
        p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD\0x82"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD\0x82AAAAAw"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD\0x83"_astr, 1.),
        p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD\0x83AAAAAw"_astr, 1.),
    };
    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        });

    atom_string_t result_prefix = "\0x80AAAAAg\0x81"_astr;
    auto range_of_trie = trie->range();

    auto source_range = OP::trie::make_mixed_sequence_factory(
        std::const_pointer_cast<const trie_t>(trie),
        typename Ingredient<trie_t>::template ChildOfKeyBegin<>(result_prefix),
        typename Ingredient<trie_t>::SiblingNext{}
    ) >> then::mapping([](const auto& i) {
        return p_t(i.key(), *i);
        });

    std::map<atom_string_t, double> test_values = {
            p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC"_astr, 0.),
            p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD"_astr, 0.),
            p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD"_astr, 0.)
    };
    tresult.assert_that<eq_sets>(source_range, test_values, OP_CODE_DETAILS());
}

void test_PrefixJoin(TestRuntime& tresult)
{
    using namespace OP::flur;
    //just reusable lambda that takes key() from trie iterator
    auto get_key_only = [](const auto& pair) {return pair.key(); };

    auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    using trie_t = Trie<EventSourcingSegmentManager, PlainValueManager<double>>;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    //empty trie intersection
    tresult.assert_true(std::empty(
        src::of_value("aaa"_astr)
        >> then::prefix_join(trie)
        >> then::mapping(get_key_only)),
        OP_CODE_DETAILS(<< "failed empty case"));

    using p_t = std::pair<const atom_string_t, double>;

    const p_t ini_data[] = {
        p_t("12"_astr, 0.9),
        p_t("124"_astr, 0.9),
        p_t("123.1"_astr, 1.),
        p_t("123.2"_astr, 1.),
        p_t("123.3"_astr, 1.3),

        p_t("ab.c"_astr, 1.),
        p_t("ab.k"_astr, 1.),

        p_t("abc"_astr, 1.),
        p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.333"_astr, 1.33), //not a child since only 'abc.3' in the result
        p_t("abc.444"_astr, 1.444), // a child
        p_t("abcdef"_astr, 2.0),
        p_t("a_"_astr, 2.0),
        p_t("___"_astr, 2.0),
    };

    std::for_each(std::begin(ini_data), std::end(ini_data), [&](const p_t& s) {
        trie->insert(s.first, s.second);
        });

    std::set<atom_string_t> prefix_set{ "\t"_astr, "123"_astr, "789"_astr, "abc"_astr, "xyz"_astr };
    using namespace OP::flur;
    auto trie_range =
        src::of_container(prefix_set)
        >> then::prefix_join(trie)
        ;
    //for (const auto& pair : trie_range)
    //{
    //    std::cout << (const char*)pair.key().c_str() << "\n";
    //}
    tresult.assert_that<eq_sets>(
        trie_range >> then::mapping(get_key_only),
        src::of_container(std::set<atom_string_t>{
        "123.1"_astr,
            "123.2"_astr,
            "123.3"_astr,
            "abc"_astr,
            "abc.1"_astr,
            "abc.2"_astr,
            "abc.3"_astr,
            "abc.333"_astr,
            "abc.444"_astr,
            "abcdef"_astr,
    })
    );
    tresult.info() << "prefix-join empty result\n";
    tresult.assert_true(std::empty(
        src::of_value(""_astr + (std::uint8_t)('_'+1) + "am"_astr)
        >> then::prefix_join(trie)
        >> then::mapping(get_key_only)),
        OP_CODE_DETAILS(<< "failed empty case #2"));
}

std::string pad_str(int i) {
    std::string base = std::to_string(i);
    //add '0' for padding
    return std::string(4 - base.size(), '0') + base;
};

template <size_t NRoots, size_t NChildrens, class TSegmentManager>
void render_children_tree()
{
    using namespace OP::flur;
    using wr_trie_t = Trie<TSegmentManager, PlainValueManager<double>>;

    auto mngr_no_tran = TSegmentManager::template create_new<TSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));
    std::shared_ptr<wr_trie_t> wr_trie = wr_trie_t::create_new(mngr_no_tran);

    //render 500*100 keys
    auto crtsn = apply::cartesian(
        src::of_iota<size_t>(0, NRoots)
        >> then::mapping([&](auto i)
            {
                auto insres = wr_trie->insert(pad_str(i), 0);
                assert(insres.second);
                return insres.first;
            })
        >> then::repeater(),
        src::of_iota<size_t>(0, NChildrens)
                >> then::mapping(pad_str));

    crtsn.collect([&](const auto& trie_iter, const auto& n2)
        {
            auto prev = trie_iter;
            wr_trie->prefixed_insert(prev, n2, 57.7564);
        });
    wr_trie.reset();
}

void test_UnorderedOrderFlatMap(TestRuntime& tresult)
{
    using namespace OP::flur;
    using trie_t = Trie<EventSourcingSegmentManager, PlainValueManager<double>>;
    constexpr size_t NRoots = 500;
    constexpr size_t NChildren = 100;
    render_children_tree<NRoots, NChildren, SegmentManager>();
    auto tmngr = OP::trie::SegmentManager::open<EventSourcingSegmentManager>(test_file_name);
    std::shared_ptr<trie_t> trie = trie_t::open(tmngr);
    using trie_iter_t = typename trie_t::iterator;
    OP::utils::ThreadPool tpool;
    auto g_custom_compare = OP::flur::custom_compare(
        [](const auto& left, const auto& right) -> int {
            auto lb = std::begin(left);
            auto le = std::end(left);
            auto rb = std::begin(right);
            auto re = std::end(right);
            return OP::common::str_lexico_comparator(lb, le, rb, re);
        });

    auto all_prefix = src::of_iota<size_t>(0, 500)
        >> then::mapping([&](auto i)
            {
                return pad_str(i);
            })
        //>> then::prefix_join(trie)
        //>> then::minibatch<32>(tpool)
        >> then::ordering_flat_mapping([&](const auto& iter) {
                return trie->children_range(iter) 
                >> then::keep_order_mapping([prefix_len = iter.size()](const auto& i) {
                        return OP::common::atom_string_view_t(i.key()).substr(prefix_len);
                    });
            }, g_custom_compare)
        ;
    size_t count = 0;
    typename trie_t::key_t prev;
    for (auto i : all_prefix)
    {
        if (count++ > 0)
        {
            tresult.assert_that<less_or_equals>(prev, i, 
                OP_CODE_DETAILS("Sequence is not ordered"));
        }
        prev = i;
    }

    tresult.assert_that<equals>(
        50000, all_prefix >>= OP::flur::apply::count());
    //trie->prefixed_range("1"_astr);
}

static auto& module_suite = OP::utest::default_test_suite("MixedAdapter")
.declare("default", testDefault)
.declare("child", testChildConfig)
.declare("ISSUE_0002", test_ISSUE_0002)
.declare("prefix-join", test_PrefixJoin)
.declare("prefix-join-unordered", test_UnorderedOrderFlatMap)
;
