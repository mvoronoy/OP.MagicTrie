#include <op/utest/unit_test.h>
#include <op/common/Range.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/SegmentHelper.h>
#include <assert.h>
#include <random>
#include <map>
#include <forward_list>
#include <op/common/Bitset.h>
#include <op/common/IoFlagGuard.h>
#include <op/flur/flur.h>
#include <op/flur/flur.h>
#include <op/common/BitboxRangeContainer.h>
#include <op/common/SpanContainer.h>


using namespace OP;
using namespace OP::vtm;
using namespace OP::trie;
using namespace OP::utest;

template <class TContainer, size_t NumberOfRanges = 10000>
void run_ZoneContainerTest(TestRuntime& tresult)
{
    using test_range_t = typename TContainer::key_t;
    /*   -->address-->
    *     01234567890123567890123456789
    *  t0| [..]   [...]
    *  t1|  [....]
    *  t2|   [.] [...]
    *  t3|      [] 
    *  t4|[]                  [...]
    */
    std::vector<test_range_t> init_list{
        /*t0*/{1, 4}, {8, 5}, 
        /*t1*/{2, 6}, 
        /*t2*/ {3, 3}, {7, 5}, 
        /*t3*/ {6, 1}, 
        /*t4*/ {0, 1}, {21, 150}
    };

    constexpr size_t test_dim_t = NumberOfRanges;
    constexpr size_t test_range_limit_t = 1000;
    using rcontainer_t = TContainer;

    auto& rnd_gen = tools::RandomGenerator::instance();
    auto rand_zone = [&]() -> test_range_t {
        auto pos = rnd_gen.next_in_range<std::uint32_t>(0, test_dim_t);
        auto dim = rnd_gen.next_in_range<std::uint32_t>(1, test_range_limit_t);
        return test_range_t(pos, dim);
    };
    
    std::vector< test_range_t> sample{ test_dim_t };
    //prepare array of random ranges
    for (auto& r : sample)
        r = rand_zone();
    // container of query->sets_which_overlap_query
    std::unordered_map<test_range_t, std::unordered_multiset< test_range_t>> expects;
    TContainer test_container;
    auto rand_init = [&]() {
        expects.clear();
        test_container.clear();
        for (const auto& ir : sample)
        { //populate test container with random ranges
            test_container.emplace(ir, typename TContainer::value_t{});
        }
        
        for (auto i = 0; i < test_dim_t/10; ++i)
        { //prepare set of probe intersections
            test_range_t r(rand_zone());
            auto [iter, succ] = expects.emplace(r, std::unordered_multiset< test_range_t>{});
            if (!succ)//such range already exists
                continue;
            //collect all items that has intersections over sample array
            for (const auto& check : sample)
            {
                if (check.is_overlapped(r))
                    iter->second.emplace(check);
            }
        }
    };

    auto run_test = [&](auto init_script) {
        init_script();
        for (auto test_i = expects.begin(); test_i != expects.end(); ++test_i)
        {
            test_container.intersect_with(test_i->first, [&](const auto& range, auto _) {
                auto found = test_i->second.find(range);

                tresult.assert_false(found == test_i->second.end(),
                    OP_CODE_DETAILS(
                            << "When expects == end() no entries for intersection must be found, but for query :["
                            << test_i->first.pos() << ", " << test_i->first.count()
                            << "] was found match:[" << range.pos() << ", " << range.count() << "]"
                    ));

                test_i->second.erase(found);
                });
            if (!test_i->second.empty())
            {
                tresult.error() << "Error in set of intersections:\nGot:";
                test_container.intersect_with(test_i->first, [&](const auto& range, auto _) {
                    tresult.error() << "\t[" << range.pos() << ", " << range.count() << "]\n";
                    });
                tresult.error() << "Diff expected still contains:\n";
                for(const auto& r:test_i->second )
                    tresult.error() << "\t[" << r.pos() << ", " << r.count() << "]\n";
            }
        }
    };

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    run_test(rand_init);
    std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
    tresult.info() << "Run took:"
        << std::chrono::duration<double, std::milli>(finish - start).count() << "[mls]\n"
        ;
}

template <class span_container_t>
struct SpanAdapter //test purpose only to expose the same
{
    using key_t = typename span_container_t::key_t;
    using value_t = typename span_container_t::value_t;
    void clear()
    {
        _container.clear();
    }

    //template <class ...TArgs>
    //void emplace(TArgs&&... args)
    template <class T>
    void emplace(const key_t& r, T&&)
    {
        //_container.emplace(std::forward<TArgs>(args)...);
        _container.emplace(r, int{});
    }

    template <class TCallback>
    size_t intersect_with(const key_t& query, TCallback callback) 
    {
        size_t n = 0;
        for (auto i = _container.intersect_with(query);
            i != _container.end(); ++i)
        {
            ++n;
            callback(i->first, i->second);
        }
        return n;
    }
    span_container_t _container;
};

void test_ZoneContainer(TestRuntime& tresult)
{
    auto pp = [](auto & t, const auto & u) -> auto&
    {
        std::cout << "{";
        size_t n = 0;
        for (const auto& el : t)
        {
            std::cout << (n++ ? ", " : "") << el;
        }
        std::cout << "}\n";
        n = 0;
        std::cout << "[";
        for (auto i = std::get<0>(u); ; ++i)
        {
            std::cout << (n++ ? ", " : "") << *i;
            if (i == std::get<1>(u))
                break;
        }
        std::cout << "]";
        return std::cout;
    };

    std::forward_list<int> d1{std::initializer_list<int> {1}};
    auto pair = std::make_pair(d1.begin(), d1.begin());
    pp(d1, pair) << "\n";
    pair.second = d1.emplace_after(pair.second, 2);
    pp(d1, pair) << "\n";
    pair.second = d1.emplace_after(pair.second, 3);
    pp(d1, pair) << "\n";
    d1.emplace_front(10);
    pp(d1, pair) << "\n";
    pair = std::make_pair(d1.begin(), d1.begin());
    pp(d1, pair) << "\n";
    pair.second = d1.emplace_after(pair.second, 11);
    pp(d1, pair) << "\n";

    ///////////////////
    using range_t = OP::Range < std::uint64_t, std::uint32_t>;
    OP::zones::BitboxRangeContainer<range_t, int> cor;

    cor.emplace(range_t{ 0x2, 0x3 }, 0);//masked by 0b0110
    cor.emplace(range_t{ 0x2, 0x4 }, 1);//masked by 0b1110
    cor.emplace(range_t{ 0x0, 0x10 }, 2);//masked by 0b11111
    auto simple_callback = [&](const auto& r, auto v) {
        std::cout << "[0x" << std::hex << r.pos() << ", <0x" << r.count() << ">]=" << v << "\n";
    };
    std::cout << "-->";
    tresult.assert_that<equals>(1, cor.intersect_with(range_t{ 0, 1 }, simple_callback));
    std::cout << "-->";
    tresult.assert_that<equals>(1, cor.intersect_with(range_t{ 0, 2 }, simple_callback));
    std::cout << "-->";
    tresult.assert_that<equals>(3, cor.intersect_with(range_t{ 0, 3 }, simple_callback));
    std::cout << "-->";
    tresult.assert_that<equals>(1, cor.intersect_with(range_t{ 7, 1 }, simple_callback));
    std::cout << "-->";
    tresult.assert_that<equals>(0, cor.intersect_with(range_t{ 0x11, 0x100 }, simple_callback));

    tresult.info() << "measure time of 100000 ranges on BitboxRangeContainer\n";
    run_ZoneContainerTest< OP::zones::BitboxRangeContainer<range_t, int>>(tresult);
    using span_container_t = OP::zones::SpanMap<range_t, int>;
    tresult.info() << "measure time of 100000 ranges on SpanContainer\n";
    run_ZoneContainerTest< SpanAdapter< span_container_t>>(tresult);
}

void test_Zones(TestRuntime& tresult)
{
    typedef Range<size_t> range_t;
    typedef std::map<range_t, int> ranges_t;

    ranges_t cont;
    //create many ranges
    for (auto i = 0; i < 10; ++i)
    {
        cont.emplace(range_t(i * 100, 10), i);
    }
    //check findability
    for (auto i = 0; i < 10; ++i)
    {
        auto f = cont.find(range_t(i * 100+5, 1));
        tresult.assert_true(cont.end() != f);
        tresult.assert_true(f->second == i);
        tresult.assert_true(f->first.count() == 10);
        f = cont.find(range_t(i * 100+11, 1));
        tresult.assert_true(cont.end() == f);
    }
    //test overlapping
    range_t r1 = { 10, 5 }, r2 = { 9, 5 }, r3 = { 14, 1 }, r4 = { 0, 11 };
    tresult.assert_true(r1.is_overlapped(r1)); //self test
    tresult.assert_true(r1.is_included(r1)); //self test

    tresult.assert_true(r1.is_overlapped(r2) && r2.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(r2) && !r2.is_included(r1));

    tresult.assert_true(r1.is_overlapped(r3) && r3.is_overlapped(r1));
    tresult.assert_true(r1.is_included(r3) && !r3.is_included(r1));

    tresult.assert_true(r1.is_overlapped(r4) && r4.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(r4) && !r4.is_included(r1));

    range_t nr1 = { 9, 6 }/*no overlaps, but inclusion*/, nr2 = { 0, 10 }/*just adjacence*/,
        nr3 = { 1, 1 }, nr4 = { 15, 2 }/*adjacent*/, nr5 = { 20, 2 };
    tresult.assert_true(r1.is_overlapped(nr1) && nr1.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr1) && nr1.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr2) && !nr2.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr2) && !nr2.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr3) && !nr3.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr3) && !nr3.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr4) && !nr4.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr4) && !nr4.is_included(r1));

    tresult.assert_true(!r1.is_overlapped(nr5) && !nr5.is_overlapped(r1));
    tresult.assert_true(!r1.is_included(nr5) && !nr5.is_included(r1));

}

static auto& module_suite = OP::utest::default_test_suite("Zones")
.declare("zoneContainer", test_ZoneContainer)
.declare("zone", test_Zones)
;