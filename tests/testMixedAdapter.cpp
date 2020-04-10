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
#include <op/trie/MixedAdapter.h>
#include <op/common/NamedArgs.h>

#include <algorithm>
#include "test_comparators.h"
#include "AtomStrLiteral.h"
#include "TrieTestUtils.h"

using namespace OP::trie;
using namespace OP::utest;
static const char* test_file_name = "trie.test";

void testDefault(OP::utest::TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using mix_ns = Ingredient<trie_t>;


    mix_ns mix0{};
    using my_range0 = OP::trie::MixAlgorithmRangeAdapter < trie_t >;
    my_range0 test_range0{ trie, mix0 };
    tresult.assert_false(test_range0.in_range(test_range0.begin()), "Wrong empty case");

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
    auto range_default = OP::trie::make_mixed_range(std::const_pointer_cast<const trie_t>(trie));
    compare_containers(tresult, *range_default, test_values);

}
void testChildConfig(OP::utest::TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using mix_ns = Ingredient<trie_t>;

    using p_t = std::pair<atom_string_t, double> ;

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
    });
    tresult.info() << "first/last child\n";
    
    atom_string_t common_prefix = "abc"_astr;
    
    using my_range = OP::trie::MixAlgorithmRangeAdapter< trie_t, mix_ns::ChildBegin, mix_ns::SiblingNext, mix_ns::ChildInRange>;

    my_range test_range{ trie, common_prefix, trie->find(common_prefix), StartWithPredicate(common_prefix) };
    auto first_ch1 = test_range.begin();//of 'abc'

    tresult.assert_that<equals>(first_ch1.key(), "abc.1"_atom, "key mismatch");
    tresult.assert_that<equals>(*first_ch1, 1., "value mismatch");

    std::map<atom_string_t, double> test_values = {
        p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.444"_astr, 1.444),
        p_t("abcdef"_astr, 2.0),
    };
    compare_containers(tresult, test_range, test_values);

    using my_range2 = OP::trie::MixAlgorithmRangeAdapter< trie_t, mix_ns::ChildBegin>;

    my_range2 test_range2{ trie, trie->find(common_prefix) };
    test_values.clear();
    test_values = {
    p_t("abc.1"_astr, 1.),
        p_t("abc.2"_astr, 1.),
        p_t("abc.3"_astr, 1.3),
        p_t("abc.333"_astr, 1.33), //not a child since only 'abc.3' in the result
        p_t("abc.444"_astr, 1.444), // a child
        p_t("abcdef"_astr, 2.0),
    };
    compare_containers(tresult, test_range2, test_values);
}
/**Issue with MixedRange when not all next_sibling returned*/
void test_ISSUE_0002(OP::utest::TestResult& tresult)
{
    auto tmngr = OP::trie::SegmentManager::create_new<TransactedSegmentManager>(test_file_name,
        OP::trie::SegmentOptions()
        .segment_size(0x110000));

    typedef Trie<TransactedSegmentManager, double> trie_t;
    std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);
    using mix_ns = Ingredient<trie_t>;

    using p_t = std::pair<atom_string_t, double>;

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
    auto source_range = OP::trie::make_mixed_range(
        std::const_pointer_cast<const trie_t>(trie),
        OP::trie::Ingredient<trie_t>::ChildOfKeyBegin(
            result_prefix),
        OP::trie::Ingredient<trie_t>::SiblingNext());

    std::map<atom_string_t, double> test_values = {
            p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAC"_astr, 0.),
            p_t("\0x80AAAAAg\0x81AAAAAgAAAAIAAAAD"_astr, 0.),
            p_t("\0x80AAAAAg\0x81AAAAAwAAAAIAAAAD"_astr, 0.)
    };
    compare_containers(tresult, *source_range, test_values);
}

static auto module_suite = OP::utest::default_test_suite("MixedAdapter")
    ->declare(testDefault,  "default")
    ->declare(testChildConfig, "child")
    ->declare(test_ISSUE_0002, "ISSUE_0002")
;
