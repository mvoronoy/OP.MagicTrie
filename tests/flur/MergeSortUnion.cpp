#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>
#include <op/flur/UnionAll.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;
namespace {

    
    void test_union_merge(TestRuntime& tresult)
    {
        using test_container_t = std::multiset<int>;
        tresult.info() << "union_merge of empties\n";
        test_container_t empty;
        auto ee = src::of_container(/*copy*/(empty))
            >> then::union_merge(src::of_container(/*copy*/(empty)));
        tresult.assert_true(
            std::empty(ee), "all empty");

        tresult.assert_that<eq_sets>(
            src::of_container(empty) 
            >> then::union_merge(src::of_container(test_container_t{1, 2, 3})), 
            std::vector{ 1, 2, 3 }, "left empty");

        tresult.assert_that<eq_sets>(
            src::of_container(test_container_t{ 1, 1, 3 })
            >> then::union_merge(src::of_container(empty)),
            std::vector{ 1, 1, 3 }, "right empty");

        tresult.info() << "union multitypes...\n";
        test_container_t dat1{ 1, 2, 3 };
        auto r1 = 
            src::of_container(dat1)
            >> then::union_merge(src::of_cref_value(10)) 
            >> then::union_merge(src::of_cref_iota(100, 103))
               ;
        tresult.assert_that<eq_sets>(r1,
            std::vector{ 1, 2, 3, 10, 100, 101, 102 }
            );

        auto r2 = 
            src::of_container(test_container_t{ 1, 1 })
            >> then::union_merge(src::of_cref_value(1))
            >> then::union_merge(src::null<const int&>())
               ;
        tresult.assert_that<eq_sets>(r2, std::vector{ 1, 1, 1 });
        //test it works twice
        auto seq = r2.compound();
        std::vector expected{ 1, 1, 1 };
        tresult.assert_that<eq_ranges>(std::begin(seq), std::end(seq), 
            std::begin(expected), std::end(expected));
        tresult.assert_that<eq_ranges>(std::begin(seq), std::end(seq), 
            std::begin(expected), std::end(expected));

        auto r3 = 
            src::null<int>()
            >> then::union_merge(src::of_iota(1, 4))
            >> then::union_merge(src::of_value(1))
               ;
        tresult.assert_that<eq_sets>(r3, std::vector{ 1, 1, 2, 3 });

        //
        auto r4 =
            src::of_container(test_container_t{ 1, 3, 5, 7 })
            >> then::union_merge(src::null<const int&>())
            >> then::union_merge(src::of_container(test_container_t{ 2, 4, 6, 8 }))
            ;

        tresult.assert_that<eq_sets>(r4, src::of_iota(1, 9));
    }


    static auto& module_suite = OP::utest::default_test_suite("flur.union_merge")
        .declare("basic", test_union_merge)
        ;
}//ns: empty
