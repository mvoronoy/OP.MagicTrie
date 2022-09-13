#ifndef _CMDLN_UNIT_TEST__H_7fcb8d3d_790c_46ce_abf8_2216991b5b9c
#define _CMDLN_UNIT_TEST__H_7fcb8d3d_790c_46ce_abf8_2216991b5b9c

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
namespace OP
{
    namespace utest{
        namespace cmdline
        {

            //inline int parse(int argc, char **argv,
        
        inline void list_cases()
        {
            auto suite_callback = [&](OP::utest::TestSuite& sui) {
                std::cout << sui.id() << "\n";
                for (auto& cs : sui)
                {
                    std::cout << "\t>" << cs->id() << "\n";
                }
                return true;
            };
            OP::utest::TestRun::default_instance().list_suites(suite_callback);
            std::exit(0);
        }
        template<char RegexKey = 'r'>
        inline int simple_command_line_run(int argc, char **argv)
        {
            using namespace OP::console;
            //default is run all
            std::function<bool(OP::utest::TestSuite&, OP::utest::TestCase&)> test_case_filter
                = [](OP::utest::TestSuite&, OP::utest::TestCase&) { return true; };
            OP::utest::TestRunOptions opts;

            std::function<void()> show_usage;
            CommandLineParser processor(
                arg(
                    key("-h"), key("--help"), key("-?"),
                    desc("Show this usage"),
                    action(show_usage)
                ),
                arg(
                    key("-l"), 
                    desc("list known test cases instead of run them"),
                    action(list_cases)
                ),
                arg(
                    key("-d"),
                    desc("level of logging 1-3 (1=error, 2=info, 3=debug)"),
                    action([&opts](std::uint64_t level) {opts.log_level(static_cast<ResultLevel>(level)); })
                ), 
                arg(
                    key("-r"),
                    desc("<regexp> - regular expression to filter test cases. Regex is matched agains pattern <Test SuiteName>/<Test Case Name>"),
                    action( [&](const std::string& arg){
                        std::regex expression(arg);

                        test_case_filter = [=](OP::utest::TestSuite& suite, OP::utest::TestCase& cs) {
                                std::string key = suite.id() + "/" + cs.id();
                                return std::regex_match(key, expression);
                            };
                    })
                ),
                arg(
                    key("-s"),
                    desc("set seed number for random generator to make tests reproducable. If not specified then default random generator used"),
                    action([&opts](std::uint64_t seed) {opts.random_seed(seed); })
                    )
                )
                ;
            show_usage = [&]() {processor.usage(std::cout); };
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
                std::cerr << "Invalid -r argument:" << e.what() << "\n";
                return 1;
            }
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

            std::cout << "==--Total run results--==:\n";
            //dump summary
            for( const auto& agg : all_sumary )
            {
                if(std::get<size_t>(agg))
                {
                    IoFlagGuard cout_flags(std::cout);
                    std::cout
                        << "\t" << TestResult::status_to_colored_str(std::get < TestResult::Status > (agg))
                        << std::setfill('-')<<std::setw(10)
                        << ">(" << std::get<size_t>(agg) << ")\n";
                }
            }
            return status;
        }
        } //ns:cmdline

    } //utest
}//OP
#endif //_CMDLN_UNIT_TEST__H_7fcb8d3d_790c_46ce_abf8_2216991b5b9c
