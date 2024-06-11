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
            "a", "a",  
            "ab", 
            "ba", 
            "ba", 
            "mz", 
            "xyz", "xyz"
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

    void test_adaptive_join(TestRuntime& tresult)
    {
        using test_container_t = std::vector<std::string>;
        test_container_t src1{
            "a"s, "ab"s, "b"s, "bc"s, "c"s, "cd"s,
            "d"s, "def"s, "g"s, "xyz"s
        };
        std::shuffle(src1.begin(), src1.end(), 
            tools::RandomGenerator::instance().generator());
        auto right = src::of_iota('a', static_cast<char>('z' + 1)) //build 1 char string ordered sequence
            >> then::keep_order_maf_cv([](char c, std::string& buf) { buf.clear(); buf.append(1, c); return true;  })
            ;
        std::set expected{
            "a"s, "b"s, "c"s, "d"s, "g"s
        };

        auto r_u_o = //result unordered & ordered
            src::of_container(std::cref(src1))
            >> then::auto_join(right)
            ;
        tresult.assert_true(r_u_o.compound().is_sequence_ordered(), 
            "sequence must be ordered because 'right' is ordered");

        tresult.assert_that<eq_sets>(expected, r_u_o,
            "wrong auto-join result for u+o"
        );

        auto r_o_u = //result ordered & unordered
            right >>
            then::auto_join(src::of_container(std::cref(src1)))
            ;
        tresult.assert_true(r_o_u.compound().is_sequence_ordered(),
            "sequence must be ordered because 'right' is ordered");

        tresult.assert_that<eq_sets>(expected, r_o_u,
            "wrong auto-join result for o+u"
        );

//////////
        auto r_o_o = //result ordered & ordered
            src::of_container(std::set(src1.begin(), src1.end())) 
            >> then::auto_join(right);
        tresult.assert_true(r_o_o.compound().is_sequence_ordered());
        tresult.assert_that<eq_sets>(expected, r_o_o,
            "wrong auto-join result for u+o"
        );

        //// test u+u
        auto r_u_u = //result unordered & unordered
            src::of_container(std::cref(src1))
            >> then::auto_join(
                src::of_container(std::cref(src1)) //filter 1 letter only strings
                >> then::filter([](const std::string& a)->bool {return a.length() == 1; })
            )
            ;

        tresult.assert_false(r_u_u.compound().is_sequence_ordered(), "result must be unordered");
        tresult.assert_that<eq_unordered_sets>(expected, r_u_u,
            "wrong auto-join result for u+u"
        );

        //next test very similar to previous r_u_u, but allows ensure `compound()&&` invoked
        auto seq = (src::of_container(std::cref(src1))
            >> then::auto_join(
                src::of_container(std::cref(src1)) //filter 1 letter only strings
                >> then::filter([](const std::string& a)->bool {return a.length() == 1; })
            )).compound();

        size_t n = 0;
        for (const auto& a : seq)
        {
            tresult.assert_true(expected.find(a) != expected.end(),
                "wrong auto-join result for u+u"
            );
            ++n;
        }
        tresult.assert_that<equals>(n, expected.size());
    }
    
    static auto& module_suite = OP::utest::default_test_suite("flur.unordered_join")
        .declare("u-u", test_u_u_join)
        .declare("adaptive", test_adaptive_join)
        ;
}//ns: empty
