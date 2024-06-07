#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;

struct TestPair
{
    std::string first;
    double second;

    //copy elision issue for MSVC been there, 
    // so replaced `const string&` with `string`
    TestPair(std::string s, double f)
        : first(s)
        , second(f)
    {}

    TestPair(const TestPair& other)
        : first(other.first)
        , second(other.second)
    {
    }

    TestPair(TestPair&& other) = default;
    TestPair& operator = (TestPair&& other) = default;
    TestPair& operator = (const TestPair& other) = default;

    bool operator == (const TestPair& other) const
    {
        return first == other.first;
    }
    bool operator < (const TestPair& other) const
    {
        return first < other.first;
    }
};


namespace {

    void test_u_u_join(TestRuntime& tresult)
    {
        using test_container_t = std::vector<std::string>;
        test_container_t src1{
            "a"s, "ab"s, "b"s, "bc"s, "c"s, "cd"s, 
            "d"s, "def"s, "g"s, "xyz"s
        };
        std::shuffle(src1.begin(), src1.end(), tools::RandomGenerator::instance().generator());

        auto a1 = src::of_container(std::cref(src1));

        tresult.info() << "u_u_join with empty\n";
        test_container_t empty;
        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src1) >> then::unordered_join( src::of_container(empty)), empty, "right empty");
        tresult.assert_that<eq_unordered_sets>(
            src::of_container(empty) >> then::unordered_join(src::of_container(src1)), empty, "left empty");

        tresult.info() << "u_u_join with itself\n";
        auto r1_src1 = src::of_container(src1) >> then::unordered_join(src::of_container(src1));

        tresult.assert_that<eq_unordered_sets>(r1_src1, src1, "identity");

        tresult.info() << "ordered_join without intersection\n";
        test_container_t src2 = { "1", "-1"};
        
        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src1) >> then::unordered_join (src::of_container(src2))
            , empty, "left bias no-ordered_join (1)");

        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src2) >> then::unordered_join (src::of_container(src1))
            , empty, "left bias no-ordered_join (2)");

        src2 = { "ge"s, "abc"s };

        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src1) >> then::unordered_join( src::of_container(src2))
            , empty, "mix no-ordered_join (1)");

        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src2) >> then::unordered_join(src::of_container(src1))
            , empty, "mix no-ordered_join (2)");

        src2 = {"x"s, "z"s, "y"s};
        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src1) >> then::unordered_join(src::of_container(src2))
            , empty, "right bias no-ordered_join (1)");

        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src2) >> then::unordered_join(src::of_container(src1))
            , empty, "right bias no-ordered_join (2)");

        //tresult.info() << "Comparator unordered_join set\n";
        //src2 = { {"0", 0}, {"1", 0.1}, {"AB", 0}, {"B", 0}, {"cD", 0}, {"cddddddddddddddddddddd", 0} };
        //test_container_t jn_res{ {"ab",1.0}, {"b", 1.0}, {"cd", 1.0} };
        //auto low_case_cmp = [](char a, char b)
        //    {
        //        return std::tolower(static_cast<unsigned char>(a)) ==
        //            std::tolower(static_cast<unsigned char>(b));
        //    };
        //auto no_case_compare =
        //    [&](const auto& l, const auto& r) {

        //    return l.first.size() == r.first.size() &&
        //        std::equal(l.first.begin(), l.first.end(), r.first.begin(), low_case_cmp);
        //};

        //auto join_res_factory = src::of_container(src1)
        //    >> then::unordered_join(src::of_container(src2), no_case_compare);

        //for (const auto& x : join_res_factory.compound())
        //    tresult.debug() << ">" << x.first << ", " << x.second << "\n";
        //tresult.assert_that<eq_sets>(join_res_factory, jn_res);
        //tresult.assert_that<eq_sets>(
        //    src::of_container(src1) 
        //        >> then::ordered_join(src::of_container(src2), key_only_compare),
        //    jn_res, "part intersect ordered_join (1)");
        //auto take_key_only = [](const auto& pair) {return pair.first; };
        //tresult.assert_that<eq_sets>(
        //    src::of_container(src2) 
        //        >> then::ordered_join( src::of_container(src1), key_only_compare)
        //        >> then::mapping(take_key_only), 
        //    src::of_container(jn_res) >> then::mapping(take_key_only),
        //        "part intersect ordered_join (2)");


        src2 = { "defx", "g", "k", "xyz", "zzzzzz" };
        test_container_t jn_res = {"g"s, "xyz"s};
        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src1) >> then::unordered_join(src::of_container(src2)),
            jn_res, "fail on right bias ordered_join(1)");

        tresult.assert_that<eq_unordered_sets>(
            src::of_container(src2) 
            >> then::unordered_join(src::of_container(src1)),
            jn_res, 
            "fail on right bias ordered_join(2)");


        tresult.info() << "unordered_join with duplicates\n";
        //
        //  PAY ATTENTION! Ordered set used for unordered-check
        //
        using test_multiset_container_t = std::multiset<std::string>;
        test_multiset_container_t multi_src1{
            "a"s,       
            "ab"s,      
            "ab"s,      
            "b"s,       
            "ba"s,      
            "ba"s,      
            "mmxxx"s,   
            "xyz"s     
        };

        test_multiset_container_t multi_src2{
            "a",  
            "a",  
            "ab", 
            "ba", 
            "ba", 
            "mz", 
            "xyz",
            "xyz"
        };

        auto res2 = src::of_container(multi_src1) 
            >> then::unordered_join( src::of_container(multi_src2) );
        //std::for_each(res2, [&](const auto& i) {
        //    tresult.debug() << "{" << i<<"}\n";
        //    });
        
        tresult.assert_that<eq_unordered_sets>(
            res2, std::array{
                "a"s, "ab"s, "ab"s, "ba"s, "ba"s, "xyz"s });
         

    }

    static auto& module_suite = OP::utest::default_test_suite("flur.unordered_join")
        .declare("u-u", test_u_u_join)
        ;
}//ns: empty
