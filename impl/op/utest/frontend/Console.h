#pragma once
#ifndef _OP_UTEST_FRONTEND_CONSOLE__H_
#define _OP_UTEST_FRONTEND_CONSOLE__H_

#include <array>
#include <functional>

#include <op/common/IoFlagGuard.h>
#include <op/common/Console.h>

#include <op/utest/unit_test.h>

namespace OP::utest::frontend
{
    namespace {// unnamed namespace to make liases
        namespace plh = std::placeholders;
    }

    class ConsoleFrontend
    {
        using unsubscriber_t = UnitTestEventSupplier::unsubscriber_t;
    public:
        using suite_event_t = UnitTestEventSupplier::suite_event_t;
        using start_case_event_t = UnitTestEventSupplier::start_case_event_t;
        using end_case_event_t = UnitTestEventSupplier::end_case_event_t;
        using load_exec_event_t = UnitTestEventSupplier::load_exec_event_t;

        using colored_wrap_t = console::color_meets_value_t<std::string>;

        ConsoleFrontend(TestRun& run_env)
            : _run_env(run_env)
            , _unsubscribes( _run_env.event_supplier().make_unsub_guard(
                _run_env.event_supplier().bind<UnitTestEventSupplier::suite_start>(
                    std::bind(&ConsoleFrontend::on_suite_start, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::case_start>(
                    std::bind(&ConsoleFrontend::on_case_start, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::case_end>(
                    std::bind(&ConsoleFrontend::on_case_end, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::load_execute_warm>(
                    std::bind(&ConsoleFrontend::on_load_execute_warm, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::load_execute_run>(
                    std::bind(&ConsoleFrontend::on_load_execute_run, this, plh::_1)
                )
            ))
        {
        }

        [[nodiscard]] static colored_wrap_t status_to_colored_str(TestResult::Status status) noexcept
        {
            using fclr_t = colored_wrap_t(*)(const std::string&);
            const std::string& str = TestResult::status_to_str(status);
            static const fclr_t coloring_seq[] = {
                /*not_started*/
                console::esc<console::void_t>,
                /*failed*/
                console::esc<console::bright_yellow_t>,
                /*exception*/
                console::esc<console::red_t>,
                /*aborted*/
                console::esc<console::background_red_t>,
                /*ok*/
                console::esc<console::bright_green_t>
            };
            size_t delta = static_cast<size_t>(status) - static_cast<size_t>(TestResult::Status::_first_);
            return
                coloring_seq[delta % TestResult::status_size_c](str);
        }

    protected:

        virtual void on_suite_start(const suite_event_t& payload)
        {
            TestSuite& suite = std::get<TestSuite&>(payload);
            raii::IoFlagGuard stream_guard(suite.info());
            int title_width = static_cast<int>(_run_env.options().output_width()) - suite.id().length();
            if (title_width < 0)
                title_width = 0;
            suite.info()
                << "==[" << suite.id() << "]"
                << std::setfill('=')
                << std::setw(static_cast<std::streamsize>(title_width))
                << "" //need this to force setfill add separators
                << std::endl;
        }

        virtual void on_case_start(const start_case_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            TestCase& tcase = std::get<TestCase&>(payload);
            TestFixture& fixture = std::get<TestFixture&>(payload);

            raii::IoFlagGuard stream_guard(runtime.info());
            print_case_label(runtime.info() << "\t", tcase, fixture) <<"...\n";
        }

        virtual void on_case_end(const end_case_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            TestCase& tcase = std::get<TestCase&>(payload);
            TestResult& result = std::get<TestResult&>(payload);
            TestFixture& fixture = std::get<TestFixture&>(payload);

            raii::IoFlagGuard stream_guard(runtime.info());
            print_case_label(runtime.info() << "\t", tcase, fixture)
                    << " done with status:"
                    << "-=[" << status_to_colored_str(result.status())
                    << "]=-"
                    << " in:" 
                    << std::fixed << std::setprecision(3) 
                    << result.ms_duration() << "ms"
                ;
            if(result.run_number() > 1)
            {
                runtime.info() << ", (avg:"
                    << console::esc<console::bright_cyan_t>(result.avg_duration() ) << "ms)"
                    ;
            }
            runtime.info() <<'.' << std::endl //need flush
                ;
        }
        
        virtual void on_load_execute_warm(const load_exec_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            unsigned warm_up_cycles = std::get<unsigned>(payload);
            runtime.info() << "=> test is starting under loading...\n"
                << console::esc<console::blue_t>("\twarm-up cycles:(")
                << console::esc<console::bright_cyan_t>(warm_up_cycles)
                << console::esc<console::blue_t>(")...\n");
        }

        virtual void on_load_execute_run(const load_exec_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            unsigned run_number = std::get<unsigned>(payload);
            runtime.info()
                << console::esc<console::blue_t>("\tmeasurement cycles:(")
                << console::esc<console::bright_cyan_t>(run_number)
                << console::esc<console::blue_t>(")...\n");
        }

    private:
        template <class Os>
        static Os& print_case_label(
            Os& os, TestCase& tcase, TestFixture& fixture )
        {
            os << "[" << tcase.id();
            if(! std::empty(fixture.id()) )
                os << console::esc<console::magenta_t>("(") << fixture.id() << console::esc<console::magenta_t>(")");
            os << "]";
            return os;
        }

        TestRun& _run_env;
        typename UnitTestEventSupplier::unsub_guard_t _unsubscribes;

    };
}//ns:OP::utest::frontend

#endif //_OP_UTEST_FRONTEND_CONSOLE__H_
