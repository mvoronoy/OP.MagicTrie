
#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/ranges/IteratorsRange.h>
#include <op/ranges/JoinRange.h>
#include <op/ranges/RangeUtils.h>
#include <op/ranges/SingletonRange.h>

#include <map>
#include <string>
#include <utility>
#include <unordered_set>
#include <algorithm>
#include <locale>
#include "../AtomStrLiteral.h"

using namespace OP::utest;

struct lex_less
{
    bool operator ()(const std::string& left, const std::string& right) const
    {
        return std::lexicographical_compare(left.cbegin(), left.cend(), right.cbegin(), right.cend());
    }
};
typedef std::map<std::string, double, lex_less> test_container_t;
typedef std::multimap<std::string, double, lex_less> test_multimap_container_t;

template<class Left, class Right>
struct lexic_comparator_functor
{
    int operator()(const Left& left, const Right& right)const {
        auto&& left_prefix = left->first; //may be return by const-ref or by value
        auto&& right_prefix = right->first;//may be return by const-ref or by value
        return OP::ranges::str_lexico_comparator(left_prefix.begin(), left_prefix.end(),
            right_prefix.begin(), right_prefix.end());
    };
};

void test_RangeJoin(OP::utest::TestRuntime& tresult)
{

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("c", 1.0);
    src1.emplace("cd", 1.0);
    src1.emplace("d", 1.0);
    src1.emplace("def", 1.0);
    src1.emplace("g", 1.0);
    src1.emplace("xyz", 1.0);

    tresult.info() << "join with itself\n";
    auto r1_src1 = OP::ranges::make_range_of_map(src1),
        r2_src1 = OP::ranges::make_range_of_map(src1);
    //
    //auto res_src1 = r1_src1.join(r2_src1, [](const decltype(r1_src1)::iterator& left, const decltype(r2_src1)::iterator& right)->int {
    //        auto&&left_prefix = left->first; //may be return by const-ref or by value
    //        auto&&right_prefix = right->first;//may be return by const-ref or by value
    //        return OP::trie::str_lexico_comparator(left_prefix.begin(), left_prefix.end(), 
    //            right_prefix.begin(), right_prefix.end());
    //}
    //);

    //[+++++]auto res_src1 = r1_src1->join(r2_src1, lexic_comparator_functor<decltype(r1_src1)::element_type::iterator, decltype(r2_src1)::element_type::iterator>());
    auto res_src1 = r1_src1->join(r2_src1);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*res_src1, src1));
    //r1_src1.for_each([](const decltype(r1_src1)::iterator& i) {
    //    std::cout << i->first << '\n';
    //});
    tresult.info() << "join without intersection\n";
    test_container_t src2 = { {"0", 0}, {"1", 0.1} };
    auto r_src2 = OP::ranges::make_range_of_map(src2);
    res_src1 = r1_src1->join(r_src2);
    tresult.assert_true(res_src1->empty());
    src2 = { {"abc", 0}, {"ge", 0.1} };
    res_src1 = r1_src1->join(r_src2);
    tresult.assert_true(res_src1->empty());
    src2 = { {"x", 0}, {"y", 0.1}, {"z", 0.1} };
    res_src1 = r1_src1->join(r_src2);
    tresult.assert_true(res_src1->empty());

    tresult.info() << "Partial join set\n";
    src2 = { {"0", 0}, {"1", 0.1}, {"ab", 0}, {"b", 0}, {"cd", 0}, {"cddddddddddddddddddddd", 0} };
    res_src1 = r1_src1->join(r_src2);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*res_src1,
        test_container_t{ {"ab",1.0}, {"b", 1.0}, {"cd", 1.0} }));
    src2 = { {"defx", 0}, {"g", 0}, {"k", 0}, {"xyz", 0}, {"zzzzzz", 0} };
    res_src1 = r1_src1->join(r_src2);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*res_src1,
        test_container_t{ {"g",1.0}, {"xyz", 1.0} }));


    tresult.info() << "join with dupplicates\n";
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
    auto r_mul_src1 = OP::ranges::make_range_of_map(multi_src1);
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
    auto r_mul_src2 = OP::ranges::make_range_of_map(multi_src2);

    auto res2 = r_mul_src1->join(r_mul_src2);
    //res2->for_each([&](const auto& i) {
    //    tresult.debug() << "{" << (const char*)i.key().c_str() << " = " << i.value() << "}\n";
    //    });

    tresult.assert_true(OP::ranges::utils::range_map_equals(*res2,
        test_multimap_container_t{
            {"a", 1},
            {"ab", 1},
            {"ba", 1},
            {"ba", 1.1},
            {"xyz", 1} 
        }));

    tresult.info() << "if-exists semantic\n";
    multi_src2 = {{"0", 2}, {"a", 2}, {"a", 2.1}, {"b",2.0}, {"m",2.0},  {"z",2}};

    auto res3 = r_mul_src1->if_exists( r_mul_src2, [](const auto&left, const auto&right){
        //compare 1 letter only
        return static_cast<int>(left[0]) - static_cast<int>(right[0]);
        });
    /*res3->for_each([&](const auto& i) {
        tresult.debug() << "{" << (const char*)i.key().c_str() << " = " << i.value() << "}\n";
        });*/
    tresult.assert_true(OP::ranges::utils::range_map_equals(*res3,
        test_multimap_container_t{ 
            {"a", 1},
            {"ab",  1},
            {"ab",  1.1},
            {"b",  1},
            {"ba",  1},
            {"ba",  1.1},
            {"mmxxx",  1.1}}));
}
void test_ApplyFncRange(OP::utest::TestRuntime& tresult)
{
    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bcd", 1.0);
    src1.emplace("c", 1.0);
    src1.emplace("cd", 1.0);
    src1.emplace("d", 1.0);
    src1.emplace("def", 1.0);
    src1.emplace("g", 1.0);
    src1.emplace("xyz", 1.0);

    auto r1_src1 = OP::ranges::make_range_of_map(src1),
        r2_src1 = OP::ranges::make_range_of_map(src1);
    auto msrc1 = r1_src1->map([](const decltype(r1_src1)::element_type::iterator& i)->char {
        return i.key()[0];
        });
    /*??????????
    static_assert(!std::is_reference<decltype(msrc1->begin().key())>::value,
        "return type of iterator must be non-refernce");
        */
    std::string check;

    std::transform(
        msrc1->begin(), msrc1->end(),
        std::back_inserter(check),
        [](const auto& pair) {
            return static_cast<char>((int)pair.first);
        });
    tresult.assert_that<equals>(check, "aabbccddgx", "map error");
    using namespace OP::ranges;
    auto msrc2 = r1_src1->map([](const decltype(r1_src1)::element_type::iterator& i)-> size_t {return i.key().size();});
    std::vector<size_t> check2;
    std::transform(
        msrc2->begin(), msrc2->end(),
        std::back_inserter(check2),
        [](const auto& pair) {return pair.first;});

    std::vector<size_t> sample2{ 1,2,1,3,1,2,1,3,1,3 };
    tresult.assert_that<equals>(check2, sample2, "map error");

}
static const std::unordered_set<char> sylable_set({ 'a', 'e', 'i', 'o', 'u', 'y' });
template <class Container>
bool _filter_sylable(const typename Container::iterator& it)
{
    using namespace OP::ranges;
    return sylable_set.find(it.key()[0]) != sylable_set.end();
}
void test_FilterRange(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "test empty set\n";
    test_container_t src0;

    auto r_src0 = OP::ranges::make_range_of_map(src0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r_src0->filter(_filter_sylable<decltype(r_src0)::element_type>), src0));

    test_container_t src0_1;
    src0_1.emplace("a", 1.0);
    src0_1.emplace("u", 2.0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r_src0->filter(_filter_sylable<decltype(r_src0)::element_type>), src0));

    tresult.info() << "test basic filtering\n";

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("c", 1.0);
    src1.emplace("cd", 1.0);
    src1.emplace("d", 1.0);
    src1.emplace("def", 1.0);
    src1.emplace("g", 1.0);
    src1.emplace("xyz", 1.0);

    test_container_t  strain1;
    //find only letters that start from `sylable_set`
    std::copy_if(src1.begin(), src1.end(), std::inserter(strain1, strain1.begin()), [](const auto& pair) {
        return sylable_set.find(pair.first[0]) != sylable_set.end();
        });

    auto r1_src1 = OP::ranges::make_range_of_map(src1);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r1_src1->filter(_filter_sylable<decltype(r1_src1)::element_type>), strain1));

    src1.emplace("edf", 1.0);
    strain1.emplace("edf", 1.0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r1_src1->filter(_filter_sylable<decltype(r1_src1)::element_type>), strain1));
}

void test_UnionAllRange(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "test empty set\n";
    test_container_t src_empty;

    auto r_src0 = OP::ranges::make_range_of_map(src_empty),
        r_src0_1 = OP::ranges::make_range_of_map(src_empty);
    auto u1r = r_src0->merge_all(r_src0_1);
    //auto u1r = r_src0.join(r_src0_1, lexic_comparator_functor<decltype(r_src0)::iterator, decltype(r_src0_1)::iterator>());

    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1r, src_empty));

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    auto r_src1 = OP::ranges::make_range_of_map(src1);

    auto u2_left_empty = r_src0->merge_all(r_src1);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u2_left_empty, src1), OP_CODE_DETAILS(<< "Union-all empty-left"));

    auto u2_right_empty = r_src1->merge_all(r_src0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u2_right_empty, src1), OP_CODE_DETAILS(<< "Union-all empty-right"));

    test_container_t src2;
    src2.emplace("a", 2.0);
    src2.emplace("abc", 2.0);
    src2.emplace("bb", 2.0);
    src2.emplace("bc", 2.0);
    auto r_src2 = OP::ranges::make_range_of_map(src2);

    test_multimap_container_t strain1(src1.begin(), src1.end());
    strain1.insert(src2.begin(), src2.end());

    auto u1_2 = r_src1->merge_all(r_src2);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1_2, strain1), OP_CODE_DETAILS(<< "Union-all with intersection"));

    auto u2_1 = r_src2->merge_all(r_src1);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u2_1, strain1), OP_CODE_DETAILS(<< "Union-all with intersection"));

    test_container_t src3;
    src3.emplace("X", 3.0);
    src3.emplace("YZ", 3.0);
    src3.emplace("ZZ", 3.0);
    auto r_src3 = OP::ranges::make_range_of_map(src3);

    test_container_t  strain2(src1.begin(), src1.end());
    strain2.insert(src3.begin(), src3.end());

    auto u1_3 = r_src1->merge_all(r_src3);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1_3, strain2), OP_CODE_DETAILS(<< "Union-all no intersection"));

    auto u1_3_1 = r_src3->merge_all(r_src1);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1_3_1, strain2), OP_CODE_DETAILS(<< "Union-all no intersection"));
}


void test_FirstThat(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "test empty set\n";
    test_container_t src_empty;

    auto r_src0 = OP::ranges::make_range_of_map(src_empty);

    tresult.assert_true(OP::ranges::utils::range_map_equals(*r_src0, src_empty));

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("xyz", 1.0);

    auto r_src1 = OP::ranges::make_range_of_map(src1);

    tresult.assert_that<equals>(
        r_src1->first_that([](const auto& i) -> bool {
            return i.key()[0] > 'a';
            }).key(),
                "b",
                OP_CODE_DETAILS(<< "First-that fails location")
                );
    //test untill the end
    auto i = r_src1->first_that([](const auto&) -> bool {
        return false;
        });
    tresult.assert_false(r_src1->in_range(i),
        OP_CODE_DETAILS(<< "First-that over last fails location")
    );
    //test range
    tresult.assert_that<equals>(
        r_src1->first_that([](const auto&) -> bool {
            return true;
            }).key(),
                "a",
                OP_CODE_DETAILS(<< "First-that fails on the first")
                );
}

template <typename T>
class has_lower_bound
{
    typedef char one;
    struct two { char x[2]; };

    template <typename C> static one test(decltype(&C::lower_bound));
    template <typename C> static two test(...);

public:
    enum { value = sizeof(test<T>(0)) == sizeof(char) };
};

template <class Container, class Key>
static void eval_lower_bound(OP::utest::TestRuntime& tresult, const Container& co, const Key& not_exists, const Key& exact, const Key& lower)
{
    const auto& k1 = co.begin().key();
    auto r1 = co.lower_bound(k1);
    tresult.assert_that<equals>(r1.key(), k1, OP_CODE_DETAILS(<< "lower_bound of begin()"));

    auto r2 = co.lower_bound(not_exists);
    tresult.assert_false(co.in_range(r2), OP_CODE_DETAILS(<< "lower_bound of not_exists"));

    auto r3 = co.lower_bound(exact);
    tresult.assert_true(co.in_range(r3), OP_CODE_DETAILS(<< "in-range lower_bound of exact"));
    tresult.assert_that<equals>(exact, r3.key(), OP_CODE_DETAILS(<< "lower_bound of exact"));

    auto r4 = co.lower_bound(lower);
    tresult.assert_true(co.in_range(r4), OP_CODE_DETAILS(<< "in-range lower_bound of lower"));
    tresult.assert_that<less>(lower, r4.key(), OP_CODE_DETAILS(<< "lower_bound of lower"));
}

void test_LowerBoundAllRanges(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "apply lower_bound on container without native support\n";

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("c", 1.0);
    src1.emplace("cd", 1.0);
    src1.emplace("d", 1.0);
    src1.emplace("def", 1.0);
    src1.emplace("g", 1.0);
    src1.emplace("xyz", 1.0);

    using namespace std::string_literals;

    auto r1_src1 = OP::ranges::make_range_of_map(src1);
    eval_lower_bound(tresult, *r1_src1, "xyzz"s, "def"s, "cda"s);

    auto r_src2 = OP::ranges::make_range_of_map(src1);
    auto r_join = r1_src1->join(r_src2); //with itself
    eval_lower_bound(tresult, *r1_src1, "xyzz"s, "def"s, "cda"s);

    auto r_filtered = r1_src1->filter([](const auto& i) {
        const auto& s = i.key();
        return s != "def"s && s != "xyz"s;
        });

    eval_lower_bound(tresult, *r_filtered, "xyzz"s, "cd"s, "def"s);

}

void test_LowerBound(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "apply lower_bound on container without native support\n";

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("c", 1.0);
    src1.emplace("cd", 1.0);
    src1.emplace("d", 1.0);
    src1.emplace("def", 1.0);
    src1.emplace("g", 1.0);
    src1.emplace("xyz", 1.0);

    auto r1_src1 = OP::ranges::make_range_of_map(src1);

    using namespace OP::ranges;
    std::locale loc;
    //map-range doesn't support lower_bound so check that lower_bound still works (meaning O(n) algorithm)
    auto msrc = r1_src1->map([&](const decltype(r1_src1)::element_type::iterator& i)-> std::string {
        //capitalize
        auto k = i.key();
        for (auto& c : k) c = std::toupper(c, loc);
        return k;
        });
    auto flt_src = msrc->filter([](auto it) { return it.key().length() > 1/*peek long enough*/; });

    static_assert(!has_lower_bound< decltype(*flt_src.get()) >::value, "Should not expose lower_bound");

    ///
    /// Now do the same for container that supports lower_bound natively
    ///

    auto filtered_range2 = r1_src1
        ->filter([](const auto& it) -> bool {

        return it.key().length() > 1/*peek long enough*/;
            });
    auto found2 = filtered_range2
        ->lower_bound("t"); //pretty sure 't' not exists so correct answer is (int)'x'
    tresult.assert_true(filtered_range2->in_range(found2), OP_CODE_DETAILS(<< "end of the range is wrong"));

    tresult.assert_that<equals>(
        found2.key(),
        "xyz",
        OP_CODE_DETAILS(<< "lower_bound must point 'xyz'")
        );

}
void test_singletonRange(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "apply lower_bound on container without native support\n";

    auto r1 = OP::ranges::make_singleton_range("b"_astr, 1.);
    auto r1_it = r1->begin();
    tresult.assert_true(r1->in_range(r1_it), OP_CODE_DETAILS(<< "end of the range is wrong"));
    tresult.assert_that<equals>(
        r1_it.key(),
        "b"_astr,
        OP_CODE_DETAILS(<< "begin must point 'b'")
        );
    tresult.assert_that<equals>(
        r1_it.value(),
        1.0,
        OP_CODE_DETAILS(<< "begin must point 'b'/1.0")
        );
    auto r1_end = r1->lower_bound("c"_astr);
    tresult.assert_false(r1->in_range(r1_end), OP_CODE_DETAILS(<< "iterator must be at the end"));

    auto r1_end2 = r1->lower_bound("bb"_astr);
    tresult.assert_false(r1->in_range(r1_end2), OP_CODE_DETAILS(<< "iterator must be at the end"));

    auto r1_lb = r1->lower_bound("a"_astr);
    tresult.assert_true(r1->in_range(r1_lb), OP_CODE_DETAILS(<< "iterator must be in the range"));
    tresult.assert_that<equals>(
        r1_lb.key(),
        "b"_astr,
        OP_CODE_DETAILS(<< "lower_bound must point 'b'/1.0")
        );
    auto r1_lb_eq = r1->lower_bound("b"_astr);
    tresult.assert_true(r1->in_range(r1_lb_eq), OP_CODE_DETAILS(<< "iterator must be in the range"));
    tresult.assert_that<equals>(
        r1_lb_eq.key(),
        "b"_astr,
        OP_CODE_DETAILS(<< "lower_bound must point 'b'/1.0")
        );
}

static void test_FlattenRange(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "flatten range empty\n";

    test_container_t src1;
    auto r1_src1 = OP::ranges::make_range_of_map(src1);
    auto flatten_range1 = r1_src1->flatten([](const auto& i) {
        return OP::ranges::make_singleton_range("x"_astr, 1.);
        });
    tresult.assert_false(flatten_range1->in_range(flatten_range1->begin()), OP_CODE_DETAILS(<< "Empty must generate empty flatten"));

    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("c", 1.0);
    src1.emplace("cd", 1.0);
    src1.emplace("d", 1.0);
    src1.emplace("def", 1.0);
    src1.emplace("g", 1.0);
    src1.emplace("xyz", 1.0);

    tresult.info() << "Test flatten range lazynes...\n";
    tresult.assert_that<equals>(flatten_range1->count(), 1, OP_CODE_DETAILS(<< "Singleton result must produce exact 1 row"));
    tresult.assert_that<equals>(flatten_range1->begin().key(), "x"_astr, OP_CODE_DETAILS(<< "Singleton produce 'x' fail"));

    auto flatten_range2 = r1_src1->flatten([](const auto& i) {
        std::map<std::string, double> result_map = {
            {i.key() + "aa", i.value() + 0.1},
            {i.key() + "bb", i.value() + 0.2},
            {i.key() + "c", i.value() + 0.3},
        };
        return OP::ranges::make_range_of_map(std::move(result_map));
        });
    //flatten_range2->for_each([&](const auto& i)
    //    {
    //       tresult.debug() << "f:{" << i.key() << "=" << i.value() << "}\n";
    //    });
    tresult.assert_that<equals>(flatten_range2->count(), src1.size() * 3, OP_CODE_DETAILS(<< "Wrong count"));
    tresult.assert_that<equals>(flatten_range2->begin().key(), "aaa", OP_CODE_DETAILS(<< "Wrong first element"));
    tresult.assert_that<equals>(std::next(flatten_range2->begin()).key(), "abaa", OP_CODE_DETAILS(<< "Wrong second element"));
    tresult.assert_that<equals>(std::next(flatten_range2->begin(), 2).key(), "abb", OP_CODE_DETAILS(<< "Wrong third element"));
    tresult.assert_that<equals>(std::next(flatten_range2->begin(), 3).key(), "abbb", OP_CODE_DETAILS(<< "Wrong forth element"));

    tresult.assert_that<equals>(flatten_range2->lower_bound("ax").key(), "baa", OP_CODE_DETAILS(<< "Wrong lower_bound first element"));
    tresult.assert_that<equals>(flatten_range2->lower_bound("m").key(), "xyzaa", OP_CODE_DETAILS(<< "Wrong lower_bound last element"));
    tresult.assert_that<equals>(flatten_range2->lower_bound("xyzbb").key(), "xyzbb", OP_CODE_DETAILS(<< "Wrong lower_bound last element"));
    tresult.assert_false(flatten_range2->in_range(flatten_range2->lower_bound("z")), OP_CODE_DETAILS(<< "Wrong lower_bound over the end()"));

    auto flatten_range3 = r1_src1->flatten([](const auto&) {
        return OP::ranges::make_empty_range<std::string, double>();
        });
    tresult.assert_false(flatten_range3->in_range(flatten_range3->begin()), OP_CODE_DETAILS(<< "Empty flatten must generate empty flatten"));
}

static void test_DistinctRange(OP::utest::TestRuntime& tresult)
{
    tresult.info() << "Test distinct range empty\n";

    test_multimap_container_t src1;
    auto r1_src1 = OP::ranges::make_range_of_map(src1);
    auto distinct_range1 = r1_src1->distinct();
    tresult.assert_true(distinct_range1->empty(), OP_CODE_DETAILS(<< "Empty must generate empty flatten"));

    src1 = { {"a", 1.0} };
    tresult.assert_true(OP::ranges::utils::map_equals(*distinct_range1, src1),
        OP_CODE_DETAILS(<< "1:1 distinct failed"));

    src1 = { {"a", 1.0}, {"a", 2.0} };
    //check that we have 2 same keys
    tresult.assert_true(OP::ranges::utils::map_equals(*r1_src1, src1),
        OP_CODE_DETAILS(<< "Must have 2 same keys"));

    test_container_t src_de_dup{ src1.begin(), src1.end() };
    tresult.assert_true(OP::ranges::utils::map_equals(*distinct_range1, src_de_dup),
        OP_CODE_DETAILS(<< "Distinct must eliminate dupplicates"));
    //check on unique map all values are consistent
    test_container_t bulk_uniq_map;
    std::generate_n(
        std::inserter(bulk_uniq_map, bulk_uniq_map.begin()),
        101,
        []() { return std::make_pair(OP::utest::tools::random<std::string>(), 11); }
    );
    auto distinct2 = OP::ranges::make_range_of_map(bulk_uniq_map)->distinct();
    tresult.assert_true(OP::ranges::utils::map_equals(*distinct2, bulk_uniq_map),
        OP_CODE_DETAILS(<< "101 error"));
    //check multi-map with big dupplicates
    test_multimap_container_t src2;
    for (unsigned i = 0; i < 10; ++i)
        for (unsigned j = 0; j < 10; ++j)
        {   //create 10 duplicates
            src2.emplace(std::make_pair(OP::utest::tools::random<std::string>(), j));
        }
    auto r2_src2 = OP::ranges::make_range_of_map(src2);
    //check that we have (10 same keys)*10
    tresult.assert_true(OP::ranges::utils::map_equals(*r2_src2, src2),
        OP_CODE_DETAILS(<< "Must have 2 same keys"));
    src_de_dup = { src2.begin(), src2.end() };
    tresult.assert_true(OP::ranges::utils::map_equals(*r2_src2->distinct(), src_de_dup),
        OP_CODE_DETAILS(<< "Distinct must eliminate dupplicates on 10*10"));

}
static auto& module_suite = OP::utest::default_test_suite("Ranges")
.declare("join", test_RangeJoin)
.declare("fnc", test_ApplyFncRange)
.declare("filter", test_FilterRange)
.declare("union-all", test_UnionAllRange)
.declare("first-that", test_FirstThat)
.declare("lower_bound-base", test_LowerBound)
.declare("lower_bound-on-containers", test_LowerBoundAllRanges)
.declare("singleton-range", test_singletonRange)
.declare("flatten", test_FlattenRange)
.declare("distinct", test_DistinctRange)

;
