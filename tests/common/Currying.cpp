#include <op/utest/unit_test.h>
#include <op/common/Currying.h>
#include <cmath>
namespace
{

    using namespace OP::utest;

    void test_Plain(OP::utest::TestRuntime& tresult)
    {
        const std::string s_sample("xyz");
        const short n_sample(57); //not an explicit match
        auto test_function = [&](
            OP::utest::TestRuntime& tresult, int a, const std::string& str, bool& evidence)
        {
            tresult.assert_that<equals>(a, 57);    
            tresult.assert_that<equals>(str, s_sample);
            evidence = true;
        };
        bool invoke_evidence = false;
        using namespace OP::currying;
        auto f1 = arguments(std::ref(tresult), n_sample, std::cref(s_sample), 
            std::ref(invoke_evidence)).def(test_function);
        f1();
        tresult.assert_true(invoke_evidence);
        invoke_evidence = false;
        const int int_sample(n_sample); //explicit match
        auto f2 = arguments(std::ref(int_sample), std::ref(invoke_evidence), 
            std::ref(tresult), std::cref(s_sample), /*redundant arg*/5.7, "")
            .typed_bind(test_function);
        tresult.assert_false(invoke_evidence);
        f2();
        tresult.assert_true(invoke_evidence);

        invoke_evidence = false;
        auto f3 = arguments(
            std::ref(int_sample), std::ref(invoke_evidence),
            std::ref(tresult), of_callable([&]() -> const std::string&{ return s_sample; }), /*redundant arg*/5.7, "")
            .typed_bind(test_function);
        tresult.assert_false(invoke_evidence);
        f3();
        tresult.assert_true(invoke_evidence);
        //free args
        invoke_evidence = false;
        auto f4 = arguments(std::ref(int_sample), std::ref(invoke_evidence),
            std::cref(s_sample), /*redundant arg*/5.7, "")
            .typed_bind_free_front<1>(test_function);
        tresult.assert_false(invoke_evidence);
        f4(tresult);
        tresult.assert_true(invoke_evidence);
    }

    void test_Cast(OP::utest::TestRuntime& tresult)
    {
        const int mark_int = 57;
        auto src1 = [&](const int& x, bool& evidence) {
            tresult.assert_that<equals>(x, mark_int); 
            evidence = true; 
        };
        bool evidence = false;
        std::any shared_data;
        CurryingTuple <Var<bool>, Unpackable<std::any> > attrs{
            of_var(evidence),
            of_unpackable(shared_data)
        };

        auto f1 = attrs.typed_bind(src1);
        shared_data = mark_int;
        f1();
        tresult.assert_true(evidence);
    }

    float hypot3d(float x, float y, float z)
    {
        return std::hypot(x, y, z);
    }

    void test_Example()
    {
        auto free_x_y = arguments(2.f).//this value will pass as 3d argument z
            typed_bind_free_front<2>(hypot3d);
        std::cout << "(Should be 7.)hypot = " << free_x_y(3.f, 6.f) << "\n";
    }
    
    struct TstFunctor
    {
        bool operator()(float a, int b) const
        {
            return (a * b) < 0;
        }
    };
    void test_Retval(OP::utest::TestRuntime& tresult)
    {
        TstFunctor f;
        tresult.assert_true(arguments(2.f, -1).typed_invoke(f));
    }

    static auto& module_suite = OP::utest::default_test_suite("Currying")
    .declare("basic", test_Plain)
    .declare("cast", test_Cast)
    .declare("retval", test_Retval)
    .declare("example", test_Example)
    ;
}//ns:""
