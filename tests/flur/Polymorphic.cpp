#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>

#include <op/common/ftraits.h>

#include <op/flur/flur.h>
#include <op/flur/Polymorphs.h>

namespace {
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace std::string_literals;
    void test_mk_polymorph(OP::utest::TestRuntime& tresult)
    {
        using set_t = std::set <std::string >;

        const set_t subset{ "aaa", "bbb", "ccc" };
        std::string vowels = { 'a', 'e', 'i', 'o', 'u', 'y' };
        auto ptr1 = make_shared(
            src::of_container(subset) >> 
            then::filter([](const auto&s) 
                {
                    return s.length() > 2; 
                })
        );
        set_t target;

        for (auto const& a : *ptr1) 
        {
            target.insert(a);
        }
        tresult.assert_that<equals>(subset, target, "two sets must be identical");
        //test unpack 
        auto ptr2 = make_lazy_range(
            std::shared_ptr(std::move(ptr1)),
            then::filter([&](const auto&s) { return vowels.find(s[0]) != std::string::npos; })
        );
        tresult.assert_that<eq_sets>(ptr2, std::array{"aaa"s}, "vowel set only");

    }

    void test_polymorph_to_lazy_range(OP::utest::TestRuntime& tresult)
    {
        auto src = 
            make_shared(src::of_container(std::set<std::string>{"a", "b", "c"}));
        auto target = src::back_to_lazy(src);
        using back_struct_t = std::decay_t<OP::flur::details::unpack_t<decltype(target)>>;
        tresult.assert_true(target.compound().is_sequence_ordered(), "must produce ordered");
        for(const auto&c : target)
            tresult.debug() << c << "\n";
        tresult.assert_that<eq_sets>(target, std::set<std::string>{ "a"s, "b"s, "c"s }, OP_CODE_DETAILS());

        auto src2 =
            make_shared(src::of_container(std::vector<std::string>{"a", "b", "c"}));
        auto target2 = src::back_to_lazy(src2);
        using back_struct2_t = std::decay_t<OP::flur::details::unpack_t<decltype(target2)>>;
        tresult.assert_false(
            target2.compound().is_sequence_ordered(), 
            "must produce un-ordered");

    }
    static auto& module_suite = OP::utest::default_test_suite("flur.polymorphic")
        .declare("basic", test_mk_polymorph)
        
        .declare("back", test_polymorph_to_lazy_range)
        ;
} //ns:<anonymous>
