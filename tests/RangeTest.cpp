#include <op/trie/Range.h>
#include <op/trie/Node.h>
#include <assert.h>
using namespace OP;
using namespace OP::trie;
void test_RangeContainer()
{
    typedef RangeContainer<NodeAddress> ranges_t;
    ranges_t r;
    NodeAddress a1;
    try
    {
        a1 = r.pull_range();
        assert(false);//exception must be raised
    }
    catch (std::out_of_range& )
    {
    }
    r.add_range(NodeAddress(0, 1)); 
    a1 = r.pull_range();
    r.add_range(a1);
    std::vector<NodeAddress> rand_v;
    for (unsigned i = 0; i < 1000; ++i)
        rand_v.emplace_back(NodeAddress(0, i));
    std::random_shuffle(rand_v.begin(), rand_v.end());
    for (auto n : rand_v)
        r.add_range(n);
    assert(r.size() == 1);
}
void test_Range()
{
    typedef Range<size_t> range_t;
    typedef std::map<range_t, int> ranges_t;

    ranges_t cont;
    //create many ranges
    for (auto i = 0; i < 10; ++i)
    {
        cont.insert(ranges_t::value_type(range_t(i * 100, 10), i));
    }
    //check findability
    for (auto i = 0; i < 10; ++i)
    {
        auto f = cont.find(range_t(i * 100+5, 1));
        assert(cont.end() != f);
        assert(f->second == i);
        assert(f->first.count() == 10);
        f = cont.find(range_t(i * 100+11, 1));
        assert(cont.end() == f);
    }
}