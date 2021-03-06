#ifndef _CMDLN_UNIT_TEST__H_7fcb8d3d_790c_46ce_abf8_2216991b5b9c
#define _CMDLN_UNIT_TEST__H_7fcb8d3d_790c_46ce_abf8_2216991b5b9c

#include <string>
#include <op/utest/unit_test.h>

#include <ctime>
#include <chrono>
#include <regex>
#include <map>
#include <sstream>
namespace OP
{
    namespace utest{
        namespace cmdline
        {
            using ArgumentDef = std::tuple<std::string, bool, std::string>;
            
            template <char SwitchChar = '-'>
            struct ArgumentProcessor
            {
                ArgumentProcessor()
                {
                }

                ArgumentProcessor& on(std::string key)
                {
                    _actions.emplace_back( 
                        Def{std::move(key)}
                    );
                    return *this;
                }
                /**
                *   declare command line switcher without parameters. 
                * \code
                *   parser.on("-b").action([](){ ... })
                * \endcode
                * Means that as soon as `-b` specified then lambda without arguments is invoked
                */
                ArgumentProcessor& action(std::function<void()> f)
                {
                    if(_actions.empty())
                        throw std::runtime_error("::action withot ::on");
                    _actions.back().need_arg = false;
                    _actions.back().action = [f](const std::string&){
                        f();
                    };
                    return *this;
                }
                /**
                *   declare command line switcher with 1 parameter.
                * \code
                *   parser.on("-n").action([](int n){ ... })
                * \endcode
                * Means that as soon as `-n 5` specified then lambda with converted int will be invoked
                */
                ArgumentProcessor& action(std::function<void(const std::string&)> f)
                {
                    if (_actions.empty())
                        throw std::runtime_error("::action withot ::on");
                    _actions.back().need_arg = true;
                    _actions.back().action = [f](const std::string& arg) {
                        f(arg);
                    };
                    return *this;
                }
                /**
                *   Declare callback with arbitrary argument of type `A` that convertible from command line argument
                *   \typename A must support default constructor and operator `>>` to convert from string argument
                */
                template <class A>
                ArgumentProcessor& action(std::function<void(A)> f)
                {
                    if(_actions.empty())
                        throw std::runtime_error("::action withot ::on");
                    _actions.back().need_arg = true; 
                    _actions.back().action = [f](const std::string& arg){
                        A a;
                        std::istringstream(arg) >> a;
                        f(a);
                    }; 
                    return *this;
                }
                template <class A>
                ArgumentProcessor& stroll_action(std::function<void(A)> f)
                {
                    _actions.back().action = [&](const std::string& arg){
                        A a;
                        std::istringstream(arg) >> a;
                        f(a);
                    }; 
                    _has_stroll = true;
                    return *this;
                }
                ArgumentProcessor& description(std::string desc)
                {
                    if(_actions.empty())
                        throw std::runtime_error("::action withot ::on");
                    _actions.back().description = std::move(desc);
                    return *this;
                }
                void parse(int argc, char **argv) const
                {
                    for(auto i = 1; i < argc; ++i)
                    {
                        std::string arg(argv[i]);
                        if( arg[0] == SwitchChar )
                        { //suppose argument
                            auto found = std::find_if(
                                _actions.begin(), _actions.end(), 
                                [&](const auto& def)->bool{
                                    return def.key == arg;
                                });
                            if( found == _actions.end() )
                                throw std::invalid_argument(std::string("Unknown argument:")+arg);
                            if( found->need_arg )
                            {
                                if( ++i < argc )
                                {
                                    arg = argv[i];
                                }
                                else
                                    throw std::invalid_argument(std::string("Missed parameter for argument:")+arg);
                            }
                            found->action(arg);
                        } else if(_has_stroll)
                        {
                            _stroll_action.action(arg);
                        } else
                        {
                            throw std::invalid_argument(std::string("Unknown parameter:")+arg);
                        }
                    }
                }
            private:
                
                struct Def
                {
                    std::string key;
                    bool need_arg;
                    std::string description;
                    std::function<void(const std::string&)> action;
                };
                using key_action_t = std::vector< Def >;
                key_action_t _actions;
                Def _stroll_action;
                bool _has_stroll = false;
            };

            //inline int parse(int argc, char **argv,
        

        template<char RegexKey = 'r'>
        inline int simple_command_line_run(int argc, char **argv)
        {
            //default is run all
            std::function<bool(OP::utest::TestSuite&, OP::utest::TestCase&)> test_case_filter
                = [](OP::utest::TestSuite&, OP::utest::TestCase&) { return true; };
            OP::utest::TestRunOptions opts;
            ArgumentProcessor<'-'> processor;
            processor
                .on("-l")
                .description("list known test cases instead of run them")
                .action([](){
                    auto case_callback = [](OP::utest::TestCase& cs)-> bool {
                        std::cout << "\t>" << cs.id() << "\n";
                        return true;
                    };
                    auto suite_callback = [&](OP::utest::TestSuite& sui) {
                        std::cout << sui.id() << "\n";
                        sui.list_cases(case_callback);
                        return true;
                    };
                    OP::utest::TestRun::default_instance().list_suites(suite_callback);
                    std::exit(0);
                });
            processor
                .on("-d")
                .description("level of logging 1-3 (1=error, 2=info, 3=debug")
                .action<int>([&](int level){
                    opts.log_level(static_cast<OP::utest::ResultLevel>(level));
                })
                ;
            processor
                .on("-r")
                .description("<regexp> - regular expression to filter test cases. Regex is matched agains pattern <Test SuiteName>/<Test Case Name>")
                .action( [&](const std::string& arg){
                    std::regex expression(arg);

                    test_case_filter = [=](OP::utest::TestSuite& suite, OP::utest::TestCase& cs) {
                                std::string key = suite.id() + "/" + cs.id();
                                return std::regex_match(key, expression);
                            };
                });

            try
            {
                processor.parse(argc, argv);
            } catch(const std::invalid_argument& e)
            {
                std::cerr << e.what() << std::endl;
                return 1;
            } catch (std::regex_error & e)
            {
                std::cerr << "Invalid -r argument:" << e.what() << "\n";
                return 1;
            }
            OP::utest::TestRun::default_instance().options() = opts;
            auto all_result = 
                OP::utest::TestRun::default_instance().run_if(test_case_filter);
            std::map<std::string, size_t> summary_map;
            int status = 0;
            for (auto result : all_result)
            {
                if (!*result)
                    status = (int)result->status();
                auto agg = summary_map.emplace(result->status_to_str(), 0);
                ++agg.first->second;
            }
            std::cout << "==--Total run results--==:\n";
            //dump summary
            for( auto agg : summary_map )
            {
                std::cout << "\t" << agg.first << std::setfill('-')<<std::setw(10)<< ">(" << agg.second << ")\n";
            }
            return status;
        }
        } //ns:cmdline

    } //utest
}//OP
#endif //_CMDLN_UNIT_TEST__H_7fcb8d3d_790c_46ce_abf8_2216991b5b9c
