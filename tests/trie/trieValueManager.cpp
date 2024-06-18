#include <algorithm>
#include <variant>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>

#include <op/vtm/EventSourcingSegmentManager.h>
#include <op/vtm/StringMemoryManager.h>

#include <op/flur/flur.h>

#include <op/trie/PlainValueManager.h>
#include <op/trie/Trie.h>

#include "TrieTestUtils.h"

using namespace OP::trie;
using namespace OP::utest;
static const char* test_file_name = "trie.test";

namespace OP::trie::store_converter
{
    template <class ... Tx>
    inline std::ostream& operator << (std::ostream& os, const PersistedVariant<Tx...>& val)
    {
        os << "{ persisted-variant }";
        return os;
    }

    template <class ...Tx>
    inline bool operator == (const PersistedVariant<Tx...>& left, const PersistedVariant<Tx...>& right) 
    {
        if (left._selector == right._selector)
            return memcmp(left._data, right._data, left.buffer_size_c) == 0;
        return false;
    }
//---------------------
    using namespace OP::vtm;

//---------------------
}//ns

template <class T, class TTopology>
auto oft(TTopology&topology, T&& t)
{
    using store_converter_t = OP::trie::store_converter::Storage<std::decay_t<T>>;
    typename store_converter_t::storage_type_t result;
    store_converter_t::serialize(topology, t, result);
    return result;
}

template <class T, class V, class TTopology>
auto tfo(TTopology& topology, const V & stor)
{
    return OP::trie::store_converter::Storage<T>::deserialize(topology, stor);
}

namespace
{
    struct TestStruct
    {
        TestStruct(double a,
            float x1 = 1, float x2 = 2, float x3 = 3,
            float x4 = 4, float x5 = 5, float x6 = 6,
            float x7 = 7, float x8 = 8, float x9 = 9,
            float x10 = 10, float x11 = 11)
            : v1(a), vector1{ x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11 }
        {}
        double v1;
        float vector1[11];
    };

    inline std::ostream& operator << (std::ostream& os, const TestStruct& val)
    {
        os << "{" << val.v1 << "...}";
        return os;
    }
    inline bool operator == (const TestStruct& left, const TestStruct& right)
    {
        return left.v1 == right.v1;
    }
    
    template <class  ... Tx>
    inline std::ostream& operator << (std::ostream& os, const std::variant<Tx...>& val)
    {
        os << "{var(" << val.index()<<")";
        size_t n = 0;
        std::visit([&](const auto& v) {
                using arg_t = std::decay_t<decltype(v)>;

                if constexpr (OP::has_operators::ostream_out_v<arg_t>)
                {
                    os << v;
                }
                else
                {
                    os << "[unprintable:" << typeid(arg_t).name() << "]";
                }
            }, val);
        os << "}";
        return os;
    }

    void testDefault(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using test_payload_t = TestStruct;

        using trie_t = Trie<
            EventSourcingSegmentManager, PlainValueManager<test_payload_t>
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        std::map<atom_string_t, test_payload_t> test_values;

        using p_t = std::pair<atom_string_t, test_payload_t>;

        const p_t ini_data[] = {
            p_t("ab.c"_astr, {1.}),
            p_t("abc"_astr, {11.}),
            p_t("abc.1"_astr, {13.}),
            p_t("abc.2"_astr, {15.}),
            p_t("abc.3"_astr, {17.}),
            p_t("abc.333"_astr, {19.}), //not a child since only 'abc.3' in the result
            p_t("abc.444"_astr, {21.}), // a child
            p_t("abcdef"_astr, {113.}),
        };

        for (const p_t& s : ini_data)
        {
            trie->insert(s.first, s.second);
            test_values.emplace(s);
        }
        compare_containers(tresult, *trie, test_values);

    }
    

    void testVariant(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using test_variant_t = std::variant<
            std::monostate,
            short, double, TestStruct,
            std::string
        >;
        using trie_t = Trie<
            EventSourcingSegmentManager, PlainValueManager<test_variant_t>
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        std::map<atom_string_t, test_variant_t> test_values;

        using p_t = std::pair<atom_string_t, test_variant_t>;

        const p_t ini_data[] = {
            p_t("ab.c"_astr, test_variant_t{1.}),
            p_t("abc"_astr, test_variant_t(TestStruct{11.})),
            p_t("xyz.1"_astr, test_variant_t{short(13)}),
            p_t("abc.1"_astr, test_variant_t{std::string("xyz")}),
            p_t("empty"_astr, test_variant_t{})
        };

        for (const p_t& s : ini_data)
        {
            trie->insert(s.first, s.second);
            test_values.emplace(s);
        }

        auto debug_prn = [&]() {
            for (const auto& p : trie->range())
            {
                tresult.debug() << (const char*)p.key().c_str() << "=" << p.value() << "\n";
            }
        };
        debug_prn();
        compare_containers(tresult, *trie, test_values);
        //test update with change type
        auto ab_range = trie->prefixed_range("ab"_astr);
        OP::vtm::TransactionGuard op_g(
            trie->segment_manager().begin_transaction(), true);
        for (auto iter : ab_range)
        {
            test_variant_t empt{};
            trie->update(iter, empt);
            test_values[iter.key()] = empt;
        }
        op_g.commit();
        compare_containers(tresult, *trie, test_values);
        debug_prn();

        OP::vtm::TransactionGuard op_g2(
            trie->segment_manager().begin_transaction(), true);
        for (auto iter : ab_range)
        {
            std::string rstr;
            test_variant_t new_var{tools::RandomGenerator ::instance().next_alpha_num(rstr, 32)};
            trie->update(iter, new_var);
            test_values[iter.key()] = new_var;
        }
        op_g2.commit();
        compare_containers(tresult, *trie, test_values);
        debug_prn();
    }

    template <class ...Tx>
    std::ostream& operator << (std::ostream& os, const std::tuple<Tx...>& t)
    {
        std::apply([&](const auto& ... args) {
            size_t n = 0;
            os << "{";
            ((os << (n++ == 0 ? "" : ", ") << args ), ...);
            os << "}";
            }, t);
        return os;
    }

    void testTuple(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using var_t = std::variant<std::monostate, short, double, TestStruct>;
        using test_tuple_t = 
            std::tuple<std::uint64_t, var_t, std::string>;

        using trie_t = Trie<
            EventSourcingSegmentManager, PlainValueManager<test_tuple_t>
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        std::map<atom_string_t, test_tuple_t> test_values;
        auto make_key = [](int x) {
            std::ostringstream os;
            os << std::hex << std::setw(8) << std::setfill('0')
                << x;
            return os.str();
        };
        for (int i = -11; i < 11; ++i) 
        {
            auto str = make_key(i);
            auto val = test_tuple_t(i, var_t(57.75 / i), str);
            auto [iter, ok] = trie->insert(str, val);
            tresult.assert_true(ok);
            test_values.emplace(iter.key(), std::move(val));
        }
        compare_containers(tresult, *trie, test_values);
        for (int i = -5; i < 5; ++i)
        {
            auto str = make_key(i);
            auto pos = trie->find(str);
            auto v = *pos;
            std::get<var_t>(v) = (double)i;
            trie->update(pos, v);
            test_values[pos.key()] = std::move(v);
        }
        compare_containers(tresult, *trie, test_values);
    }

    void testStr(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = Trie<
            EventSourcingSegmentManager, PlainValueManager<std::string>
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        std::map<atom_string_t, std::string> test_values;
        constexpr size_t N = 1025;
        for (auto i = 0; i < N; ++i)
        {
            std::string rstr;
            tools::RandomGenerator::instance().next_alpha_num(rstr, 0xFF, 1);
            trie->insert(rstr, rstr);
            test_values.emplace(atom_string_t(reinterpret_cast<const atom_t*>(rstr.c_str())), rstr);
        }
        compare_containers(tresult, *trie, test_values);

    }

    void testStrCompact(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::flur;
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>("trie2.test",
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using trie_t = Trie<
            EventSourcingSegmentManager, PlainValueManager<std::string, 16>, 1ull << 9
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        std::map<atom_string_t, std::string> test_values;
        constexpr size_t N = 1025;
        for (auto i = 0; i < N; ++i)
        {
            std::string rstr;
            tools::RandomGenerator::instance().next_alpha_num(rstr, 0xFF, 1);
            trie->insert(rstr, rstr);
            test_values.emplace(atom_string_t(reinterpret_cast<const atom_t*>(rstr.c_str())), rstr);
        }
        compare_containers(tresult, *trie, test_values);

    }

    void testErase(OP::utest::TestRuntime& tresult)
    {
        auto tmngr = OP::trie::SegmentManager::create_new<EventSourcingSegmentManager>(test_file_name,
            OP::trie::SegmentOptions()
            .segment_size(0x110000));

        using test_variant_t = std::variant<
            std::monostate,
            short, double, TestStruct,
            std::string
        >;
        using trie_t = Trie<
            EventSourcingSegmentManager, PlainValueManager<test_variant_t, 16>,
            1ull << 10
        >;
        std::shared_ptr<trie_t> trie = trie_t::create_new(tmngr);

        constexpr size_t N = 1025;
        for (auto i = 0; i < N; ++i)
        {
            std::string rstr;
            test_variant_t new_var{ tools::RandomGenerator::instance().next_alpha_num(rstr, 0xFF) };
            trie->insert(rstr, new_var);
            //if (!rstr.empty() && rstr[0] == '4')
            //    tresult.debug() << rstr << "\n";
        }
        //tresult.debug() << "4='" << *trie->find("4"_astr) << "'\n";
        for (std::uint16_t a = 0; a < 256; ++a)
        {
            atom_string_t k(1, (atom_t)a);
            trie->prefixed_key_erase_all(k);
        }
        tresult.assert_that<equals>(1, trie->nodes_count());
    }

    static auto& module_suite = OP::utest::default_test_suite("trie-values")
        .declare("default", testDefault)
        .declare("variant", testVariant)
        .declare("tuple", testTuple)
        .declare("str", testStr)
        .declare("str-compact", testStrCompact)
        .declare("erase", testErase)
        ;
}