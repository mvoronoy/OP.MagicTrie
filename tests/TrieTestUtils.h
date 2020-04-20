#pragma once
#include <map>
#include <sstream>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

template <class Trie, class Map>
void compare_containers(OP::utest::TestResult &tresult, const Trie& trie, const Map& map)
{
    auto mi = std::begin(map);
    //for (auto xp : map)
    //{
    //    print_hex(tresult.info(), xp.first );
    //    tresult.info() << /*xp.first << */'=' << xp.second << '\n';
    //}
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
