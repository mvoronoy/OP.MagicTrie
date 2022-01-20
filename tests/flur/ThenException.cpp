#include <vector>
#include <numeric>
#include <array>
#include <map>
#include <set>
#include <numeric>
#include <op/utest/unit_test.h>
#include <op/flur/flur.h>

namespace {
    using namespace OP::utest;
    using namespace OP::flur;
    using namespace std::string_literals;

    template <class Ex = std::runtime_error>
    auto mk_generator(int start_from, int raise_at)
    {
        return src::generator([=]() mutable {
            if (start_from == raise_at)
            {
                throw Ex("generation fail emulation");
            }

            return std::make_optional(start_from++);
            });
    }

    void test_ExceptionOnStart(OP::utest::TestResult& tresult)
    {
        tresult.info() << "check exception raised on start...\n";

        auto pipeline = mk_generator(0, 0) >> then::on_exception<std::runtime_error>(src::of_value(57));
        size_t n = 0;
        for (auto i : pipeline)
        {
            ++n;
            tresult.assert_that<equals>(57, i, "only 1 value must pass");
        }
        tresult.assert_that<equals>(1, n, "single entry only");

    }
    void test_ExceptionOnNext(OP::utest::TestResult& tresult)
    {
        tresult.info() << "check exception raised on next...\n";

        auto pipeline = mk_generator(1, 2) >> then::on_exception<std::runtime_error>(src::of_value(57));
        size_t n = 0, sum = 0;
        for (auto i : pipeline)
        {
            ++n;
            tresult.assert_that<less>(sum, i, "only 1 value must pass");
            sum += i;

        }
        tresult.assert_that<equals>(sum, 58, "single entry only");

    }
    void test_ExceptionOnInRange(OP::utest::TestResult& tresult)
    {
        tresult.info() << "check exception raised on in_range...\n";

        struct LocalSeq : Sequence<int>
        {
            LocalSeq(int) {}
            void start() override {};

            bool in_range() const {
                throw std::runtime_error{ "" };
            }
            int current() const {
                return 0;
            }
            void next() {};
        };

        auto pipeline = make_lazy_range(SimpleFactory<int, LocalSeq>())
            >> then::on_exception<std::runtime_error>(src::of_value(57))
            ;
        size_t n = 0;
        for (auto i : pipeline)
        {
            ++n;
            tresult.assert_that<equals>(57, i, "only 1 value must pass");
        }
        tresult.assert_that<equals>(1, n, "single entry only");
    }


    void test_ExceptionOnCurrent(OP::utest::TestResult& tresult)
    {
        tresult.info() << "check exception raised on current...\n";

        auto pipeline = src::of_iota(0, 10)
            >> then::mapping([](auto i) ->int {
            throw std::runtime_error{ "current" };
                })
            >> then::on_exception<std::runtime_error>(src::of_value(57))
                    ;
                size_t n = 0;
                for (auto i : pipeline)
                {
                    ++n;
                    tresult.assert_that<equals>(57, i, "only 1 value must pass");
                }
                tresult.assert_that<equals>(1, n, "single entry only");
    }

    void test_MultyExceptions(OP::utest::TestResult& tresult)
    {
        tresult.info() << "check multi exceptions handler...\n";

        int invocked = 0;
        auto pipeline = mk_generator<std::logic_error>(0, 1)
            >> then::on_exception<std::logic_error>(src::of_value(42)) //intercept only std::logic_error, but not std::runtime_error
            >> then::mapping([](auto n) ->int {throw std::runtime_error("re-raise error"); })
            >> then::on_exception<std::runtime_error>(src::of_value(73))
            ;
        size_t n = 0;
        for (auto i : pipeline)
        {
            ++n;
            tresult.assert_that<equals>(73, i, "unexpected number");
        }
        tresult.assert_that<equals>(1, n, "exact once");

        tresult.info() << "check multi exceptions arbitrary order...\n";
        auto pipeline2 = mk_generator<std::logic_error>(0, 1)
            >> then::on_exception<std::runtime_error>(src::of_value(42)) //not a case!
            >> then::mapping([](auto i) ->int {
                    if( i < 1 )
                        return i;
                    throw std::runtime_error("re-raise error"); 
                })
            >> then::on_exception<std::runtime_error>(src::of_value(73))
            >> then::on_exception<std::logic_error>(src::of_value(57)) 
            ;
        n = 0;
        for (auto i : pipeline2)
        {
            tresult.assert_that<equals>(57*n, i, "unexpected number");
            ++n;
        }
        tresult.assert_that<equals>(2, n, "1 entries only");
    }

    static auto module_suite = OP::utest::default_test_suite("flur.then.exception")
        ->declare(test_ExceptionOnStart, "on-start")
        ->declare(test_ExceptionOnNext, "on-next")
        ->declare(test_ExceptionOnInRange, "on-in_range")
        ->declare(test_ExceptionOnCurrent, "on-current")
        ->declare(test_MultyExceptions, "multi")
        ;
}//ns:<empty>