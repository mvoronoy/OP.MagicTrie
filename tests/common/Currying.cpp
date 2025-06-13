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
            .def(test_function);
        tresult.assert_false(invoke_evidence);
        f2();
        tresult.assert_true(invoke_evidence);

        invoke_evidence = false;
        auto f3 = arguments(
            std::ref(int_sample), 
            std::ref(invoke_evidence),
            std::ref(tresult), 
            of_callable([&]() -> const std::string&{ return s_sample; }), 
            /*redundant arg*/5.7, 
            "")
            .def(test_function);
        tresult.assert_false(invoke_evidence);
        f3();
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

        auto f1 = attrs.def(src1);
        shared_data = mark_int;
        f1();
        tresult.assert_true(evidence);
    }

    float sum_int(float x, int y)
    {
        return x + y;
    }

    //Test for compile-time only check
    void test_Example(OP::utest::TestRuntime& tresult)
    {
        using namespace std::placeholders;
        auto all = arguments(2.f, -9).
            invoke(sum_int);
        tresult.debug() << "(Should be -7.)sum = " << all << "\n";
        tresult.assert_that<almost_eq>(-7.f, all);
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
        tresult.assert_true(arguments(2.f, -1).invoke(f));
    }

    struct ClassWithMember
    {
        int _a;
        explicit ClassWithMember(int a) noexcept
            :_a(a) {}
        float member(int b, float discontinuous) const 
        {
            if (_a == b)
                return float(_a + b) / discontinuous;
            return float(_a + b) / float(_a - b);
        }
        void update(int a)
        {
            _a = a;
        }

        float consumer_of_float(float arg) 
        {
            _a *= arg;
            return _a;
        }
    };

    void test_MemberFunction(OP::utest::TestRuntime& tresult)
    {
        const ClassWithMember const_class(11);
        auto eval = use_member(&ClassWithMember::member);
        tresult.assert_that<almost_eq>(-23.f, 
            arguments(12, 7.5f, const_class).invoke(eval));
        //check std::reference_wrapper works
        tresult.assert_that<almost_eq>(-23.f,
            arguments(12, 7.5f, std::cref(const_class)).invoke(eval));

        //check member allowed alter class instance
        ClassWithMember class_with_method(11);
        auto up_method = use_member(&ClassWithMember::update);
        arguments(15, 7.5f, std::ref(class_with_method)).invoke(up_method);
        tresult.assert_that<equals>(15, class_with_method._a);

        //check member allowed with const reference
        tresult.assert_that<almost_eq>(9.f,
            arguments(std::cref(class_with_method), 12, 7.5f).invoke(eval));
        
        ////////////////////////////////////////////////////////////
        tresult.assert_that<almost_eq>(-18.f,
                arguments(12, of_member(const_class, &ClassWithMember::member, 101, 102.f), std::ref(class_with_method))
                .invoke(use_member(&ClassWithMember::consumer_of_float))
        );

    }

    static auto& module_suite = OP::utest::default_test_suite("Currying")
    .declare("basic", test_Plain)
    .declare("cast", test_Cast)
    .declare("retval", test_Retval)
    .declare("example", test_Example)
    .declare("member", test_MemberFunction)
    ;
}//ns:""
