#pragma once
#ifndef _OP_UTEST_CMDLN_UNIT_TEST__H_
#define _OP_UTEST_CMDLN_UNIT_TEST__H_

#include <string>
#include <functional>
#include <filesystem>

#include <op/utest/unit_test.h>
#include <op/utest/frontend/Console.h>
#include <op/utest/frontend/JsonFrontend.h>

#include <op/common/IoFlagGuard.h>
#include <op/common/CmdLine.h>

#include <ctime>
#include <chrono>
#include <regex>
#include <array>
#include <sstream>

namespace OP::utest::cmdline
{
    inline void list_cases()
    {
        auto suite_callback = [&](OP::utest::TestSuite& sui) {
            std::cout << sui.id() << ":\n";
            for (auto& cs : sui)
            {
                std::cout << "\t" << cs->id() << "\n";
            }
            return true;
        };
        OP::utest::TestRun::default_instance().list_suites(suite_callback);
        std::exit(0);
    }

    inline int simple_command_line_run(int argc, char **argv)
    {
        using namespace OP::console;
        using namespace OP::utest;
        namespace fs = std::filesystem;
        
        //default is run all
        std::function<bool(TestSuite&, TestCase&, TestFixture&)> test_case_filter
            = [](TestSuite&, TestCase&, TestFixture&) { return true; };

        bool fail_fast = false;
        bool render_json = false;

        OP::utest::TestRunOptions opts;

        std::function<void()> show_usage; //need later definition since `action(...)` cannot reference to `processor` var
        CommandLineParser processor(
            arg(
                key("-h"), key("--help"), key("-?"),
                desc("Show this usage."),
                action([&](){ show_usage(); })
            ),
            arg(
                key("-l"), 
                desc("List known test cases instead of run."),
                action(list_cases)
            ),
            arg(
                key("-d"),
                desc("Specify level of logging details: 1=error (default), 2=info, 3=debug."),
                action([&opts](std::uint64_t level) {opts.log_level(static_cast<ResultLevel>(level)); })
            ), 
            arg(
                key("-rs"),
                desc("<suite-regex> - Regular expression to match name of Test Suites to run. Only test cases belonging to matched suite are executed.\n" 
                    "\tFor example:\n"
                    "\t\t-rs \".+\" - run all test cases;\n"
                    "\t\t-rs \"test-[1-9]\" - run all test suites in range 'test-1'...'test-9'."
                    ),
                action( [&](const std::string& arg){
                    test_case_filter = [previous_predicate = std::move(test_case_filter), expression = std::regex(arg)](
                                TestSuite& suite, TestCase& tc, TestFixture& fixture) {
                            if (previous_predicate)
                            {
                                if (!previous_predicate(suite, tc, fixture))
                                    return false;
                            }
                            return std::regex_match(suite.id(), expression);
                        };
                })
            ),
            arg(
                key("-rc"),
                desc("<test-case-regex> - Regular expression to match name of Test Case to run.\n" 
                    "\tFor example:\n"
                    "\t\t-rc \".+\" - run all test cases;\n"
                    "\t\t-rc \"test-[^1-3]\" - run all test cases named 'test-4'...'test-9'."
                    ),
                action( [&](const std::string& arg){
                    
                    test_case_filter = [previous_predicate = std::move(test_case_filter), expression = std::regex(arg)](
                                        TestSuite& suite, TestCase& cs, TestFixture& fixture) {
                            if (previous_predicate)
                            {
                                if (!previous_predicate(suite, cs, fixture))
                                    return false;
                            }
                            return std::regex_match(cs.id(), expression);
                    };
                })
            ),
            arg(
                key("-rf"),
                desc("<test-fixture-regex> - Regular expression to match name of Test Fixture to run. If test suite has unnamed fixture, it can be matched by \"^$\".\n"
                    "\tFor example:\n"
                    "\t\t-rf \".+\" - run test cases sequentially enumerating all available test fixtures;\n"
                    "\t\t-rf \"(simple|base)\" - run test cases with fixtures named as 'simple' or 'base'."
                ),
                action([&](const std::string& arg) {

                    test_case_filter = [previous_predicate = std::move(test_case_filter), expression = std::regex(arg)](
                        TestSuite& suite, TestCase& cs, TestFixture& fixture) {
                            if (previous_predicate)
                            {
                                if (!previous_predicate(suite, cs, fixture))
                                    return false;
                            }
                            return std::regex_match(fixture.id(), expression);
                        };
                    })
            ),

            arg(
                key("-t"), key("-tw"),
                desc("<tag regex> - White list tag regular expression. Only Test Cases that contain matched tag are selected to run.\n" 
                    "\tFor example:\n"
                    "\t\t-t \"long\" - run all test cases tagged 'long';\n"
                    "\t\t-rt \"ISSUE-00[0-9]\" - run all test cases tagged with one of: 'ISSUE-000'...'ISSUE-009'."
                    ),
                action( [&](const std::string& arg){
                    
                    test_case_filter = [previous_predicate = std::move(test_case_filter), expression = std::regex(arg)](
                                        TestSuite& suite, TestCase& cs, TestFixture& fixture) {
                            if (previous_predicate)
                            {
                                if (!previous_predicate(suite, cs, fixture))
                                    return false;
                            }
                            for(const auto& tag: cs.tags())
                                if(std::regex_match(tag, expression))
                                    return true;
                            return false;
                    };
                })
            ),

            arg(
                key("-tb"),
                desc("<tag regex> - Black list tag regular expression. Only Test Cases that does NOT match tag are selected to run.\n" 
                    "\tFor example:\n"
                    "\t\t-t \"long\" - prevent running any test case tagged as 'long';\n"
                    "\t\t-rt \"ISSUE-00[1-8]\" - run all test cases tagged as: 'ISSUE-000', 'ISSUE-009'."
                    ),
                action( [&](const std::string& arg){
                    
                    test_case_filter = [previous_predicate = std::move(test_case_filter), expression = std::regex(arg)](
                                        TestSuite& suite, TestCase& cs, TestFixture& fixture) {
                            if (previous_predicate)
                            {
                                if (!previous_predicate(suite, cs, fixture))
                                    return false;
                            }
                            for(const auto& tag: cs.tags())
                                if(std::regex_match(tag, expression))
                                    return false;
                            return true;
                    };
                })
            ),
            arg(
                key("-n"),
                desc("<N> - Run test in bulk mode N times."),
                action([&opts](std::uint64_t runs) {
                    LoadRunOptions local;
                    local._runs = runs;
                    opts.load_run(local);
                    })
            ),
            arg(
                key("-s"),
                desc("Set seed number for the internal random generator to make tests reproducible. Without this parameter the seed is initialized with current time."),
                action([&opts](std::uint64_t seed) {opts.random_seed(seed); })
            ),
            arg( key("-f"),
                desc("Fail fast. Stop test process on unhandled exception or first failed test. By default this option is off - test engine tries to execute rest of the cases."),
                assign(&fail_fast)
            ),
            arg( key("-j"),
                desc("Render json report"),
                assign(&render_json)
            )
        );

        show_usage = [&]() {
            processor.usage(std::cout); 
            std::exit(0);
        };

        try
        {
            processor.parse(argc, const_cast<const char**>(argv));
        } catch(const std::invalid_argument& e)
        {
            std::cerr << e.what() << std::endl;
            processor.usage(std::cout);
            return 1;
        } catch (std::regex_error & e)
        {
            std::cerr << "Invalid regex in -r? argument: '" << e.what() << "'\n";
            return 1;
        }

        opts.fail_fast(fail_fast);
        OP::utest::TestRun::default_instance().options() = opts;
        OP::utest::frontend::ConsoleFrontend frontend(OP::utest::TestRun::default_instance());
        std::unique_ptr<OP::utest::frontend::JsonFrontend> json_frontend;
        
        if (render_json)
        {
            std::time_t t = std::time(nullptr);
            std::tm tm = *std::localtime(&t);
            std::ostringstream file_name;
            file_name << "test_at_" << std::put_time(&tm, "%Y-%m-%dT%H_%M_%S");
            auto report_path = fs::path(file_name.str()).replace_extension(".json");
            std::ofstream json_file(
                report_path,
                std::ios_base::out | std::ios_base::trunc);
            if (json_file.bad() || json_file.fail())
            {
                std::cerr << "Failed to create JSON report file:" << report_path << std::endl;
                return 1;
            }

            json_frontend.reset(new OP::utest::frontend::JsonFrontend(
                OP::utest::TestRun::default_instance(),
                std::move(json_file)));
        }


        // keep origin out formatting
        raii::IoFlagGuard cout_flags(std::cout);
        //std::set_terminate([]() {
        //    std::cerr << "fatal termination happened...\n";
        //    });
        auto all_result = 
            OP::utest::TestRun::default_instance().run_if(test_case_filter);

        using summary_t = std::tuple<size_t, TestResult::Status>;
        
        constexpr size_t n_statuses_c = TestResult::status_size_c;

        std::array<summary_t, n_statuses_c> all_summary{};
        int status = 0; //0 means everything good
        for (auto& result : all_result)
        {
            if (!result)
                status = 1;  //on exit will mean the some test failed

            auto &reduce = all_summary[(size_t)result.status() % n_statuses_c];
            if(! std::get<size_t>(reduce)++ )
            {
                std::get<TestResult::Status>(reduce) = result.status();
            }
        }
        //restore cout formatting
        cout_flags.reset();

        std::cout << "--== Total run results ==--:\n";
        //dump summary
        for( const auto& agg : all_summary )
        {
            if(std::get<size_t>(agg))
            {
                raii::IoFlagGuard cout_flags(std::cout);
                std::cout
                    << "\t" << frontend.status_to_colored_str(std::get <TestResult::Status>(agg))
                    << std::setfill('-') << std::setw(10)
                    << ">(" << std::get<size_t>(agg) << ")\n";
            }
        }
        return status;
    }

}//OP::utest::cmdline

#endif //_OP_UTEST_CMDLN_UNIT_TEST__H_
