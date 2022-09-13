#include <op/utest/unit_test.h>
#include <op/common/CmdLine.h>

namespace
{
    using namespace OP::utest;
    using namespace std::string_literals;
    using namespace OP::console;

    void test_CmdLine(OP::utest::TestRuntime& tresult)
    {
        struct X { int v = -1; std::string txt; bool y = false; 
        void setter(int a) { v = a; }
        } x;

        CommandLineParser cml(
            arg(key("-a"), key("--auto"), assign(&x.y)),
            arg(key("-v"), required(), assign(&x.v)), 
            arg(stroll{}, required(), assign(&x.txt))
        );
        std::string expected = "two  spaces";
        cml.parse(std::vector{ 
            "--auto"s,
            "-v"s, "11"s,
            expected
            });

        tresult.debug() << "X::v=" << x.v << ", X::txt='" << x.txt << "', x.y=" << x.y << "\n";
        
        tresult.assert_that<equals>(11, x.v, OP_CODE_DETAILS() );
        tresult.assert_that<equals>(expected, x.txt, OP_CODE_DETAILS() );
        tresult.assert_true(x.y, OP_CODE_DETAILS() );

        tresult.info() << "empty case...\n";
        x.y = false;
        x.v = -1;
        x.txt = "xxy";
        CommandLineParser cml2(
            arg(key("-a"), key("--auto"), assign(&x.y)),
            arg(stroll{}, assign(&x.txt))
        );
        cml2.parse(std::vector<std::string>{});
        tresult.assert_false(x.y);
        tresult.assert_that<equals>(x.v, -1);
        tresult.assert_that<equals>(x.txt, "xxy"s);

        x.v = -1;
        CommandLineParser cml3(
            arg(key("-a"), key("--auto"), assign(&x.v))
        );
        tresult.assert_exception<std::invalid_argument>(
            [&]() {
                cml3.parse(std::vector{ "-k"s, "57"s });
            }
        );
        tresult.assert_that<equals>(-1, x.v);

        tresult.assert_exception<std::invalid_argument>(
            [&]() {
                cml3.parse(std::vector{ "-a"s});
            }
        );
        CommandLineParser cml4(
            arg(key("-a"), key("--auto"), action(std::bind(&X::setter, x)))
        );
        cml3.parse(std::vector{ "-a"s, "11"s});
        tresult.assert_that<equals>(11, x.v);
    }

    void test_Required(OP::utest::TestRuntime& tresult)
    {
        bool action_invoked = false;
        int dummy = 0;
        CommandLineParser cml(
            arg(key("-a"), key("--auto"), 
                action(
                    [&](double x){ 
                        action_invoked = true; 
                        tresult.assert_that<equals>(x, 5.7); 
                    }) ),
            arg(key("-v"), required(), assign(&dummy)) 
        );
        tresult.assert_exception<std::invalid_argument>(
            [&](){
                cml.parse(std::vector{ "-a"s, "5.7"s });
            }
        );
        tresult.assert_true(action_invoked, OP_CODE_DETAILS() );

        tresult.info() << "Check `stroll` can be declared `required`...\n";
        //check ability of `stroll` to be required
        action_invoked = false;
        CommandLineParser cml2(
            arg(key("-v"), assign(&action_invoked)),
            arg(stroll{}, required() /*pay attention no assign there at all*/ )
        );
        tresult.assert_exception<std::invalid_argument>(
            [&](){
                cml2.parse(std::vector{ "-v"s });
            }
        );
        tresult.assert_true(action_invoked, OP_CODE_DETAILS() );
    }

    void test_Usage(OP::utest::TestRuntime& tresult)
    {
        std::vector<std::string> expected{ "-a, --auto: aaa"s, "-b: bbb"s, ": ccc" };
        std::string skip;
        CommandLineParser cml(
            arg(key("-a"), key("--auto"), assign(&skip), 
                desc("aaa")),
            arg(key("-b"), desc("bbb")),
            arg(stroll{}, desc("ccc") /*no assign there at all*/)
            );
        
        cml.usage(tresult.debug());
        std::ostringstream os;
        cml.usage(os);
        tresult.assert_that<negate<equals>>(std::string::npos, os.str().find(expected[0]));
        tresult.assert_that<negate<equals>>(std::string::npos, os.str().find(expected[1]));
        tresult.assert_that<negate<equals>>(std::string::npos, os.str().find(expected[2]));
    }


    static auto& module_suite = OP::utest::default_test_suite("CmdLine")
    .declare("basic", test_CmdLine)
    .declare("required", test_Required)
    .declare("usage", test_Usage)
    ;
} //ns:""
