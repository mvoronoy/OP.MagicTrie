#pragma once
#ifndef _OP_UTEST_FRONTEND_JSONFROTEND__H_
#define _OP_UTEST_FRONTEND_JSONFROTEND__H_

#include <array>
#include <functional>
#include <fstream>
#include <iomanip>

#include <op/common/IoFlagGuard.h>
#include <op/common/Assoc.h>

#include <op/utest/unit_test.h>

namespace OP::utest::frontend
{
    namespace {// unnamed namespace to make aliases
        namespace plh = std::placeholders;
    }

    class JsonFrontend
    {
        using unsubscriber_t = typename UnitTestEventSupplier::unsubscriber_t;
        using event_time_t = typename UnitTestEventSupplier::event_time_t;
        using system_time_t = std::chrono::system_clock::time_point;

    public:
        using run_end_event_t = UnitTestEventSupplier::run_end_event_t;
        using suite_event_t = UnitTestEventSupplier::suite_event_t;
        using start_case_event_t = UnitTestEventSupplier::start_case_event_t;
        using end_case_event_t = UnitTestEventSupplier::end_case_event_t;
        using load_exec_event_t = UnitTestEventSupplier::load_exec_event_t;

        using colored_wrap_t = console::color_meets_value_t<std::string>;

        JsonFrontend(TestRun& run_env, std::ofstream&& json_file)
            : _run_env(run_env)
            , _json_file(std::move(json_file))
            , _unsubscribes{
                _run_env.event_supplier().bind<UnitTestEventSupplier::test_run_start>(
                    std::bind(&JsonFrontend::on_run_start, this/*, plh::_1*/)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::test_run_end>(
                    std::bind(&JsonFrontend::on_run_end, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::suite_start>(
                    std::bind(&JsonFrontend::on_suite_start, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::suite_end>(
                    std::bind(&JsonFrontend::on_suite_end, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::case_start>(
                    std::bind(&JsonFrontend::on_case_start, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::case_end>(
                    std::bind(&JsonFrontend::on_case_end, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::load_execute_warm>(
                    std::bind(&JsonFrontend::on_load_execute_warm, this, plh::_1)
                ),
                _run_env.event_supplier().bind<UnitTestEventSupplier::load_execute_run>(
                    std::bind(&JsonFrontend::on_load_execute_run, this, plh::_1)
                )
            }
        {
        }

    protected:

        template <class T>
        static std::string as_key(const T& key)
        {
            static constexpr char bumper[] = "\": ";
            std::string result;
            result
                .append(1, '"')
                .append(key)
                .append(bumper, sizeof(bumper) - 1);
            return result;
        }
            
        template <class T>
        static std::string as_value(const T& t)
        {
            std::ostringstream os;
            os << t;
            return std::move(os).str();
        }
            
        static std::string as_value(const char* t)
        {
            std::ostringstream os;
            os << std::quoted(t);
            return std::move(os).str();
        }

        static std::string as_value(const std::string& t)
        {
            std::ostringstream os;
            os << std::quoted(t);
            return std::move(os).str();
        }

        static std::string as_value(system_time_t time_point)
        {
            std::time_t as_time = std::chrono::system_clock::to_time_t(time_point);
            // Convert to gm time structure (std::tm)
            const system_time_t::duration tt = time_point.time_since_epoch();
            const time_t count_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tt).count();

            std::ostringstream os;

            os << "{ \"UTC\": \"" << std::put_time(std::gmtime(&as_time), "%Y-%m-%dT%H:%M:%S") << ":" << (count_ms%1000) << "Z" << "\" }";
            return std::move(os).str();
        }
        

        template <class Os, class Ratio>
        static void ratio_time(Os& os, Ratio)
        {
            os << "10^-" << Ratio::den << "s";
        }

        template <class Os>
        static void ratio_time(Os& os, std::milli)
        {
            os << "ms";
        }
                                       
        template <class Os>
        static void ratio_time(Os& os, std::micro)
        {
            os << "us";
        }
                                       
        template <class Os>
        static void ratio_time(Os& os, std::nano)
        {
            os << "ns";
        }
                                       
        template <class Rep, class Ratio>
        static std::string as_value(const std::chrono::duration<Rep, Ratio>& dur)
        {
            std::ostringstream os;
            
            ratio_time(os << "{ \"", Ratio{});
            os << "\": " 
                << std::fixed << std::setprecision(3) << dur.count()
                << " }";
            return std::move(os).str();
        }

        struct Hold final
        {
            std::string _hold;
            bool _sequence_element;

            Hold(std::string str, bool sequence_element) noexcept
                :_hold(std::move(str))
                , _sequence_element(sequence_element)
            {
            }

            bool sequence_element() const
            {
                return _sequence_element;
            }


            template <class Os>
            inline friend Os& operator << (Os& os, const Hold& zhis)
            {
                os << zhis._hold;
                return os;
            }
        };

        template <class V>
        auto kv(const std::string& str, const V& v) noexcept
        {
            Hold kv(as_key(str), true);
            kv._hold += as_value(v);
            return kv;
        }

        struct Ident final
        {
            constexpr static char symbol_c = ' ';
            constexpr static char step_c = 4;

            size_t* _global, _actual;
            size_t _sequence_size = 0;

            explicit Ident(size_t &global) noexcept
                : _global(&global)
            {
                _actual = *_global;
                *_global += step_c;
            }
            Ident(const Ident&) = delete;
            Ident(Ident&& other) 
                : _global(std::exchange(other._global, nullptr))
                , _actual(other._actual)
                , _sequence_size(other._sequence_size)
            {
            }

            ~Ident()
            {
                if(_global)
                    *_global = _actual;
            }

            template <class Os>
            inline friend Os& operator << (Os& os, const Ident& ident)
            {
                os << '\n';
                if (ident._actual)
                {
                    raii::IoFlagGuard stream_guard(os);
                    os << std::setfill(ident.symbol_c) << std::setw(ident._actual) << ident.symbol_c;
                }
                return os;
            }
        };

        auto typo(char seq) const noexcept
        {
            return Hold(std::string(1, seq), false);
        }

        OP_DECLARE_CLASS_HAS_MEMBER(sequence_element)

        template <class ...Tx>
        void formatter(const Tx& ... assoc)
        {
            auto& ident = _stateful_ident.back();
            auto place = [&](const auto& v) {
                using Type = std::decay_t<decltype(v)>;
                std::ostringstream os;
                os << v;
                auto str = std::move(os).str();
                if (std::empty(str))
                    return;
                if constexpr (has_sequence_element<Type>::is_invocable())
                {//has method: sequence_element()
                    if (v.sequence_element() && ident._sequence_size++)
                        _json_file << ", ";
                }
                _json_file << ident;
                _json_file << str;
            };
            (place(assoc), ...);
        };
        
        void ident(char seq = 0)
        {
            if(seq) 
                formatter(Hold(std::string(1, seq), true));
            Ident spc(_json_ident);
            _stateful_ident.emplace_back(std::move(spc));
        }

        void unindent(char seq = 0)
        {
            _stateful_ident.pop_back();
            if(seq)
                formatter(typo(seq));
        }


        static inline const char kw_suites[] = "suites";
        static inline const char kw_summary[] = "summary";
        static inline const char kw_id[] = "id";
        static inline const char kw_tags[] = "tags";
        static inline const char kw_start_time[] = "start_time";
        static inline const char kw_end_time[] = "end_time";
        static inline const char kw_duration[] = "duration";
        static inline const char kw_total[] = "total";
        static inline const char kw_test_cases[] = "cases";
        static inline const char kw_fixture[] = "fixture";
        static inline const char kw_status[] = "status";
        static inline const char kw_avg_duration[] = "avg_duration";
        static inline const char kw_run_number[] = "run_number";
        static inline const char kw_warm_up_cycles[] = "warm_up_cycles";
        static inline const char kw_measurement_cycles[] = "measurement_cycles";

        virtual void on_run_start(/*const dummy_event_t&*/)
        {
            _stateful_ident.emplace_back(Ident{ _json_ident }); //main sequence
            ident('{');
            formatter(kv(kw_suites, typo('[')));
            ident();//[
        }

        virtual void on_run_end(const run_end_event_t& payload)
        {
            auto& all_result = std::get<0>(payload);

            //put aggregated summary
            unindent(']');//close suite array
            formatter(
                kv(kw_summary, typo('{'))
                );
            ident(); //'{'
            // build summary duration
            std::chrono::duration<double, std::milli> duration(0);
            for (auto& result : all_result)
            {
                duration += result.duration();
            }
            formatter(
                kv(kw_duration, duration),
                kv(kw_total, all_result.size())
            );
            for(auto i = 0; i < _summary_aggregate_by_status.size(); ++i)
            {
                if( _summary_aggregate_by_status[i] > 0 )
                {
                    const auto& statstr = TestResult::status_to_str(static_cast<TestResult::Status>(i));
                    formatter(
                        kv(statstr, _summary_aggregate_by_status[i])
                    );
                }    
            }
            unindent('}');

            unindent('}');
            assert(_stateful_ident.size() == 1);
        }

        virtual void on_suite_start(const suite_event_t& payload)
        {
            TestSuite& suite = std::get<TestSuite&>(payload);
            _time_stack.push_back(std::get<event_time_t>(payload));

            system_time_t start = std::chrono::system_clock::now();

            ident('{');

            formatter(
                kv(kw_id, suite.id()),
                kv(kw_tags, '['));
            ident();
            for (const auto& tag : suite.tags())
                formatter(as_value(tag));
            unindent(']');
            formatter(
                kv(kw_start_time, start),
                kv(kw_test_cases, '['));
            ident();
        }

        virtual void on_suite_end(const suite_event_t& payload)
        {
            TestSuite& suite = std::get<TestSuite&>(payload);
            event_time_t end_time = std::get<event_time_t>(payload);

            unindent(']');
            formatter(
                kv(kw_duration, end_time - _time_stack.back())
            );
            _time_stack.pop_back();
            unindent('}');
        }

        virtual void on_case_start(const start_case_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            TestCase& tcase = std::get<TestCase&>(payload);
            TestFixture& fixture = std::get<TestFixture&>(payload);

            system_time_t start = std::chrono::system_clock::now();

            ident('{');

            formatter(
                kv(kw_id, tcase.id()),
                kv(kw_start_time, start),
                kv(kw_fixture, 
                    std::empty(fixture.id()) ? "<default>" : fixture.id().c_str()),
                kv(kw_tags, '[')
            );
            ident();
            for (const auto& tag : tcase.tags())
                formatter(as_value(tag));
            unindent(']');
        }

        virtual void on_case_end(const end_case_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            TestResult& result = std::get<TestResult&>(payload);
            //count aggregated summary
            ++_summary_aggregate_by_status[(size_t)result.status() % TestResult::status_size_c];
            formatter(
                kv(kw_status, TestResult::status_to_str(result.status())),
                kv(kw_duration, result.duration())
             );
            if(result.run_number() > 1)
            {
                formatter(
                    kv(kw_run_number, result.run_number()),
                    kv(kw_avg_duration, result.duration() / result.run_number())
                    );
            }
            unindent('}');
        }
        
        virtual void on_load_execute_warm(const load_exec_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            unsigned warm_up_cycles = std::get<unsigned>(payload);
            formatter(
                kv(kw_warm_up_cycles, warm_up_cycles)
             );

        }

        virtual void on_load_execute_run(const load_exec_event_t& payload)
        {
            TestRuntime& runtime = std::get<TestRuntime&>(payload);
            unsigned run_number = std::get<unsigned>(payload);
            formatter(
                kv(kw_measurement_cycles, run_number)
             );
        }

    private:


        TestRun& _run_env;
        std::ofstream _json_file;
        size_t _json_ident = 0;
        std::vector<Ident> _stateful_ident;
        std::vector<event_time_t> _time_stack;

        std::array<size_t, TestResult::status_size_c> _summary_aggregate_by_status{};

        std::array<unsubscriber_t, UnitTestEventSupplier::_event_code_count_> _unsubscribes;

    };

}//ns:OP::utest::frontend

#endif //_OP_UTEST_FRONTEND_JSONFROTEND__H_
