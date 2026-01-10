#pragma once
#include <map>
#include <sstream>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

using namespace OP::utest;

template <class Os, class T>
void print_hex(Os& os, const T& t)
{
    OP::raii::IoFlagGuard stream_guard(os);
    auto b = std::begin(t), e = std::end(t);
    for (; b != e; ++b)
        os << std::setbase(16) << std::setw(2) << std::setfill('0') << (unsigned int)(unsigned char)*b;
    os << '\n';
}


namespace debug
{
    struct prn_ustr
    {
        std::string _buf;
        bool _as_hex = false;

        template <class TStrLike>
        explicit prn_ustr(const TStrLike& str, size_t trunc = std::string::npos)
            : _buf(
                reinterpret_cast<const char*>(str.data()), std::min(str.size(), trunc)) //trick to avoid unsigned casting
        {
        }

        prn_ustr& as_hex(bool flag = true)
        {
            _as_hex = flag;
            return *this;
        }

        friend inline std::ostream& operator << (std::ostream& os, const prn_ustr& obj)
        {
            if( obj._as_hex )
                print_hex(os, obj._buf);
            else
                os << obj._buf;
            return os;
        }
    };

    /// Allow check if instance of templated Range has member ::size(), usage: \code
    //  debug::has_size<Trie>::value \endcode
    OP_DECLARE_CLASS_HAS_MEMBER(size);

};

template <class Trie, class Map>
void compare_containers(OP::utest::TestRuntime &tresult, const Trie& trie, const Map& map)
{
    using namespace OP::utest;
    
    if constexpr(debug::has_size<Trie>::value)
    {
        auto tsn = trie.size(), msn = map.size();
        tresult.assert_that<equals>(tsn, msn);
    }

    auto mi = std::begin(map);
    
    auto ti = trie.begin();
    auto range_end = trie.end();
    int n = 0;
    //order must be the same
    for (; ti != range_end && mi != std::end(map); ++ti, ++mi, ++n)
    {
        //print_hex(tresult.info() << "1)", ti.key());
        //print_hex(tresult.info() << "2)", mi->first);
        tresult.assert_that<equals>(ti.key().size(), mi->first.size(),
            OP_CODE_DETAILS(
                << "step#" << n 
                <<" \"" << debug::prn_ustr(ti.key(), 16) << "\"..."
            ));
        tresult.assert_true(
            tools::container_equals(ti.key(), mi->first, &tools::sign_tolerant_cmp<OP::common::atom_t>),
            OP_CODE_DETAILS()
            <<" step#"<< n 
            << ", for key="<< debug::prn_ustr(mi->first) 
            << ", while obtained:" << debug::prn_ustr(ti.key()) );

        tresult.assert_that<equals>(ti.value(), mi->second,
            OP_CODE_DETAILS()<<" Associated value error, has:" << ti.value() << ", expected:" << mi->second );
    }

    if(mi != std::end(map))
    {
        std::ostringstream os;
        os << "sample map contains extra items:\n";
        for (; mi != std::end(map); ++mi)
        {
            os << "{" << debug::prn_ustr(mi->first) << ", " << mi->second << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }

    if (trie.in_range(ti))
    {
        std::ostringstream os;
        os << "Compared range contains extra items:\n";
        for (; trie.in_range(ti); trie.next(ti))
        {
            os << "{" << debug::prn_ustr(ti.key()) << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }
}

template <class Trie, class Map>
void compare_containers_relaxed_order(OP::utest::TestRuntime& tresult, const Trie& trie, Map map)
{

    if constexpr (debug::has_size<Trie>::value)
    {
        auto tsn = trie.size(), msn = map.size();
        tresult.assert_that<equals>(tsn, msn);
    }

    auto ti = trie.begin();
    int n = 0;
    
    //order relaxed
    for (; trie.in_range(ti) && !map.empty(); trie.next(ti), ++n)
    {
        //print_hex(tresult.info() << "1)", ti.key());
        //print_hex(tresult.info() << "2)", mi->first);
        const auto& key = ti.key();
        tresult.assert_that<equals>(1, map.erase(key),
            OP_CODE_DETAILS() << "step#" << n << ", sample map has no key=" << debug::prn_ustr(key)
        );
    }

    if (!map.empty())
    {
        std::ostringstream os;
        os << "sample map contains extra items:\n";
        for (auto mi : map)
        {
            os << "{" << debug::prn_ustr(mi.first) << ", " << mi.second << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }

    if (trie.in_range(ti))
    {
        std::ostringstream os;
        os << "Compared range contains extra items:\n";
        for (; trie.in_range(ti); trie.next(ti))
        {
            os << "{" << debug::prn_ustr(ti.key()) << "}\n";
        }
        tresult.fail(OP_CODE_DETAILS() << os.str());
    }
}
