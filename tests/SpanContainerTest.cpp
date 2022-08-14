#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <random>
#include <algorithm>

#include <map>
#include <set>

#include <op/common/Range.h>
template <class Os, class Int1, class Int2 >
Os& operator << (Os& os, const OP::Range<Int1, Int2>& r)
{
    return os << r.pos() << ":" << r.right() << "(" << r.count() << ")";
}
#include <op/common/SpanContainer.h>

template <class Container, class Sampler>
void comparative_test(OP::utest::TestRuntime& tresult, Container& co, Sampler samples)
{
    for (auto found = co.begin(); found != co.end(); ++found)
    {
        auto probe_iter = samples.find(*found);
        tresult.assert_true(samples.end() != probe_iter);
        samples.erase(probe_iter);
    }
    tresult.assert_true(samples.empty());
}

template <class Span, class SpanContainer, class Sampler>
void intersect_test(OP::utest::TestRuntime& tresult, const Span& test_span, SpanContainer& from, Sampler samples)
{
    auto tracker = from.intersect_with(test_span);
    while (tracker != from.end() && OP::zones::is_overlapping(*tracker, test_span) && !samples.empty())
    {
        auto probe_iter = samples.find(*tracker);
        tresult.assert_true(samples.end() != probe_iter);
        samples.erase(probe_iter);
        ++tracker;
    }
    tresult.assert_true(samples.empty());
}
using zone_t = OP::Range<unsigned, unsigned>;
struct less_by_pos{ //used as less for std::set
    bool operator () (const zone_t& a, const zone_t& b) const 
    {
        return a.pos() < b.pos() || (a.pos() == b.pos() && a.count() < b.count()); 
    }
};
using check_container_t = std::multiset<zone_t, less_by_pos>;

void test_add(OP::utest::TestRuntime& tresult)
{
    using span_container_t = OP::zones::SpanSet<zone_t>;
    using tracker_t = typename span_container_t::IntersectionTracker;
    span_container_t container;
    check_container_t check_container;
    tracker_t oo1 (container, zone_t(0, 0));
    tresult.assert_true(!oo1 );

    auto r = container.emplace(10, 1000);
    tresult.assert_true(r->pos() == 10 && r->count() == 1000);
    check_container.emplace(*r);
    comparative_test(tresult, container, check_container);
    tracker_t  oo2(container, zone_t(10, 0));
    tresult.assert_true(!oo2);
    tracker_t oo3 (container, zone_t(0, 1));
    tresult.assert_true(!oo3 );
    tracker_t oo4 (container, zone_t(1111, 1));
    tresult.assert_true(!oo4 );

    tracker_t cs1 (container, zone_t(10, 1));
    tresult.assert_true(cs1->pos() == 10 && cs1->count() == 1000);

    r = std::move(container.emplace(10, 20));
    tresult.assert_true(r->pos() == 10 && r->count() == 20);
    check_container.emplace(*r);

    comparative_test(tresult, container, check_container);

    {
        const zone_t test_span(5, 7);
        intersect_test(tresult, test_span, container, check_container);
        container.emplace(1, 3);
        container.emplace(1001, 1);
        container.dump(std::cout << "--=======\n");
        intersect_test(tresult, test_span, container, check_container);
    }
    {
        const zone_t test_span(999, 5);
        check_container_t check_container;
        intersect_test(tresult, test_span, container, check_container_t{zone_t(10, 1000), zone_t(1001, 1)});
    }
    tresult.info() << "test dupplicate addings...\n";
    {   
        tresult.info() << "\tover existing values\n";
        check_container.emplace(1001, 1);
        check_container.emplace(1, 3);
        
        r = container.emplace(10, 1000);
        check_container.emplace(*r);
        comparative_test(tresult, container, check_container);

        r = container.emplace(1, 3);
        check_container.emplace(*r);

        r = container.emplace(1, 3);
        check_container.emplace(*r);

        r = container.emplace(10, 1000);//3-times
        check_container.emplace(*r);
        comparative_test(tresult, container, check_container);
        container.dump(std::cout << "--*3---\n");
        intersect_test(
            tresult,
            zone_t(3, 10), container, 
            check_container_t{zone_t(1, 3), zone_t(1, 3), zone_t(1, 3), zone_t(10, 20), zone_t(10, 1000), zone_t(10, 1000), zone_t(10, 1000)} );
    }

    {//smoke on erase
        const auto tst_span = zone_t(10, 1000);
        auto found = container.intersect_with(tst_span);
        while(*found != tst_span )
            ++found;
        tresult.assert_true(1 == container.erase(found));
        intersect_test(tresult,
            zone_t(3, 10), container, 
            check_container_t{zone_t(1, 3), zone_t(1, 3), zone_t(1, 3), zone_t(10, 20), zone_t(10, 1000), zone_t(10, 1000)} );
    }
}
int overlap_add(OP::utest::TestRuntime& tresult)
{
    using span_container_t = OP::zones::SpanSet<zone_t>;
    span_container_t container;
    check_container_t check_container;
    {
        auto r = container.emplace(10, 10);
        check_container.emplace(*r);
        r = container.emplace(20, 10);
        check_container.emplace(*r);
        container.dump(std::cout << "--=======\n");
        comparative_test(tresult, container, check_container);
        intersect_test(tresult, 
            zone_t(9, 2), container, check_container_t{zone_t(10, 10)});
        intersect_test(tresult, 
            zone_t(9, 31), container, check_container);
        intersect_test(tresult, 
            zone_t(19, 21), container, check_container);
        intersect_test(tresult, 
            zone_t(31, 1), container, check_container_t{});
        
    }
    {
        auto r = container.emplace(100, 10);
        check_container.emplace(*r);
        r = container.emplace(100, 5);
        check_container.emplace(*r);
        container.dump(tresult.debug() << "--=======\n");
        comparative_test(tresult, container, check_container);
        intersect_test(tresult, 
            zone_t(99, 2), container, check_container_t{zone_t(100, 10), zone_t(100, 5)});
        intersect_test(tresult, 
            zone_t(99, 12), container, check_container_t{zone_t(100, 10), zone_t(100, 5)});
        intersect_test(tresult,
            zone_t(104, 1), container, check_container_t{zone_t(100, 10), zone_t(100, 5)});
        intersect_test(tresult,
            zone_t(111, 1), container, check_container_t{});

        r = container.emplace(130, 5);
        check_container.emplace(*r);
        r = container.emplace(130, 10);
        check_container.emplace(*r);
        intersect_test(tresult,
            zone_t(129, 2), container, check_container_t{zone_t(130, 10), zone_t(130, 5)});
        intersect_test(tresult,
            zone_t(129, 12), container, check_container_t{zone_t(130, 10), zone_t(130, 5)});
        intersect_test(tresult,
            zone_t(134, 1), container, check_container_t{zone_t(130, 10), zone_t(130, 5)});
        intersect_test(tresult, 
            zone_t(141, 1), container, check_container_t{});
    }
    {
        auto r = container.emplace(1000, 10);
        check_container.emplace(*r);
        r = container.emplace(999, 5);
        check_container.emplace(*r);
        r = container.emplace(1009, 5);
        check_container.emplace(*r);
        container.dump(std::cout << "--=======\n");
        comparative_test(tresult,
            container, check_container);
        intersect_test(tresult,
            zone_t(999, 2), container, check_container_t{zone_t(999, 5), zone_t(1000, 10)});
        intersect_test(tresult,
            zone_t(1003, 5), container, check_container_t{zone_t(999, 5), zone_t(1000, 10)});
        intersect_test(tresult,
            zone_t(1008, 2), container, check_container_t{zone_t(1000, 10), zone_t(1009, 5)});
        intersect_test(tresult,
            zone_t(1011, 2), container, check_container_t{zone_t(1009, 5)});
        intersect_test(tresult,
            zone_t(1015, 1), container, check_container_t{});
    }
    intersect_test(tresult,
        zone_t(1, 10000), container, check_container);
    return 0;
}

constexpr unsigned UNIFIED_MAGIC_NUM = 1;
constexpr unsigned TST_NUM_RUNS = 20;
constexpr unsigned TST_CHUNK_SIZE = 111 * 16;

std::uniform_int_distribution<unsigned> pos_dist(0, 0x10000); //rules to generate zone begining
std::uniform_int_distribution<unsigned> size_dist(0x10, 0x1000);// rules to generate zone size

template <class Pt, class Rnd, class Vext>
void generate_rand_chunk(Rnd& rnd, Vext& container)
{
    container.clear();
    container.reserve(TST_CHUNK_SIZE);
    for (unsigned i = 0; i < TST_CHUNK_SIZE; ++i)
    {
        container.emplace_back(typename Vext::value_type{ Pt(pos_dist(rnd)), Pt(size_dist(rnd)) });
    }
}
auto find_all_intersects = [](const zone_t& what, const check_container_t& from) -> check_container_t {
    check_container_t rv;
    std::copy_if(from.begin(), from.end(),
            std::inserter(rv, rv.end()), 
        [&](const auto& chk )->bool{ return OP::zones::is_overlapping(what, chk);}
        );
    return rv;
};

void random_stuff_test(OP::utest::TestRuntime& tresult)
{
    using span_container_t = OP::zones::SpanSet<zone_t>;
    using tracker_t = typename span_container_t::IntersectionTracker ;
    span_container_t container;
    check_container_t check_container;
    std::mt19937 rgen;
    // Seed the engine with an unsigned int
    rgen.seed(UNIFIED_MAGIC_NUM);

    for (unsigned i = 0; i < TST_CHUNK_SIZE; ++i)
    {
        auto p = pos_dist(rgen);
        auto sz = size_dist(rgen);
        auto r = container.emplace(p, sz);
        tresult.assert_true(*r == *check_container.emplace(p, sz));
    }
    comparative_test(tresult, 
        container, check_container);
    //
    // Positive test, check all intersections are foundable
    //
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    for (const auto& z : check_container)
    {
        auto expected = find_all_intersects(z, check_container);
        /*if(z.pos() == 0x1f && z.count() == 0x1a9)
        {
            container.dump(std::cout<<"======================\n");
        }*/
        intersect_test(tresult, 
            z, container, expected);
    }
    std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
    tresult.info() << "Positive took:"
        << std::chrono::duration<double, std::milli>(finish - start).count() << "[mls]\n"
        ;
    //
    //  Positive + Negative tests - intersect with random span that exists and non exists
    //
    unsigned prev_p = 0, prev_sz = 0;
    start = std::chrono::steady_clock::now();
    for(auto i = 0; i < TST_CHUNK_SIZE; ++i)
    {
        auto p = pos_dist(rgen);
        auto sz = size_dist(rgen);
        zone_t z(p, sz);
        auto expected = find_all_intersects(z, check_container);
        intersect_test(tresult,
            z, container, expected);
    }
    finish = std::chrono::steady_clock::now();
    tresult.info() << "Negative+Positive took:"
        << std::chrono::duration<double, std::milli>(finish - start).count() << "[mks]"
        << std::endl;
}
void test_erase(OP::utest::TestRuntime& tresult)
{
    using span_container_t = OP::zones::SpanSet<zone_t>;
    span_container_t container;

    tresult.assert_true(0 == container.erase(container.begin()));

    auto r = container.emplace(10, 3);
    intersect_test( tresult, 
        zone_t(3, 10), container, 
        check_container_t{zone_t(10, 3)} );

    tresult.assert_true(1 == container.erase(r));

    intersect_test( tresult, 
        zone_t(3, 10), container, 
        check_container_t{} );

    r = container.emplace(10, 3);
    container.emplace(7, 5);
    container.emplace(11, 7);
    container.emplace(3, 20);
    tresult.assert_true(1 == container.erase(r));
    intersect_test( tresult, 
        zone_t(3, 10), container, 
        check_container_t{zone_t(7, 5), zone_t(11, 7), zone_t(3, 20)} );
    {//test to proove independance of erasure order
        std::random_device rd;
        std::mt19937 g(rd());
 
        for (int i = 0; i < 25; ++i)
        {
             std::vector<decltype(r)> ins_res = {
                container.emplace(10, 3),
                container.emplace(10, 3),
                container.emplace(10, 3),
                container.emplace(10, 3)
             };
             std::shuffle(ins_res.begin(), ins_res.end(), g);
        
             for(auto &ri : ins_res)
                tresult.assert_true(1 == container.erase(ri));

             intersect_test( tresult,
                 zone_t(3, 10), container, 
                check_container_t{zone_t(7, 5), zone_t(11, 7), zone_t(3, 20)} );
        }
    }
    container.dump(std::cout << "----[After erasure]----\n");
    {
        std::vector<decltype(r)> check_container;
        check_container_t exist_conatiner;
        std::mt19937 rgen;
        // Seed the engine with an unsigned int
        rgen.seed(UNIFIED_MAGIC_NUM);

        for (unsigned i = 0; i < TST_CHUNK_SIZE; ++i)
        {
            auto p = pos_dist(rgen);
            auto sz = size_dist(rgen);
            auto r = container.emplace(p, sz);
            if( (i % 3) == 0 ) //peek each 3d
                check_container.emplace_back(r);
            else
                exist_conatiner.emplace(*r);
        }
        // **********
        for (auto & w : check_container)
        {
            tresult.assert_true(1 == container.erase(w));
        }
        // ********** check integrity
        for (const auto& z : exist_conatiner)
        {
            auto expected = find_all_intersects(z, exist_conatiner);
            intersect_test(tresult, 
                z, container, expected);
        }

    }
}
void test_dump(OP::utest::TestRuntime& tresult)
{
    using span_container_t = OP::zones::SpanSet<zone_t>;
    using tracker_t = typename span_container_t::IntersectionTracker ;
    span_container_t container;
    std::mt19937 rgen;
    // Seed the engine with an unsigned int
    rgen.seed(UNIFIED_MAGIC_NUM);
    const unsigned limit_c = 200;
    for (unsigned i = 0; i < limit_c; ++i)
    {
        auto p = pos_dist(rgen);
        auto sz = size_dist(rgen);
        auto r = container.emplace(p, sz);
    }
    container.dump(tresult.debug() << "--====##### Branches dump for :("<< limit_c<< ") node #####====--\n");
}

void test_map(OP::utest::TestRuntime& tresult)
{
    
    using span_map_t = OP::zones::SpanMap<zone_t, double>;
    span_map_t map;
    map.emplace(zone_t(1, 2), 1.2);
    map.emplace(zone_t(1, 3), 1.3);
    map.emplace(zone_t(1, 4), 1.4);
    map.emplace(zone_t(1, 4), 1.4);
    map.emplace(zone_t(1, 4), 1.4);
    map.emplace(zone_t(1, 4), 1.4);
    map.emplace(zone_t(2, 2), 2.2);
    map.emplace(zone_t(4, 2), 4.2);
    map.emplace(zone_t(1, 3), 1.3);

    std::multimap<zone_t, double, less_by_pos> sample_map{
        {zone_t(1, 2), 1.2},
        {zone_t(1, 3), 1.3},
        {zone_t(1, 4), 1.4},
        {zone_t(1, 4), 1.4},
        {zone_t(1, 4), 1.4},
        {zone_t(1, 4), 1.4},
        {zone_t(2, 2), 2.2},
        {zone_t(1, 3), 1.3}
    };

    for(auto i = map.intersect_with(zone_t(2, 1)); i != map.end(); ++i)
    {
        tresult.debug() << i->first << ":*:" << i->second << "\n";
        auto fnd = sample_map.find(i->first);
        tresult.assert_true(fnd != sample_map.end() );
        sample_map.erase(fnd);
    }
    tresult.assert_true(sample_map.empty());
}
static auto& module_suite = OP::utest::default_test_suite("SpanContainer")
.declare("add", test_add)
.declare("overlap", overlap_add)
.declare("random", random_stuff_test)
.declare("erase", test_erase)
.declare("map", test_map)
;
