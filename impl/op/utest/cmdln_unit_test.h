#pragma once
#ifndef _OP_UTEST_CMDLN_UNIT_TEST__H_
#define _OP_UTEST_CMDLN_UNIT_TEST__H_

#include <string>
#include <functional>
#include <op/utest/unit_test.h>
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
        //default is run all
        std::function<bool(OP::utest::TestSuite&, OP::utest::TestCase&)> test_case_filter
            = [](OP::utest::TestSuite&, OP::utest::TestCase&) { return true; };
        bool fail_fast = false;
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
                desc("Specify level of logging 1-3 (1=error, 2=info, 3=debug)."),
                action([&opts](std::uint64_t level) {opts.log_level(static_cast<ResultLevel>(level)); })
            ), 
            arg(
                key("-r"),
                desc("<suite-regex>/<case-regex> - Run only test cases matched to both regular expressions.\n" 
                    "\tFor example:\n"
                    "\t\t-r \".+/.+\" - run all test cases;\n"
                    "\t\t-r \"test-[1-9]/.*runtime.*\" - run all test suites in range 'test-1'...'test-9' with all\n"
                    "\t\t    cases containing the word 'runtime'."
                    ),
                action( [&](const std::string& arg){
                    std::regex expression(arg);

                    test_case_filter = [=](OP::utest::TestSuite& suite, OP::utest::TestCase& cs) {
                            std::string key = suite.id() + "/" + cs.id();
                            return std::regex_match(key, expression);
                        };
                })
            ),
            arg(
                key("-b"),
                desc("<N> - Run test in bulk mode N times."),
                action([&opts](std::uint64_t runs) {
                    LoadRunOptions opt;
                    opt._runs = runs;
                    opts.load_run(opt);
                    })
            ),
            arg(
                key("-s"),
                desc("Set seed number for the internal random generator to make tests reproducable. Without this paramater the seed is inititalized with current time."),
                action([&opts](std::uint64_t seed) {opts.random_seed(seed); })
            ),
            arg( key("-f"),
                desc("Fail fast. Stop test process on unhandled exception or first failed test. By default this option is off - test engine tries to execute rest of the cases."),
                stroll{}, 
                assign(&fail_fast)
                )
            )
            ;

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
            std::cerr << "Invalid regex in -r argument: '" << e.what() << "'\n";
            return 1;
        }

        opts.fail_fast(fail_fast);
        OP::utest::TestRun::default_instance().options() = opts;
        // keep origin out formatting
        IoFlagGuard cout_flags(std::cout);
        //std::set_terminate([]() {
        //    std::cerr << "fatal termination happened...\n";
        //    });
        auto all_result = 
            OP::utest::TestRun::default_instance().run_if(test_case_filter);

        using summary_t = std::tuple<size_t, TestResult::Status>;
        constexpr size_t n_statuses_c = 
            static_cast<std::uint32_t>(TestResult::Status::_last_) -
            static_cast<std::uint32_t>(TestResult::Status::_first_);

        std::array<summary_t, n_statuses_c> all_sumary{};
        int status = 0; //0 means everything good
        for (auto& result : all_result)
        {
            if (!result)
                status = 1;  //on exit will mean the some test failed

            auto &reduce = all_sumary[(size_t)result.status() % n_statuses_c];
            if(! std::get<size_t>(reduce)++ )
            {
                std::get<TestResult::Status>(reduce) = result.status();
            }
        }
        //restore cout formatting
        cout_flags.reset();

        std::cout << "--== Total run results ==--:\n";
        //dump summary
        for( const auto& agg : all_sumary )
        {
            if(std::get<size_t>(agg))
            {
                IoFlagGuard cout_flags(std::cout);
                std::cout
                    << "\t" << TestResult::status_to_colored_str(std::get < TestResult::Status > (agg))
                    << std::setfill('-') << std::setw(10)
                    << ">(" << std::get<size_t>(agg) << ")\n";
            }
        }
        return status;
    }

}//OP::utest::cmdline

#endif //_OP_UTEST_CMDLN_UNIT_TEST__H_
