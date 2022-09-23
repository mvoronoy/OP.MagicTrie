#include <op/utest/unit_test.h>
#include <op/common/Currying.h>

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
            .typed_bind_front<1>(test_function);
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

    static auto& module_suite = OP::utest::default_test_suite("Currying")
    .declare("basic", test_Plain)
    .declare("cast", test_Cast)
    ;
}//ns:""
