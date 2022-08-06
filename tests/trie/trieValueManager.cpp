#include <algorithm>
#include <variant>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/EventSourcingSegmentManager.h>

#include <op/trie/Trie.h>

#include "TrieTestUtils.h"

using namespace OP::trie;
using namespace OP::utest;
static const char* test_file_name = "trie.test";


template <class Payload>
struct ValueManager
{
        
};
namespace
{
    void testDefault(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        //static_assert(std::is_standard_layout_v<Payload>,
        //    "only standart-layout allowed in TrieNode");

        struct TestStruct
        {
            TestStruct(double a,
                float x1 = 1, float x2 = 2, float x3 = 3,
                float x4 = 4, float x5 = 5, float x6 = 6,
                float x7 = 7, float x8 = 8, float x9 = 9,
                float x10 = 10, float x11 = 11)
                : v1(a), vector1{ x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11 }
            {}
            double v1;
            float vector1[11];
        };
        using test_payload_t = TestStruct;// std::variant<TestStruct, double, int>;

        typedef Trie<EventSourcingSegmentManager, test_payload_t> trie_t;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

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

        for (const p_t& s : ini_data)
        {
            trie->insert(s.first, s.second);
            test_values.emplace(s);
        }
    }


    static auto& module_suite = OP::utest::default_test_suite("trie-values")
        .declare("default", testDefault)
        ;
}