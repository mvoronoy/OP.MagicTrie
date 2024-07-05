#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>


namespace 
{
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace std::string_literals;

    bool logic_or(bool a, bool b)
    {
        return a || b;
    }

    bool op_logic_or(zip_opt<bool> a, zip_opt<bool> b)
    {
        if (a)
            return *a;
        if(b)
            return *b;
        throw std::runtime_error("both sequences cannot produce empty");
    }

    void test_SrcZip(OP::utest::TestRuntime& tresult)
    {
        tresult.info() << "Zip with empty source\n";

        tresult.assert_that<eq_sets>(
            src::zip(
                logic_or,
                src::null<bool>(),
                src::of_value(false)//not empty
            ), std::array<bool, 0>{});

        tresult.assert_that<eq_sets>(
            src::zip(
                logic_or,
                src::of_value(false),//not empty
                src::null<bool>()
            ), std::array<bool, 0>{});

        // test empty with longest_sequence zip_opt
        tresult.assert_that<eq_sets>(
            src::zip_longest(
                op_logic_or,
                src::null<bool>(),
                src::null<bool>()
            ), std::array<bool, 0>{});

        tresult.assert_that<eq_sets>(
            src::zip_longest(
                op_logic_or,
                src::of_value(false),//not empty
                src::null<bool>()
            ), std::array<bool, 1>{false});
        //////////////////////////////////////
        tresult.assert_that<eq_sets>(
            src::zip(
                [](int i, char c) -> std::string { // Convert zipped pair to string
                        std::ostringstream result;
                        result << i << c;
                        return result.str();
                }, 
                src::of_container(std::array{1, 2, 3}),
                src::of_container("abcd"s) //expecting 'd' is omitted
            ), 
            std::array{ "1a"s, "2b"s, "3c"s }
        );

    }

    void test_ThenZip(OP::utest::TestRuntime& tresult)
    {
        tresult.info() << "Zip with empty source\n";
        tresult.assert_that<eq_sets>(
            src::null<bool>()
            >> then::zip(
                logic_or,
                src::null<int>()
            ), 
            std::array<bool, 0>{});

        tresult.assert_that<eq_sets>(
            src::of_value(false)
            >> then::zip(
                logic_or,
                src::null<bool>()
            ), 
            std::array<bool, 0>{});

        auto print_optional = [](std::ostream& os, const auto& v) -> std::ostream& { return v ? (os << *v) : (os << '?'); };
        auto zip3 = src::of_container(std::array{1, 2, 3})
            >> then::zip_longest( 
                // Convert zipped triplet to string with '?' when optional is empty
                // Note: All arguments must be `std::optional`
                [&](const auto& i, const auto& c, const auto& f) -> std::string {
                        std::ostringstream result;
                        print_optional(result, i);
                        print_optional(result, c);
                        print_optional(result, f); 
                        return result.str();
                }, 
                src::of_container("abcd"s), // String as source of 4 characters
                src::of_container(std::array{1.1f, 2.2f}) // Source of 2 floats
            )
            ;
        tresult.assert_that<eq_sets>(zip3,
            std::array{
                "1a1.1"s,
                "2b2.2"s,
                "3c?"s,
                "?d?"s });

    }


    static auto& module_suite = OP::utest::default_test_suite("flur.then.zip")
    .declare("src_zip", test_SrcZip)
    .declare("then_zip", test_ThenZip)
    ;
}//ns: