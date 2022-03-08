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
    static auto module_suite = OP::utest::default_test_suite("flur.polymorphic")
        ->declare(test_mk_polymorph, "basic")
        ;
} //ns:<anonymous>
