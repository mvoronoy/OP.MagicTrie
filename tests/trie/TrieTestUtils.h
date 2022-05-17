#pragma once
#include <map>
#include <sstream>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

using namespace OP::utest;

template <class Trie, class Map>
void compare_containers(OP::utest::TestRuntime &tresult, const Trie& trie, const Map& map)
{
    using namespace OP::utest;

    auto tsn = trie.size(), msn = map.size();
    tresult.assert_that<equals>(tsn, msn, OP_CODE_DETAILS() << "Size is wrong, expected:" << msn << ", but: " << tsn);

    auto mi = std::begin(map);
    
    auto ti = trie.begin();
    int n = 0;
    //order must be the same
    for (; trie.in_range(ti) && mi != std::end(map); trie.next(ti), ++mi, ++n)
    {
        //print_hex(tresult.info() << "1)", ti.key());
        //print_hex(tresult.info() << "2)", mi->first);
        tresult.assert_true(ti.key().length() == mi->first.length(), 
            OP_CODE_DETAILS()<<"step#"<< n << " has:" << ti.key().length() << ", while expected:" << mi->first.length());
        tresult.assert_true(tools::container_equals(ti.key(), mi->first, &tools::sign_tolerant_cmp<atom_t>),
            OP_CODE_DETAILS()<<"step#"<< n << ", for key="<<(const char*)mi->first.c_str() << ", while obtained:" << (const char*)ti.key().c_str());
        tresult.assert_that<equals>(ti.value(), mi->second,
            OP_CODE_DETAILS()<<"Associated value error, has:" << ti.value() << ", expected:" << mi->second );
    }
    if(mi != std::end(map))
    {
        std::ostringstream os;
        os << "sample map contains extra items:\n";
        for (; mi != std::end(map); ++mi)
        {
            os << "{" << (const char*)mi->first.c_str() << ", " << mi->second << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }
    if (trie.in_range(ti))
    {
        std::ostringstream os;
        os << "Compared range contains extra items:\n";
        for (; trie.in_range(ti); trie.next(ti))
        {
            os << "{" << (const char*)ti.key().c_str() << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }
}
template <class Trie, class Map>
void compare_containers_relaxed_order(OP::utest::TestRuntime& tresult, const Trie& trie, Map map)
{
    auto tsn = trie.size(), msn = map.size();
    tresult.assert_that<equals>(tsn, msn, OP_CODE_DETAILS() << "Size is wrong, expected:" << msn << ", but: " << tsn);

    auto ti = trie.begin();
    int n = 0;
    //order relaxed
    for (; trie.in_range(ti) && !map.empty(); trie.next(ti), ++n)
    {
        //print_hex(tresult.info() << "1)", ti.key());
        //print_hex(tresult.info() << "2)", mi->first);
        const auto& key = ti.key();
        tresult.assert_that<equals>(1, map.erase(key),
            OP_CODE_DETAILS() << "step#" << n << ", sample map has no key=" << (const char*)key.c_str());
    }
    if (!map.empty())
    {
        std::ostringstream os;
        os << "sample map contains extra items:\n";
        for (auto mi : map)
        {
            os << "{" << (const char*)mi.first.c_str() << ", " << mi.second << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }
    if (trie.in_range(ti))
    {
        std::ostringstream os;
        os << "Compared range contains extra items:\n";
        for (; trie.in_range(ti); trie.next(ti))
        {
            os << "{" << (const char*)ti.key().c_str() << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }
}
