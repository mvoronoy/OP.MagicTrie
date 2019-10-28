
#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <op/ranges/IteratorsRange.h>
#include <op/ranges/JoinRange.h>
#include <op/ranges/RangeUtils.h>
#include <map>
#include <string>
#include <utility>
#include <unordered_set>
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
        auto&&left_prefix = left->first; //may be return by const-ref or by value
        auto&&right_prefix = right->first;//may be return by const-ref or by value
        return OP::ranges::str_lexico_comparator(left_prefix.begin(), left_prefix.end(),
            right_prefix.begin(), right_prefix.end());
    };
};

void test_RangeJoin(OP::utest::TestResult &tresult)
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

    tresult.status_details().as_stream() << "join with itself\n";
    auto r1_src1 = OP::ranges::make_iterators_range(src1),
        r2_src1 = OP::ranges::make_iterators_range(src1);
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
}
void test_ApplyFncRange(OP::utest::TestResult &tresult)
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

    auto r1_src1 = OP::ranges::make_iterators_range(src1),
        r2_src1 = OP::ranges::make_iterators_range(src1);
    auto msrc1 = r1_src1->map([](const auto& i)->int {return (int)i.key()[0];});
    static_assert(!std::is_reference<decltype(msrc1->begin().key())>::value,
        "return type of iterator must be non-refernce");
    using namespace OP::ranges;
    auto msrc2 = r1_src1->map<policy::cached>([](const auto& i)-> int{return (int)key_discovery::key(i)[0];});
    static_assert(std::is_reference<decltype(msrc2->begin().key())>::value,
        "return type of iterator must be const refernce");
    //
    
}
static const std::unordered_set<char> sylable_set({ 'a', 'e', 'i', 'o', 'u', 'y' });
template <class Container>
bool _filter_sylable(const typename Container::iterator &it)
{
    using namespace OP::ranges;
    return sylable_set.find(key_discovery::key(it)[0]) != sylable_set.end();
}
void test_FilterRange(OP::utest::TestResult &tresult)
{
    tresult.status_details().as_stream() << "test empty set\n";
    test_container_t src0;
    
    auto r_src0 = OP::ranges::make_iterators_range(src0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r_src0->filter(_filter_sylable<decltype(r_src0)::element_type>), src0));

    test_container_t src0_1;
    src0_1.emplace("a", 1.0);
    src0_1.emplace("u", 2.0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r_src0->filter(_filter_sylable<decltype(r_src0)::element_type>), src0));

    tresult.status_details().as_stream() << "test basic filtering\n";

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

    auto r1_src1 = OP::ranges::make_iterators_range(src1);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r1_src1->filter(_filter_sylable<decltype(r1_src1)::element_type>), strain1));

    src1.emplace("edf", 1.0);
    strain1.emplace("edf", 1.0);
    tresult.assert_true(OP::ranges::utils::range_map_equals(*r1_src1->filter(_filter_sylable<decltype(r1_src1)::element_type>), strain1));
}

void test_UnionAllRange(OP::utest::TestResult &tresult)
{
    tresult.status_details().as_stream() << "test empty set\n";
    test_container_t src_empty;

    auto r_src0 = OP::ranges::make_iterators_range(src_empty),
        r_src0_1 = OP::ranges::make_iterators_range(src_empty);
    auto u1r = r_src0->merge_all(r_src0_1, lexic_comparator_functor<decltype(r_src0)::element_type::iterator, decltype(r_src0_1)::element_type::iterator>());
    //auto u1r = r_src0.join(r_src0_1, lexic_comparator_functor<decltype(r_src0)::iterator, decltype(r_src0_1)::iterator>());

    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1r, src_empty));

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    auto r_src1 = OP::ranges::make_iterators_range(src1);

    auto u2_left_empty = r_src0->merge_all(r_src1, lexic_comparator_functor<decltype(r_src0)::element_type::iterator, decltype(r_src1)::element_type::iterator>());
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u2_left_empty, src1), OP_CODE_DETAILS(<<"Union-all empty-left"));

    auto u2_right_empty = r_src1->merge_all(r_src0, lexic_comparator_functor<decltype(r_src1)::element_type::iterator, decltype(r_src0)::element_type::iterator>());
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u2_right_empty, src1), OP_CODE_DETAILS(<< "Union-all empty-right"));

    test_container_t src2;
    src2.emplace("a", 2.0);
    src2.emplace("abc", 2.0);
    src2.emplace("bb", 2.0);
    src2.emplace("bc", 2.0);
    auto r_src2 = OP::ranges::make_iterators_range(src2);

    test_multimap_container_t  strain1(src1.begin(), src1.end());
    strain1.insert(src2.begin(), src2.end());

    auto u1_2 = r_src1->merge_all(r_src2, lexic_comparator_functor<decltype(r_src1)::element_type::iterator, decltype(r_src2)::element_type::iterator>());
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1_2, strain1), OP_CODE_DETAILS(<< "Union-all with intersection"));

    auto u2_1 = r_src2->merge_all(r_src1, lexic_comparator_functor<decltype(r_src2)::element_type::iterator, decltype(r_src1)::element_type::iterator>());
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u2_1, strain1), OP_CODE_DETAILS(<< "Union-all with intersection"));

    test_container_t src3;
    src3.emplace("X", 3.0);
    src3.emplace("YZ", 3.0);
    src3.emplace("ZZ", 3.0);
    auto r_src3 = OP::ranges::make_iterators_range(src3);

    test_container_t  strain2(src1.begin(), src1.end());
    strain2.insert(src3.begin(), src3.end());

    auto u1_3 = r_src1->merge_all(r_src3, lexic_comparator_functor<decltype(r_src1)::element_type::iterator, decltype(r_src3)::element_type::iterator>());
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1_3, strain2), OP_CODE_DETAILS(<< "Union-all no intersection"));

    auto u1_3_1 = r_src3->merge_all(r_src1, lexic_comparator_functor<decltype(r_src3)::element_type::iterator, decltype(r_src1)::element_type::iterator>());
    tresult.assert_true(OP::ranges::utils::range_map_equals(*u1_3_1, strain2), OP_CODE_DETAILS(<< "Union-all no intersection"));
}


void test_FirstThat(OP::utest::TestResult &tresult)
{
    tresult.status_details().as_stream() << "test empty set\n";
    test_container_t src_empty;

    auto r_src0 = OP::ranges::make_iterators_range(src_empty);

    tresult.assert_true(OP::ranges::utils::range_map_equals(*r_src0, src_empty));

    test_container_t src1;
    src1.emplace("a", 1.0);
    src1.emplace("ab", 1.0);
    src1.emplace("b", 1.0);
    src1.emplace("bc", 1.0);
    src1.emplace("xyz", 1.0);

    auto r_src1 = OP::ranges::make_iterators_range(src1);

    tresult.assert_that<equals>(
        r_src1->first_that([](const auto& i) -> bool {
            return OP::ranges::key_discovery::key(i)[0] > 'a';
        })->first,
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
        })->first,
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
static void eval_lower_bound(OP::utest::TestResult &tresult, const Container& co, const Key &not_exists, const Key& exact, const Key& lower)
{
    const auto& k1 = OP::ranges::key_discovery::key(co.begin());
    auto r1 = co.lower_bound(k1);
    tresult.assert_that<equals>(OP::ranges::key_discovery::key(r1), k1, OP_CODE_DETAILS(<< "lower_bound of begin()"));

    auto r2 = co.lower_bound(not_exists);
    tresult.assert_false(co.in_range(r2), OP_CODE_DETAILS(<< "lower_bound of not_exists"));

    auto r3 = co.lower_bound(exact);
    tresult.assert_true(co.in_range(r3), OP_CODE_DETAILS(<< "in-range lower_bound of exact"));
    tresult.assert_that<equals>(exact, OP::ranges::key_discovery::key(r3), OP_CODE_DETAILS(<< "lower_bound of exact"));

    auto r4 = co.lower_bound(lower);
    tresult.assert_true(co.in_range(r4), OP_CODE_DETAILS(<< "in-range lower_bound of lower"));
    tresult.assert_that<less>(lower, OP::ranges::key_discovery::key(r4), OP_CODE_DETAILS(<< "lower_bound of lower"));
}

void test_LowerBoundAllRanges(OP::utest::TestResult &tresult)
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

    auto r1_src1 = OP::ranges::make_iterators_range(src1);
    eval_lower_bound(tresult, *r1_src1, "xyzz"s, "def"s, "cda"s);

    auto r_src2 = OP::ranges::make_iterators_range(src1);
    auto r_join = r1_src1->join(r_src2); //with itself
    eval_lower_bound(tresult, *r1_src1, "xyzz"s, "def"s, "cda"s);

    auto r_filtered = r1_src1->filter([](const auto& i) {
        const auto& s = OP::ranges::key_discovery::key(i);
        return s != "def"s && s !="xyz"s; 
    }); 

    eval_lower_bound(tresult, *r_filtered, "xyzz"s, "cd"s, "def"s);

}

void test_LowerBound(OP::utest::TestResult &tresult)
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

    auto r1_src1 = OP::ranges::make_iterators_range(src1);
        
    using namespace OP::ranges;
    std::locale loc;
    //map-range doesn't support lower_bound so check that lower_bound still works (meaning O(n) algorithm)
    auto msrc = r1_src1->map<policy::cached>([&](const auto& i)-> std::string {
        //capitalize
        auto k = key_discovery::key(i);
        for (auto &c : k) c = std::toupper(c, loc);
        return k; 
    });
    auto flt_src = msrc->filter([](auto it) { return it.key().length() > 1/*peek long enough*/; });
    
    static_assert(!has_lower_bound< decltype(*flt_src.get()) >::value, "Should not expose lower_bound");

    ///
    /// Now do the same for container that supports lower_bound natively
    ///
    
    auto filtered_range2 = r1_src1
        ->filter([](const auto& it) -> bool {

        return OP::ranges::key_discovery::key(it).length() > 1/*peek long enough*/;
    });
    auto found2 = filtered_range2
        ->lower_bound("t"); //pretty sure 't' not exists so correct answer is (int)'x'
    tresult.assert_true(filtered_range2->in_range(found2), OP_CODE_DETAILS(<< "end of the range is wrong"));

    tresult.assert_that<equals>(
        key_discovery::key(found2),
        "xyz",
        OP_CODE_DETAILS(<< "lower_bound must point 'xyz'")
        );
    
}

static auto module_suite = OP::utest::default_test_suite("Ranges")
->declare(test_RangeJoin, "join")
->declare(test_ApplyFncRange, "fnc")
->declare(test_FilterRange, "filter")
->declare(test_UnionAllRange, "union-all")
->declare(test_FirstThat, "first-that")
->declare(test_LowerBound, "lower_bound-base")
->declare(test_LowerBoundAllRanges, "lower_bound-on-containers")

;
