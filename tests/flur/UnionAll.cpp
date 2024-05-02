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

    
    void test_union_all(TestRuntime& tresult)
    {
        using test_container_t = std::vector<int>;
        tresult.info() << "union_all of empties\n";
        test_container_t empty;
        auto ee = src::of_container(/*copy*/(empty))
            >> then::union_all(src::of_container(/*copy*/(empty)));
        tresult.assert_true(
         std::empty(ee), "all empty");

        tresult.assert_that<eq_sets>(
            src::of_container(empty) 
            >> then::union_all(src::of_container(std::vector{1, 2, 3})), 
            std::vector{ 1, 2, 3 }, "left empty");

        tresult.assert_that<eq_sets>(
            src::of_container(std::vector{ 1, 2, 3 })
            >> then::union_all(src::of_container(empty)),
            std::vector{ 1, 2, 3 }, "right empty");

        tresult.info() << "union multitypes...\n";
        std::vector dat1{ 1, 2, 3 };
        auto r1_dat1 = 
            src::of_container(dat1)
            >> then::mapping([](const int& i)->int {return i; })
            >> then::union_all(
                src::of_value(10), 
                src::of_iota(100, 103));
        tresult.assert_that<eq_sets>(r1_dat1,
            std::vector{ 1, 2, 3, 10, 100, 101, 102 }
            );
    }

    void test_shared(TestRuntime& tresult)
    {

        tresult.info() << "union Polymorphs ...\n";
        ;
        auto r1_dat1 =
            make_shared(
                src::of_container(std::vector{ 1, 2, 3 }) 
                >> then::mapping([](const int& i)->int {return i; })) |
            make_shared(src::of_value(10)) |
            make_shared(src::of_iota(100, 103))
            ;
        
        tresult.assert_that<eq_sets>(*r1_dat1,
            std::vector{ 1, 2, 3, 10, 100, 101, 102 }
            );
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.union_all")
        .declare("basic", test_union_all)
        .declare("shared", test_shared)
        ;
}//ns: empty
