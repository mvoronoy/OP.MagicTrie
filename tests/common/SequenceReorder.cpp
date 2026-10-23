#include <iostream>
#include <cassert>
#include <concepts>
#include <span>

#include <op/common/SequenceReorder.h>
#include <op/utest/unit_test.h>

namespace 
{

    using namespace OP::utest;
    using namespace std::string_literals;


    template <auto n>
    struct I2T
    {
        using dep_type_t = decltype(n);
        constexpr static inline dep_type_t id_c = n;
        
        template <class T>
        constexpr bool operator == (const T& other) const noexcept
        {
            return id_c == other.id_c;
        }
    };

    template <class T, class U>
    constexpr inline std::partial_ordering depends_on_c = T::id_c == U::id_c
        ? std::partial_ordering::equivalent
        : (T::id_c % U::id_c == 0
            ? std::partial_ordering::greater
            : (U::id_c % T::id_c == 0 ? std::partial_ordering::less : std::partial_ordering::unordered)
            );

    template <class ...Tx>
    std::ostream& operator << (std::ostream& os, const std::tuple<Tx...>& tup)
    {
        size_t n = 0;
        os << '{';
        std::apply([&](const auto& ...it) {
            ((os << (n++ > 0 ? ", " : "") << "i2t<" << it.id_c << ">"), ...);
            }, tup);
        os << '}';
        return os;
    }


    inline std::ostream& operator << (std::ostream& os, const std::partial_ordering& ord)
    {
        if (std::partial_ordering::less == ord)
            os << "less";
        else if (std::partial_ordering::equivalent == ord)
            os << "equivalent";
        else if (std::partial_ordering::greater == ord)
            os << "greater";
        else
            os << "undefined";
        return os;
    }


    template <class Tup>
    struct Predicate
    {
        using origin_tup = Tup;

        template <size_t I, size_t J>
        constexpr static std::partial_ordering predicate = 
            depends_on_c< std::tuple_element_t<I, origin_tup>, std::tuple_element_t<J, origin_tup>>;
    };

    struct NumOrd
    {
        template <size_t I, size_t J>
        constexpr static std::partial_ordering predicate = (I < J ? std::partial_ordering::less :(
                J < I ? std::partial_ordering::greater : std::partial_ordering::equivalent
            ))
            ;
    };

    void test_sequence_reorder(OP::utest::TestRuntime& tresult)
    {
        tresult.debug() << std::boolalpha
            << "is_dep<2, 4>=" << depends_on_c<I2T<2>, I2T<4>> << "\n"
            << "is_dep<4, 2>=" << depends_on_c<I2T<4>, I2T<2>> << "\n"
            << "is_dep<2, 1>=" << depends_on_c<I2T<2>, I2T<1>> << "\n"
            ;
        using origin_tup = std::tuple<I2T<2>, I2T<3>, I2T<1>, I2T<12>, I2T<6>, I2T<7>, I2T<4>>;
        using predicate_t = Predicate<origin_tup>;
        tresult.debug() << "origin:" << origin_tup{} << "\n";
        using direct_index_t = decltype(OP::common::SequenceReorder{ std::make_index_sequence<std::tuple_size_v<origin_tup>>{} });
        constexpr auto direct_index = OP::common::SequenceReorder{ std::make_index_sequence<std::tuple_size_v<origin_tup>>{} };
        std::cout << direct_index << "\n";
        constexpr auto ordered_index = direct_index.reorder<predicate_t>();
        std::cout << ordered_index << "\n";
        // This test uses I2T as partially sorted elements, some of pair compare are undefined, some
        // are based on (x % y == 0). To declare this `expecting_tup` tuple depict reordered sequence in compare with 
        // `origin_tup`.
        using expecting_tup = std::tuple<I2T<1>, I2T<2>, I2T<3>, I2T<6>, I2T<7>, I2T<4>, I2T<12>>;
        expecting_tup result;
        ordered_index.apply([&](const auto& ...it) {
                ((std::cout << "i2t<" << it.id_c << "> "), ...);
                //compiler must Fail if order of items doesn't match to declared in `expecting_tup`
                result = std::make_tuple(it...);
            }, 
            origin_tup{});
        
        tresult.assert_that<equals>(result, expecting_tup{}); //assuming tuple supports == 
    }

    void test_sequence_reverse()
    {
        using straight_t = OP::common::SequenceReorder<0, 1, 2, 5>;
        static_assert(
            std::is_same_v<
                decltype(straight_t::reverse()),
                OP::common::SequenceReorder<5, 2, 1, 0>
            >);

        using single_t = OP::common::SequenceReorder<3>;
        static_assert(
            std::is_same_v<
                decltype(single_t::reverse()),
                OP::common::SequenceReorder<3>
            >);

        using empty_t = OP::common::SequenceReorder<>;
        static_assert(
            std::is_same_v<
                decltype(empty_t::reverse()),
                empty_t
            >);
    }

    static auto& module_suite = OP::utest::default_test_suite("SequenceReorder")
        .declare("reorder", test_sequence_reorder)
        .declare("reverse", test_sequence_reverse)
    ;

}//ns:

