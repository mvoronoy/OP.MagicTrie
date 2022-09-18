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
            .tdef(test_function);
        tresult.assert_false(invoke_evidence);
        f2();
        tresult.assert_true(invoke_evidence);

        invoke_evidence = false;
        auto f3 = arguments(
            std::ref(int_sample), std::ref(invoke_evidence),
            std::ref(tresult), of_callable([&]() -> const std::string&{return s_sample; }), /*redundant arg*/5.7, "")
            .tdef(test_function);
        tresult.assert_false(invoke_evidence);
        f3();
        tresult.assert_true(invoke_evidence);

    }


    static auto& module_suite = OP::utest::default_test_suite("Currying")
    .declare("plain", test_Plain)
    ;
}//ns:""