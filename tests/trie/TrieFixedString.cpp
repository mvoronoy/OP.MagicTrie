#include <map>
#include <any>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/EventSourcingSegmentManager.h>

#include <op/trie/Trie.h>
#include <op/trie/PlainValueManager.h>

#include <op/common/astr.h>
#include <op/common/FixedString.h>

#include "TrieTestUtils.h"

namespace
{
    namespace opc = OP::common;
    
    using namespace OP::utest;

    const char* test_file_name = "trie.test";

    constexpr size_t string_limit_c = 64;
    constexpr size_t workload_size_c = 4000;

    template <class Trie, class TSample>
    void _insert_tes(OP::utest::TestRuntime& tresult, Trie& trie, const TSample& volume)
    {
        for(const auto& [k, v]: volume)
            trie.insert(k, v);
/*        auto ti = trie.begin();
        size_t n = 0;
        //order relaxed
        for (; trie.in_range(ti); trie.next(ti), ++n)
        {
            const auto& key = ti.key();
            std::cout << n << ")";
            std::cout.write((const char*)key.data(), key.size());
            std::cout << "\n";
        }
        */

    }

    using worload_t = 
            std::map<opc::atom_string_t, double>;

    std::any init_workload()
    {
        worload_t result;

        opc::atom_string_t buffer;
        for(auto n = 0; n < workload_size_c; ++n)
        {
            result.insert({
                tools::RandomGenerator::instance().next_alpha_num(buffer, 60, 3),
                static_cast<double>(n)});
        }
        return std::any{std::move(result)};
    }

    void testDefault(OP::utest::TestRuntime& tresult, const worload_t& workload)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<OP::trie::EventSourcingSegmentManager>(
            test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using ufstr_t = OP::FixedString<OP::fix_str_policy_noexcept<std::uint8_t, 64>>;

        //using trie_t = Trie<
        //    EventSourcingSegmentManager, PlainValueManager<double>, OP::common::atom_string_t> ;
        using trie_t = OP::trie::Trie<
            OP::trie::EventSourcingSegmentManager, OP::trie::PlainValueManager<double>, ufstr_t
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        auto test_range0 = OP::trie::make_mixed_sequence_factory(
            std::static_pointer_cast<trie_t const>(trie));
        tresult.assert_true(std::empty(test_range0), "Wrong empty case");

        _insert_tes(tresult, *trie, workload);
        
        using trie_iter_t = typename trie_t::iterator;
        auto range_default = (
            OP::trie::make_mixed_sequence_factory(std::const_pointer_cast<const trie_t>(trie))
            >> then::mapping([](const /*trie_iter_t*/auto& i) -> decltype(auto) {
                return std::pair<const ufstr_t, double>(i.key(), i.value());
                })
            );
        //tresult.assert_that<eq_sets>(range_default, workload, OP_CODE_DETAILS());

        auto mapi = workload.begin();
        for(const auto& [k, v]: range_default)
        {
            auto& captured_key = k; //c++ standard P0588R1 doesn't allow capture bindings inside lambda
            tresult.assert_that<equals>(k, mapi->first,
                OP_CODE_DETAILS( 
                    << "expecting:'" << debug::prn_ustr(mapi->first) 
                    << "', while:'" << debug::prn_ustr(captured_key) << "'"));

            tresult.assert_that<almost_eq>(v, mapi->second);
            ++mapi;
        }
        tresult.assert_true(mapi == workload.end()); 
    }

    void testAtomStr(OP::utest::TestRuntime& tresult, const worload_t& workload)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<OP::trie::EventSourcingSegmentManager>(
            test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = OP::trie::Trie<
            OP::trie::EventSourcingSegmentManager, OP::trie::PlainValueManager<double>, OP::common::atom_string_t> ;

        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        auto test_range0 = OP::trie::make_mixed_sequence_factory(
            std::static_pointer_cast<trie_t const>(trie));
        tresult.assert_true(std::empty(test_range0), "Wrong empty case");

        _insert_tes(tresult, *trie, workload);

        auto range_default = (
            OP::trie::make_mixed_sequence_factory(std::const_pointer_cast<const trie_t>(trie))
            >> then::mapping([](const auto& i) {
                return std::pair<const OP::common::atom_string_t, double>(i.key(), i.value());
                })
            );
        //tresult.assert_that<eq_sets>(range_default, workload, OP_CODE_DETAILS());

        auto mapi = workload.begin();
        for (const auto& [k, v] : range_default)
        {
            auto& captured_key = k; //c++ standard P0588R1 doesn't allow capture bindings inside lambda
            tresult.assert_that<equals>(k, mapi->first,
                OP_CODE_DETAILS(
                    << "expecting:'" << debug::prn_ustr(mapi->first)
                    << "', while:'" << debug::prn_ustr(captured_key) << "'"));

            tresult.assert_that<almost_eq>(v, mapi->second);
            ++mapi;
        }
        tresult.assert_true(mapi == workload.end());
    }

    static auto& module_suite = OP::utest::default_test_suite("Trie.fixed_string")
        .before_suite(init_workload)
        .declare("default", testDefault)
        .declare("compare-with-atomstr", testAtomStr)
       ;

}//ns:
