#include <array>
#include <set>
#include <vector>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

using namespace OP::utest;
using namespace OP::flur;
using namespace std::string_literals;
namespace {

    //template <class Left, class Right, typename = 
    //    std::enable_if_t<Left::ordered_c && Right::ordered_c> >
    //decltype(auto) ordered_join(Left&& left, Right&& right)
    //{
    //    return 0;
    //}

    void test_Join(TestRuntime& tresult)
    {
        using test_container_t = std::map<std::string, double>;
        test_container_t src1 {
            {"a", 1.0},
        {"ab", 1.0},
        {"b", 1.0},
        {"bc", 1.0},
        {"c", 1.0},
        {"cd", 1.0},
        {"d", 1.0},
        {"def", 1.0},
        {"g", 1.0},
        {"xyz", 1.0} 
        };

        tresult.info() << "ordered_join with empty\n";
        test_container_t empty;
        tresult.assert_that<eq_sets>(
            src::of_container(src1) & src::of_container(empty), empty, "right empty");
        tresult.assert_that<eq_sets>(
            src::of_container(empty) & src::of_container(src1), empty, "left empty");

        tresult.info() << "ordered_join with itself\n";
        auto r1_src1 = src::of_container(src1) & src::of_container(src1);

        tresult.assert_that<eq_sets>(r1_src1, src1, "identity");

        tresult.info() << "ordered_join without intersection\n";
        test_container_t src2 = { {"0", 0}, {"1", 0.1} };
        
        tresult.assert_that<eq_sets>(
            src::of_container(src1) & src::of_container(src2)
            , empty, "left bias no-ordered_join (1)");

        tresult.assert_that<eq_sets>(
            src::of_container(src2) & src::of_container(src1)
            , empty, "left bias no-ordered_join (2)");

        src2 = { {"abc", 0}, {"ge", 0.1} };

        tresult.assert_that<eq_sets>(
            src::of_container(src1) & src::of_container(src2)
            , empty, "mix no-ordered_join (1)");

        tresult.assert_that<eq_sets>(
            src::of_container(src2) & src::of_container(src1)
            , empty, "mix no-ordered_join (2)");

        src2 = { {"x", 0}, {"y", 0.1}, {"z", 0.1} };
        tresult.assert_that<eq_sets>(
            src::of_container(src1) & src::of_container(src2)
            , empty, "right bias no-ordered_join (1)");

        tresult.assert_that<eq_sets>(
            src::of_container(src2) & src::of_container(src1)
            , empty, "right bias no-ordered_join (2)");

        tresult.info() << "Partial ordered_join set\n";
        src2 = { {"0", 0}, {"1", 0.1}, {"ab", 0}, {"b", 0}, {"cd", 0}, {"cddddddddddddddddddddd", 0} };
        test_container_t jn_res{ {"ab",1.0}, {"b", 1.0}, {"cd", 1.0} };
        auto key_only_compare =
            [](const auto& l, const auto& r) {
            return l.first.compare(r.first); 
        };

        auto join_res_factory = src::of_container(src1)
            >> then::ordered_join(src::of_container(src2), key_only_compare);
        for (const auto& x : join_res_factory.compound())
            tresult.debug() << ">" << x.first << ", " << x.second << "\n";
        tresult.assert_that<eq_sets>(join_res_factory, jn_res);

        tresult.assert_that<eq_sets>(
            src::of_container(src1) 
                >> then::ordered_join(src::of_container(src2), key_only_compare),
            jn_res, "part intersect ordered_join (1)");
        auto take_key_only = [](const auto& pair) {return pair.first; };
        tresult.assert_that<eq_sets>(
            src::of_container(src2) 
                >> then::ordered_join( src::of_container(src1), key_only_compare)
                >> then::mapping(take_key_only), 
            src::of_container(jn_res) >> then::mapping(take_key_only),
                "part intersect ordered_join (2)");


        src2 = { {"defx", 0}, {"g", 0}, {"k", 0}, {"xyz", 0}, {"zzzzzz", 0} };
        jn_res = { {"g",1.0}, {"xyz", 1.0} };
        tresult.assert_that<eq_sets>(
            src::of_container(src1) >> then::ordered_join(src::of_container(src2), key_only_compare),
            jn_res, "fail on right bias ordered_join(1)");

        tresult.assert_that<eq_sets>(
            src::of_container(src2) 
            >> then::ordered_join(src::of_container(src1), key_only_compare)
            >> then::mapping(take_key_only),
            src::of_container(jn_res) >> then::mapping(take_key_only), 
            "fail on right bias ordered_join(2)");


        tresult.info() << "ordered_join with dupplicates\n";
        using test_multimap_container_t = std::multimap<std::string, double>;
        test_multimap_container_t multi_src1{
            {"a", 1.0},
            {"ab", 1.0},
            {"ab", 1.1},
            {"b", 1.0},
            {"ba", 1.0},
            {"ba", 1.1},
            {"mmxxx", 1.1},
            {"xyz", 1.0},
        };

        test_multimap_container_t multi_src2{
            {"a", 2.0},
            {"a", 2.1},
            {"ab", 2.1},
            {"ba", 2.0},
            {"ba", 2.1},
            {"mz", 2.1},
            {"xyz", 2.0},
            {"xyz", 2.1},
        };

        auto res2 = src::of_container(multi_src1) 
            >> then::ordered_join( src::of_container(multi_src2), key_only_compare );
        std::for_each(res2, [&](const auto& i) {
            tresult.debug() << "{" << i.first << " = " << i.second << "}\n";
            });

        tresult.assert_that<eq_sets>(
            res2
            //>> then::mapping(take_key_only)
            ,
            src::of_container(test_multimap_container_t{
                {"a", 1},
                {"ab", 1},
                {"ab", 1.1},
                {"ba", 1},
                {"ba", 1.1},
                {"xyz", 1} 
                }) 
            //>> then::mapping(take_key_only)
            , 
            "fail on dupplicate ordered_join");

        //tresult.info() << "if-exists semantic\n";
        //multi_src2 = {{"0", 2}, {"a", 2}, {"a", 2.1}, {"b",2.0}, {"m",2.0},  {"z",2}};

        //auto res3 = r_mul_src1->if_exists( r_mul_src2, [](const auto&left, const auto&right){
        //    //compare 1 letter only
        //    return static_cast<int>(left[0]) - static_cast<int>(right[0]);
        //    });
        ///*res3->for_each([&](const auto& i) {
        //tresult.debug() << "{" << (const char*)i.key().c_str() << " = " << i.value() << "}\n";
        //});*/
        //tresult.assert_true(OP::ranges::utils::range_map_equals(*res3,
        //    test_multimap_container_t{ 
        //        {"a", 1},
        //    {"ab",  1},
        //    {"ab",  1.1},
        //    {"b",  1},
        //    {"ba",  1},
        //    {"ba",  1.1},
        //    {"mmxxx",  1.1}}));

    }
    void test_JoinSingle(TestRuntime& tresult)
    {
        using test_container_t = std::map<std::string, double>;
        test_container_t src1{
            {"a", 1.0},
            {"ab", 1.0},
            {"b", 1.0},
            {"bc", 1.0},
            {"c", 1.0},
            {"cd", 1.0},
            {"d", 1.0},
            {"def", 1.0},
            {"g", 1.0},
            {"xyz", 1.0}
            };

        std::map<std::string, double> expected{ {"def", 1.0} };
        tresult.assert_that<eq_sets>(
            src::of_container(src1) 
            >> then::ordered_join(src::of_value("def"s), 
                [](const auto& l, const auto& r)->int {
                    return l.first.compare(r);
                }), 
            expected,
            "single-item"
        );

    }

    void test_shared(TestRuntime& tresult)
    {
        tresult.info() << "join Polymorphs ...\n";
        ;
        auto r1_dat1 =
            make_shared(
                src::of_container(std::set{ 1, 2, 3 })
                ) &
            make_shared(src::of_value(2)) &
            make_shared(src::of_iota(100, 103))
            ;

        tresult.assert_that<eq_sets>(*r1_dat1, std::vector<int>{  }
        );

        auto r2 =
            make_shared(
                src::of_container(std::set{ 1, 2, 3 })
                ) &
            make_shared(src::of_value(3)) 
            ;

        tresult.assert_that<eq_sets>(*r2, std::vector{ 3 });
    }

    template <class Os, class TContainer>
    Os& debug_print(Os& os, const TContainer& cnt)
    {
        os << "{";
        size_t n = 0;
        for(decltype(auto) x: cnt)
            os << (n++?", ": "") << x;
        os << "}";
        return os;
    }

    void test_ordered_no_low_bound(TestRuntime& tresult)
    {
        constexpr int start_c = 5, end_c = 20;
        std::set<int> lb_set;
        for(int i = start_c; i < end_c; i+=3)
            lb_set.insert(i);    
        auto lb_container = src::of_container<Intrinsic::result_by_value>(std::cref(lb_set));
        auto test_lb_seq = lb_container.compound();
        static_assert(
            std::is_base_of_v<OrderedSequenceOptimizedJoin<int>, decltype(test_lb_seq)>, 
            "must use lower_bound as optimization"
            );
        auto no_lb_container_big = src::of_iota(0, end_c + 10);
        tresult.assert_true(
            no_lb_container_big.compound().is_sequence_ordered(), 
            "iota for monotone int must produce ordered seq");

        tresult.assert_that<eq_sets>(lb_container & no_lb_container_big, lb_set);
        tresult.assert_that<eq_sets>(no_lb_container_big & lb_container, lb_set);
        tresult.assert_that<eq_sets>(lb_container & lb_container , lb_set);
        tresult.assert_that<eq_sets>(lb_container & 
            (lb_container >> then::ordering_flat_mapping(
                [](int a){return src::of_iota(a, a+3);}
            )), 
            lb_set);


        auto no_lb_container_left = src::of_iota(0, end_c);
      
        debug_print(tresult.debug() << "UUT container:\n\t",  lb_container) << "\n";
        debug_print(tresult.debug() << "Exemplar container:\n\t",  no_lb_container_left) << "\n";
        debug_print(tresult.debug() << "Join container:\n\t",  lb_container & no_lb_container_left) << "\n";

        tresult.assert_that<eq_sets>(lb_container & no_lb_container_left, lb_set);
        tresult.assert_that<eq_sets>(no_lb_container_left & lb_container, lb_set);

        auto no_lb_container_right = src::of_iota(start_c, end_c+5);
        tresult.assert_that<eq_sets>(lb_container & no_lb_container_right, lb_set);
        tresult.assert_that<eq_sets>(no_lb_container_right & lb_container, lb_set);

        std::array<int, 1> unit_ordered_array{start_c};
        tresult.assert_that<eq_sets>(lb_container & src::of_container(unit_ordered_array), unit_ordered_array,
            "1 item array is an ordered container too");
    }

    static auto& module_suite = OP::utest::default_test_suite("flur.ordered_join")
        .declare("filter", test_Join)
        .declare("with-single", test_JoinSingle)
        .declare("shared", test_shared)
        .declare("ordered_no_low_bound", test_ordered_no_low_bound)
        ;
}//ns: empty
