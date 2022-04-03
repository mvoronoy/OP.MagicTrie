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
    void test_mk_polymorph(OP::utest::TestResult& tresult)
    {
        using set_t = std::set <std::string >;

        const set_t subset{ "aaa", "bbb", "ccc" };
        std::string vowels = { 'a', 'e', 'i', 'o', 'u', 'y' };
        auto ptr1 = make_unique(
            src::of(subset) >> 
            then::filter([](const auto&s) 
                {
                    return s.length() > 2; 
                })
        );
        set_t target;
        //auto pset = ptr1->compound_unique();
        for (auto const& a : *ptr1) 
        {
            target.insert(a);
        }
        tresult.assert_that<equals>(subset, target, "two sets must be identical");
        //test unpack can deal with unique_ptr
        auto ptr2 = make_unique(
            std::shared_ptr(std::move(ptr1)),
            then::filter([&](const auto&s) { return vowels.find(s[0]) != std::string::npos; })
        );

    }
    /** This test generally exists for compile-time only capability to cast 
    *  from ordered to unordered set. At runtime it doesn't check any valuable features
    */
    void test_polymorph_cast(OP::utest::TestResult& tresult)
    {
        using sorted_seq_t = OP::flur::AbstractPolymorphFactory<true, std::string const&>;
        using seq_t = OP::flur::AbstractPolymorphFactory<false, std::string const&>;

        auto cast = [](std::shared_ptr<sorted_seq_t>& sq) -> std::shared_ptr<seq_t>
        {
            return std::static_pointer_cast<seq_t>(sq);
        };
        
        auto res = cast(
            make_shared(src::of_container(std::set<std::string>{})));
        tresult.assert_true(std::empty(*res));
    }
    static auto module_suite = OP::utest::default_test_suite("flur.polymorphic")
        ->declare(test_mk_polymorph, "basic")
        ->declare(test_polymorph_cast, "cast")
        ;
} //ns:<anonymous>
