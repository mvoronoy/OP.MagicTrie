#pragma once
#ifndef _OP_UNIT_TEST__H_
#define _OP_UNIT_TEST__H_

#include <utility>
#include <vector>
#include <deque>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <memory>
#include <chrono>
#include <ctime>
#include <sstream>
#include <string>
#include <typeinfo>
#include <functional>
#include <any>
#include <map>
#include <set>
#include <mutex>
#include <cstdint>
#include <csignal>
#include <random>
#include <optional>
#include <limits>

#include <op/utest/unit_test_is.h>
#include <op/utest/details.h>

#include <op/common/IoFlagGuard.h>
#include <op/common/ftraits.h>
#include <op/common/has_member_def.h>
#include <op/common/Currying.h>
#include <op/common/NullStream.h>
#include <op/common/EventSupplier.h>
#include <op/common/StringEnforce.h>

#if defined( _MSC_VER ) 
#if defined(max)
#undef max
#endif //max
#if defined(min)
#undef min
#endif //min
#endif //_MSC_VER


/**Render inline information like __FILE__, __LINE__ in lazy way (rendered only on demand) */
#define _OP_LAZY_INLINE_INFO(...) OP::utest::Details{ [&](std::ostream&os) { \
        os << "{File:" << __FILE__ << " at:" << __LINE__  << "} " __VA_ARGS__ << "\n"; \
    }}
#define OP_UTEST_DETAILS(...)  _OP_LAZY_INLINE_INFO(__VA_ARGS__)
/** Allows place useful information to details output.
* Usage:
* \code
* ...
* OP_CODE_DETAILS() << "Detail of exception with number " << 57;

* \endcode
*/
#define OP_CODE_DETAILS(...)  _OP_LAZY_INLINE_INFO(__VA_ARGS__)

#define OP_TEST_STRINGIFY(a) #a

/** Exposes assert functionality if for some reason function have no access to TestRuntime instance.
* Usage:
* \code
* ...
*   OP_UTEST_ASSERT(1==0, << "Logic is a power! Following number:" << 57);
*/
#define OP_UTEST_ASSERT(condition, ...) ([&](bool test)->void{ \
        if(!test){ auto msg ( std::move(OP_CODE_DETAILS( << OP_TEST_STRINGIFY(condition) << " - "  __VA_ARGS__ )));\
            OP::utest::_inner::_unconditional_exception_raise( msg.result() ); } \
    }(condition))

/**The same as OP_UTEST_ASSERT but unconditionally failed*/
#define OP_UTEST_FAIL(...) OP_UTEST_ASSERT(false,  __VA_ARGS__ )

namespace OP::utest
{
    /**
    *   Level of result logging
    */
    enum class ResultLevel : std::uint64_t
    {
        /**Display errors only*/
        error [[maybe_unused]] = 1,
        /**Display information messages and errors*/
        info = 2,
        /**Display all messages*/
        debug = 3
    };

    /** Options for load running */
    struct LoadRunOptions
    {
        /** Number of dummy exercise before real metrics measurement */
        size_t _warm_up = 10;

        /** Number of times to execute test for the real metrics measurement */
        size_t _runs = 1;
        
        [[nodiscard]] constexpr bool is_load_run() const noexcept
        {
            return _runs > 1;
        }
    };

    /** Encapsulate test suite configurable options */
    struct TestRunOptions
    {
        TestRunOptions() noexcept
            : _fail_fast{false}
            , _intercept_sig_abort{true}
            , _output_width{40}
        {
        }

        TestRunOptions& fail_fast(bool new_value) noexcept
        {
            _fail_fast = new_value;
            return *this;
        }

        [[nodiscard]] bool fail_fast() const noexcept
        {
            return _fail_fast;
        }

        /** Modifies permission to intercept 'abort' from test code.
         * Set true if C-style assert shouldn't break test execution
         */
        TestRunOptions& intercept_sig_abort(bool new_value) noexcept
        {
            _intercept_sig_abort = new_value;
            return *this;
        }

        [[nodiscard]] bool intercept_sig_abort() const noexcept
        {
            return _intercept_sig_abort;
        }

        [[nodiscard]] std::uint16_t output_width() const noexcept
        {
            return _output_width;
        }

        TestRunOptions& output_width(std::uint16_t output_width) noexcept
        {
            _output_width = output_width;
            return *this;
        }

        [[nodiscard]] ResultLevel log_level() const noexcept
        {
            return _log_level;
        }

        TestRunOptions& log_level(ResultLevel level) noexcept
        {
            _log_level = level;
            return *this;
        }
        
        /** Options for load runs */
        [[nodiscard]] LoadRunOptions load_run() const noexcept
        {
            return _load_run;
        }

        TestRunOptions& load_run(LoadRunOptions lro) noexcept
        {
            _load_run = std::move(lro);
            return *this;
        }

        /** Allows configure internal random generator to create reproducible results
         * If needed to get the same random values between several tests run specify
         * some constant for this parameter
         */
        [[nodiscard]] const auto& random_seed() const noexcept
        {
            return _random_seed;
        }

        template <class IntOrOptional>
        TestRunOptions& random_seed(IntOrOptional seed) noexcept
        {
            _random_seed = std::forward<IntOrOptional>(seed);
            return *this;
        }

    private:
        bool _fail_fast;
        bool _intercept_sig_abort;
        std::uint16_t _output_width;
        ResultLevel _log_level = ResultLevel::info;
        LoadRunOptions _load_run;
        std::optional<std::uint64_t> _random_seed;
    };

    namespace _inner {

        inline bool _unconditional_exception_raise(const std::string& x);

        /** Do nothing buffer */
        using null_buffer = OP::NullStreamBuffer;

    } //inner namespace

    /**Specialization of exception to distinguish fail from aborted state*/
    struct TestFail : std::logic_error
    {
        TestFail() :
            std::logic_error("fail")
        {
        }

        explicit TestFail( const std::string& text) :
            std::logic_error(text)
        {
        }
    };
    /**Demarcate abort exception. It is intentionally has no any inheritance*/
    struct TestAbort : public TestFail
    {
        explicit TestAbort(int level)
            : TestFail(std::string("abort triggered, level:") + std::to_string(level))
        {
        }
    };

    struct TestCase;
    struct TestFixture;
    struct TestSuite;
    struct TestRun;
    struct TestRuntime;

    struct Identifiable 
    {
        using tag_set_t = std::unordered_set<std::string>;

        explicit Identifiable(std::string id) noexcept
            : _id(std::move(id))
            {
            }

        virtual ~Identifiable() = default;

        const std::string& id() const noexcept
        {
            return _id;
        }

        tag_set_t& tags() noexcept
        {
            return _tags;
        }

        const tag_set_t& tags() const noexcept
        {
            return _tags;
        }

    private:
        std::string _id;
        tag_set_t _tags;
    };

    /** Helper class to append chain of additional checks after assert_exception was intercepted*/
    template <class Exception>
    struct AssertExceptionWrapper
    {
        using this_t = AssertExceptionWrapper<Exception>;
        AssertExceptionWrapper(TestRuntime& owner, Exception ex)
            : _owner(owner)
            , _ex(std::move(ex))
        {}

        /**
            Apply additional check 'f' to exception ex
            \param f predicate bool(const Exception& ) for additional exception check. Just return false to raise assert
        */
        template <typename F, typename ...Xetails>
        this_t& then(F f, Xetails&& ...details);
        
    private:
        TestRuntime& _owner;
        Exception _ex;
    };

    /** Result of test execution */
    struct TestResult
    {
        using clock_t = std::chrono::steady_clock;
        using time_point_t = clock_t::time_point;
        using duration_t = decltype(clock_t::now() - clock_t::now());

        friend struct TestCase;
        friend struct TestRun;

        explicit TestResult(const TestSuite& suite, const TestCase* test_case = nullptr) noexcept
            : _test_suite(suite)
            , _test_case(test_case)
            , _status(Status::not_started)
            , _run_number(0)
        {
        }

        [[nodiscard]] const TestSuite& test_suite() const noexcept
        {
            return _test_suite;
        }
        /** 
        * \brief represent optional (may be nullptr) test case that produced this result. 
        * Nullptr means that initialization/tear-down code of TestSuite has been failed, so no TestCase is related.
        */
        [[nodiscard]] const TestCase* test_case() const noexcept
        {
            return _test_case;
        }

        enum class Status : std::uint32_t
        {
            _first_ = 0,
            not_started = _first_,
            /**some test condition was not met*/
            failed,
            /**test raised unhandled exception*/
            exception,
            /*test signaled abort (for example using CLR assert())*/
            aborted,
            /*Test succeeded*/
            ok,
            _last_
        };
        constexpr static size_t status_size_c = 
            (static_cast<size_t>(Status::_last_) - static_cast<size_t>(Status::_first_));

        constexpr bool operator !() const noexcept
        {
            return _status != Status::ok;
        }
        
        [[nodiscard]] constexpr Status status() const noexcept
        {
            return _status;
        }
        
        [[nodiscard]] duration_t duration() const noexcept
        {
            return (_end_time - _start_time);
        }

        [[nodiscard]] double ms_duration() const noexcept
        {
            return std::chrono::duration<double, std::milli>(duration()).count();
        }

        [[nodiscard]] unsigned run_number() const noexcept
        {
            return _run_number;
        }

        [[nodiscard]] double avg_duration() const noexcept
        {
            return _run_number > 1 ? (ms_duration() / _run_number) : ms_duration();
        }


        [[nodiscard]] static const std::string& status_to_str(Status status) noexcept
        {
            static const std::string values[] = {
                "not started", "failed", "exception", "aborted", "ok"
            };
            return values[((size_t)status - (size_t)Status::_first_) % status_size_c];
        }
        [[nodiscard]] const std::string& status_to_str() const
        {
            return status_to_str(_status);
        }

    private:
        Status _status;
        unsigned _run_number;
        time_point_t _start_time, _end_time;
        const TestSuite& _test_suite;
        const TestCase* _test_case;
    };

    class UnitTestEventSupplier
    {
    public:
        using event_time_t = std::chrono::steady_clock::time_point;

        enum Code
        {
            test_run_start = 0,
            test_run_end,
            suite_start,
            suite_end,
            case_start,
            case_end,
            /** Notify that load-execution start warm-up cycle. Event parameter is 
            *   defined by #load_exec_event_t type, where unsigned parameter specifies number of expected warm-up calls.
            */
            load_execute_warm,
            /** Notify that load-execution finished warm-up cycle and starts real run. 
             *  Event parameter is defined by #load_exec_event_t type, where unsigned parameter specifies number 
             * of expected run calls.
             */
            load_execute_run,
            _event_code_count_
        };

        using dummy_event_t = std::tuple<>;
        using run_end_event_t = std::tuple<std::deque<TestResult>&>;
        using suite_event_t = std::tuple<TestSuite&, event_time_t>;
        using start_case_event_t = std::tuple<TestRuntime&, TestCase&, TestFixture&, event_time_t>;
        using end_case_event_t = std::tuple<TestRuntime&, TestFixture&, TestResult&, event_time_t>;
        using load_exec_event_t = std::tuple<TestRuntime&, TestResult&, unsigned>;


        using test_event_manager_t = OP::events::EventSupplier<
            Assoc<test_run_start, dummy_event_t>,
            Assoc<test_run_end, run_end_event_t>,
            Assoc<suite_start, suite_event_t>,
            Assoc<suite_end, suite_event_t>,
            Assoc<case_start, start_case_event_t>,
            Assoc<case_end, end_case_event_t>,
            Assoc<load_execute_warm, load_exec_event_t>,
            Assoc<load_execute_run, load_exec_event_t>
        >;
        
        using unsubscriber_t = test_event_manager_t::unsubscriber_t;

        template<Code code, class FHandler>
        unsubscriber_t bind(FHandler&& handler) 
        {
            return _supplier.on<code>(std::move(handler));
        }

        template<Code code, class TPayload>
        void send_event(TPayload payload)
        {
            _supplier.send<code>(payload);
        }

    private:
        test_event_manager_t _supplier;
    };

    namespace tools //forward decl
    {
        struct RandomGenerator;
    }//ns:tools

    /** Expose operations and runtime environment to support unit tests */
    struct TestRuntime
    {
        friend struct TestCase;
        friend struct TestRun;

        TestRuntime(
            TestSuite& suite, TestCase& test_case, TestRunOptions run_options)
            : _suite(suite)
            , _access_result(new std::recursive_mutex())
            , _test_case(test_case)
            , _run_options(std::move(run_options))
        {
        }

        TestRuntime(const TestRuntime&) = delete;

        [[maybe_unused]] [[nodiscard]] TestSuite& suite() const noexcept
        {
            return _suite;
        }

        const TestCase& test_case() const noexcept
        {
            return _test_case;
        }

        const TestRunOptions& run_options() const noexcept
        {
            return _run_options;
        }

        /**
        *  Provide access to error-level log.
        */
        inline std::ostream& error() const;
        /**
        *  Provide access to information-level log. It may be dummy (full functional but
            ignoring any output) if TestRunOptions specifies log level below `info`
        */
        inline std::ostream& info() const;

        /**
        *  Provide access to debug-level log. It may be dummy (full functional but
            ignoring any output) if TestRunOptions specifies log level below `debug`
        */
        inline std::ostream& debug() const;

        void assert_true(bool condition)
        {
            assert_true(condition, "assert_true(false)");
        }

        template <class Xetails>
        void assert_true(bool condition, Xetails&& details)
        {
            if (!condition)
            {
                fail(std::forward<Xetails>(details));
            }
        }

        void assert_false(bool condition)
        {
            assert_true(!condition, "assert_false(true)");
        }

        template <class Xetails>
        void assert_false(bool condition, Xetails&& details)
        {
            assert_true(!condition, std::forward<Xetails>(details));
        }

        /**
        *   Most generic way to check result and assert. Method takes template parameter Marker
        *   then applied multiple Args to it. Marker can be custom or one of the standard existing in
         *  the namespace `OP::utest`:
        * \li equals/not_equals - to assert equality of 2 comparable arguments (including STL containers);

        * \li almost_eq - for floating types allows compare equality with neglection precision specified by custom or default `std::numeric_limits::epsilon()`.
        * \li eq_sets - to assert equality of 2 arbitrary containers but with items that can be
        *               automatically compared (by `==` comparator). Both containers are also checked against same items order.
        * \li eq_unordered_sets - the same as `eq_set`, but ignores the order of items (uses std::unordered_set inside to collect items,
        *       so item must be not only comparable (`==`) but support `std::hash<>` as well).
        * \li eq_ranges - to assert equality of 2 arbitrary containers specified by pair of iterators.
        *                    Both ranges are also checked against same items order.
        * \li less/greater/greater_or_equals/less_or_equals - to assert comparison relation between 2 args
        
        * \li negate or logical_not to invert other Markers. For example `assert_that< negate<less> >(3, 2)` - evaluates
        *               `!(3 < 2)` that is effectively `3>=2`
        * \li is_null/is_not_null - to assert arg is nullptr
        * \li regex_match to test if one string-like argument matches to the specified `std::regex` expression.
        * \li regex_search to test if one string-like argument contains specified `std::regex` expression.
        */
        template<const auto& AssertOperation, class ...Args>
        void assert_that(Args&& ...args)
        {
            using Marker = std::decay_t<decltype(AssertOperation)>;
            //consciously don't use make_tuple to have reference to argument
            auto pack_arg = std::forward_as_tuple(std::forward<Args>(args)...);
            auto that_result = details::apply_prefix(AssertOperation, pack_arg, std::make_index_sequence<Marker::args_c>());
            bool succeeded;
            if constexpr (std::is_convertible_v<std::decay_t<decltype(that_result)>, bool>)
                succeeded = that_result;
            else
                succeeded = std::get<bool>(that_result);

            if (!succeeded)
            { //predicate failed, print fail details
                auto bind_fail = [&](auto&& ... t) {
                    if constexpr (!std::is_convertible_v<std::decay_t<decltype(that_result)>, bool>)
                    { //predicate result containing additional details to print

                        fail(std::get<OP::utest::Details>(that_result),
                            std::forward<decltype(t)>(t)...);
                    }
                    else //predicate is plain bool
                        fail(std::forward<decltype(t)>(t)...);
                };
                //need pass argument to fail
                details::apply_rest< Marker::args_c>(bind_fail, pack_arg,
                    std::make_index_sequence<(sizeof ... (Args)) - Marker::args_c>());
            }
        }

        /**
        *   Assert that functor `f` raises exception of expected type `Exception`. The method only checks
        * that exception was raised, for extra detailed analyzes you can leverage `then` method of 
        * AssertExceptionWrapper, for example:
        * \code
        * tresult.assert_exception<std::logic_error>(
        *       [](){ throw std::logical_error("paradox"); }), "Exception must be raised there")
        *       .then([&](const auto& ex){tresult.assert_that<equals>(ex.what(), std::string("paradox"));}
        * \endcode
        * \tparam Exception type that is expected during function execution. Test case is succeeded 
        * if this type (or inherited) is raised.
        * \tparam Xetails allows specify printable details of failed case (why the exception has not been raised).
        * \param f function that should raise the exception of type `Exception`
        */
        template <class Exception, class F, typename ...Xetails>
        AssertExceptionWrapper<Exception> assert_exception(F f, Xetails&& ...details)
        {
            try
            {
                f();
            }
            catch (const Exception& ex)
            {
                debug() << "intercepted exception: " << typeid(Exception).name();
                if constexpr (std::is_base_of_v<std::exception, Exception>)
                    debug() << ": " << ex.what();
                debug() << "... caught as expected\n";
                return AssertExceptionWrapper<Exception>(*this, ex);
            }
            fail(std::forward<Xetails>(details)...);
            throw 1; //fake line to avoid warning about return value. `fail` will unconditionally raise the exception
        }

        /** Unconditional fail.
        * \tparam Xetails allows specify printable details of failed case (why the exception has not been raised).
        * \throws TestFail exception
        */
        template<typename ...Xetails>
        void fail(Xetails&& ...details)
        {
            guard_t g(*_access_result);
            auto one_step_print = [&](auto&& item)
            {
                if constexpr (std::is_invocable_v<decltype(item)>)
                    error() << item();
                else
                    error() << item;
            };
            (one_step_print(std::forward<Xetails>(details)), ...);
            fail();
        }

        static void fail()
        {
            throw TestFail();
        }

        /** Run callback functor `f` several times and measure median time. To reduce 
        * influence of statistical outlier method can run `f` several times to warm-up.
        * \param f callback to measure;
        * \param repeat number of controlled runs of `f`;
        * \param warm_up number of runs before measurement to warm-up runtime environment (0 is 
        *   allowed as well);
        * \return average time in milliseconds of all runs (excluding warm-up).
        */
        template <class F>
        double measured_run(F f, size_t repeat = 10, size_t warm_up = 2)
        {
            while(warm_up--)
                f();

            double sum = 0.;
            for(size_t i = 0; i < repeat; ++i)
            {
                std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                f();
                std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
                sum += std::chrono::duration<double, std::milli>(finish - start).count();
            }
            return sum / repeat;
        }
        
        /** Accessor to pre-configured random generator */
        inline tools::RandomGenerator& randomizer() const;

    private:
        TestSuite& _suite;
        TestCase& _test_case;

        typedef std::unique_ptr<std::recursive_mutex> mutex_ptr_t;
        mutex_ptr_t _access_result;
        typedef std::unique_lock<mutex_ptr_t::element_type> guard_t;

        _inner::null_buffer _null_buffer;
        mutable std::ostream _null_stream{ &_null_buffer };
        TestRunOptions _run_options;
    };

    using namespace OP::currying;

    using test_run_shared_args_t = CurryingTuple<
        // Current instance of TestRuntime
        Var<TestRuntime>,
        // Current instance of TestSuite
        Var<TestSuite>,
        // Current instance of TestRun
        Var<TestRun>,
        // Shared state from `TestFixture`
        UnpackAny >;

    /** Abstract definition of single test */
    struct TestCase : Identifiable
    {
        explicit TestCase(std::string name) noexcept
            : Identifiable(std::move(name))
        {
        }

        virtual ~TestCase() = default;

        TestCase(const TestCase&) = delete;
        TestCase(TestCase&&) = delete;

        TestResult execute(test_run_shared_args_t& runtime, const TestRunOptions& options)
        {
            if (options.load_run().is_load_run())
                return load_execute(runtime, options.load_run()._runs, options.load_run()._warm_up);
            else
                return single_execute(runtime);
        }

    protected:
        /** Invoke test once */
        TestResult single_execute(test_run_shared_args_t& run_args)
        {
            auto& suite = run_args.eval_arg<TestSuite>();
            TestResult retval(suite, this);
            isolate_exception_run(run_args, retval);
            retval._run_number = 1;
            return retval;
        }

        /**
        *   Start same test multiple times.
        *   @param result - accumulate results of all execution into single one. At exit this parameter
        *           contains summary time execution (without warm-up) and status of last executed test
        *   @param run_number - number of times to execute test-case
        *   @param warm_up - some number of executions before measure time begins. Allows
         *                  warm-up CPU, internal cache and so on...
        */
        inline TestResult load_execute(test_run_shared_args_t& run_args, unsigned run_number, unsigned warm_up);
        
        /** Conduct particular test */
        virtual void run(test_run_shared_args_t& retval) = 0;

    private:
        template <class Exception>
        static inline void render_exception_status(test_run_shared_args_t& runtime, Exception const& e)
        {
            const TestRuntime& tr = runtime.eval_arg<TestRuntime&>();
            if (e.what() && *e.what())
            {
                tr.error() << e.what();
            }
            else
            {
                tr.error() << "Exception of type:[" << typeid(Exception).name() << "] without details.";
            }
        }
        // apply RAII to set end test time
        struct SetEndTime
        {
            explicit SetEndTime(TestResult& result): _result(result)
            {
                _result._start_time = std::chrono::steady_clock::now();
            }
            ~SetEndTime()
            {
                _result._end_time = std::chrono::steady_clock::now();
            }
            TestResult& _result;
        };

        void isolate_exception_run(test_run_shared_args_t& runtime, TestResult& retval)
        {
            try
            {
                SetEndTime end_time_setup(retval); //grant assign of _end
                this->run(runtime);
                retval._status = TestResult::Status::ok;
            }
            catch (TestAbort const& e)
            {
                retval._status = TestResult::Status::aborted;
                render_exception_status(runtime, e);
            }
            catch (TestFail const& e)
            {
                retval._status = TestResult::Status::failed;
                render_exception_status(runtime, e);
            }
            catch (std::exception const& e)
            {
                retval._status = TestResult::Status::exception;
                render_exception_status(runtime, e);
            }
            catch (...)
            { //stop propagation of any other exception
                retval._status = TestResult::Status::exception;
            }
        }
    };


    /**
    * \brief Abstract interface representing a test fixture (stateful run environment) for a TestSuite.
    *
    * TestFixture defines a lifecycle contract for providing shared setup and tear-down
    * logic for a group of test cases within a TestSuite. A fixture instance is responsible
    * for preparing resources before test execution and releasing them afterward.
    *
    * The fixture lifecycle consists of:
    * - \ref setup — invoked before executing a test bundle to construct and return
    *   fixture-specific state.
    * - \ref tear_down — invoked after test execution to clean up resources associated
    *   with the previously created state.
    *
    * The state produced by \ref setup is returned as a \c std::any object, allowing
    * arbitrary user-defined types to be stored. This value may later be injected into
    * test cases or passed to the tear-down phase.
    *
    * TestFixture instances are non-copyable and non-movable to ensure strict ownership
    * and a well-defined lifecycle during test execution.
    */
    struct TestFixture : Identifiable
    {
        const static inline std::string no_name{};

        TestFixture() 
            : Identifiable(no_name) 
        {
        };

        explicit TestFixture(std::string id) noexcept
            : Identifiable(std::move(id))
        {
        }

        TestFixture(const TestFixture&) = delete;
        TestFixture& operator = (const TestFixture&) = delete;
        TestFixture(TestFixture&&) = delete;
        TestFixture& operator = (TestFixture&&) = delete;

        virtual ~TestFixture() = default;

        /**
        * \brief Prepare fixture state before executing a test bundle.
        *
        * This method is called once per configuration registered in the owning TestSuite.
        * Implementations should allocate and initialize all resources required by test cases.
        *
        * \param suite Reference to the owning TestSuite.
        * \return A \c std::any object containing fixture-specific state.
        *         The returned value may later be injected into test cases and is guaranteed
        *         to be passed to \ref tear_down.
        */
        virtual std::any setup(TestSuite&) = 0;

        /**
        * \brief Clean up fixture state after test execution.
        *
        * This method is called after all test cases associated with the fixture configuration
        * have finished execution.
        *
        * \param suite Reference to the owning TestSuite.
        * \param state The fixture state previously returned by \ref setup.
        */
        virtual void tear_down(TestSuite&, std::any&) noexcept = 0;
    };

    namespace details
    {
        /** RAII helper to ensure that tear-down method of TestFixture has been called in any case.
        */
        struct FixtureRAII
        {

            FixtureRAII(TestSuite& owner, TestFixture& fixture) noexcept
                : _owner(owner)
                , _fixture(fixture)
            {
            }

            ~FixtureRAII()
            {
                if(_has_value)
                    _fixture.tear_down(_owner, _shared_state);
            }

            void setup() noexcept
            {
                _shared_state = std::move(_fixture.setup(_owner));
                _has_value = true;
            }

            bool has_value() const noexcept
            {
                return _has_value;
            }

            std::any& value() noexcept
            {
                return _shared_state;
            }

        private:

            bool _has_value = false;
            TestSuite& _owner;
            TestFixture& _fixture;
            std::any _shared_state;
        };
        /** RAII to ensure start/end suite event bee sent */
        struct SendSuiteEventRAII
        {
            UnitTestEventSupplier& _event_supplier;
            TestSuite& _suite;
            size_t _started = 0;

            SendSuiteEventRAII(TestSuite& suite, UnitTestEventSupplier& event_supplier)
                : _suite(suite)
                , _event_supplier(event_supplier)
            {
            }

            void start()
            {
                ++_started;
                _event_supplier
                    .send_event<UnitTestEventSupplier::suite_start>(
                        UnitTestEventSupplier::suite_event_t(_suite, std::chrono::steady_clock::now())
                    );
            }

            ~SendSuiteEventRAII()
            {
                if (_started)
                {
                    _event_supplier
                        .send_event<UnitTestEventSupplier::suite_end>(
                            UnitTestEventSupplier::suite_event_t(_suite, std::chrono::steady_clock::now())
                        );
                }
            }

        };

    }//ns:details

    /**
    * \brief Logical container for grouping and executing related test cases.
    *
    * TestSuite represents a collection of test cases that share a common purpose,
    * lifecycle, or execution context. Each suite is uniquely identified by its name
    * and may be selectively executed using this name or tags.
    *
    * Test cases are registered into the suite via the `declare()` method.
    *
    * ### Fixtures / Configurations
    * A TestSuite may define one or more execution configurations using
    * `with_fixture()`. Each registered fixture provides a setup/teardown 
    * context for executing the entire suite.
    *
    * Execution behavior:
    * - If no fixture is registered, all test cases are executed once.
    * - If exactly one fixture is registered, all test cases are executed once
    *   under that fixture.
    * - If multiple fixtures are registered, the entire bundle of test cases is
    *   executed once per fixture.
    *
    * This allows the same logical test set to be validated under multiple
    * configurations without duplicating test declarations.
    *
    * ### Tagging
    * TestSuite supports tagging, enabling conditional execution (e.g. filtering
    * by feature, environment, or test category).
    *
    * The class is designed for C++17 and does not impose any threading or execution
    * policy by itself; execution semantics are controlled by the surrounding test
    * runner.
    */    
    class TestSuite : public Identifiable
        //public std::enable_shared_from_this<TestSuite>
    {
    public:
        using this_t = TestSuite;
        using test_executor_ptr = std::shared_ptr<TestCase>;
        using test_container_t = std::deque<test_executor_ptr>;
        using iterator = typename test_container_t::iterator;

        TestSuite(
            std::string name, 
            std::ostream& info_stream, std::ostream& error_stream) noexcept
            : Identifiable(std::move(name))
            , _info_stream(info_stream)
            , _error_stream(error_stream)
        {
        }

        /**
         *  Register a test case in this suite to execute.
         * \param name - Symbolic name of the case inside the suite.
         * \param f - Functor to execute during the test run. Input parameters for the functor
         *            may be empty or any combination, in any order, of the following
         *            types:
         *               - TestSuite& - Accepts this instance of the test suite.
         *               - TestRuntime& - Accepts a utility class with multiple useful
         *                   logging and assert methods.
         *               - `std::any` or a user type with value constructed inside `#before_suite`.
         * \param tags - Optional list of string tags that allows starting or filtering out cases at runtime. For example,
         *               create a tag "long" for long-running tests and then exclude them for quick smoke testing.
         */
        template <class F, class ...Tags>
        TestSuite& declare(std::string name, F f, Tags...tags )
        {
            std::unique_ptr<TestCase> test(
                new FunctionalTestCase<F>(
                    std::move(name),
                    std::move(f)
                ));
            auto& test_tags = test->tags();
            ((test_tags.emplace(tags)), ...);
            return this->declare_case(std::move(test));
        }

        /**
        * \brief Register a no name test case in this suite to execute.
        * Method automatically assigns numeric based name.
        */
        template <class F>
        TestSuite& declare(F f)
        {
            std::string name = "case#" + std::to_string(_tests.size()+1);
            return this->declare(name, std::move(f));
        }

        template <class F>
        TestSuite& declare_disabled(const std::string&, F )
        {
            return *this;
        }

        template <class F>
        TestSuite& declare_exceptional(std::string name, F f)
        {
            return this->declare_case(
                std::unique_ptr<TestCase> (new AnyExceptionTestCase<F>(
                    std::move(name),
                    std::move(f)
                ))
            );
        }

        TestSuite& declare_case(std::unique_ptr<TestCase> exec)
        {
            _tests.emplace_back(std::move(exec));
            return *this;
        }

        /**
        *  \brief Register a fixture providing setup and optional teardown logic
        *        for executing the entire test suite.
        *
        * A fixture defines an execution context under which all test cases in the
        * suite will be executed. The method may be called multiple times to register
        * multiple fixtures; in that case, the full bundle of test cases is executed
        * once per registered fixture.
        *
        * ### Setup function
        * The setup functor is mandatory and must return a `std::any` object that
        * represents the fixture state. This value is passed to the corresponding
        * teardown function (if provided).
        *
        * Supported setup signatures:
        * - `std::any ()`
        * - `std::any (TestSuite&)`
        *
        * ### Tear-down function
        * The tear-down functor is optional. When provided, it is invoked after all
        * test cases in the suite have completed execution for the given fixture.
        *
        * Supported tear-down signatures:
        * - `void (std::any&)`
        * - `void (std::any&, TestSuite&)`
        *
        * ### Execution semantics
        * - If no fixture is registered, test cases are executed once without any setup.
        * - If exactly one fixture is registered, test cases are executed once under
        *   that fixture.
        * - If multiple fixtures are registered, the entire suite is executed once per
        *   fixture in registration order.
        *
        * The returned `std::any` from setup is guaranteed to be passed unchanged to
        * the corresponding tear-down invocation.
        *
        * The value contained in std::any returned by the setup function may optionally be passed to test cases.
        * When a test case declares an arbitrary argument, the framework attempts to cast the fixture value
        * to the requested type and inject it into the test. For example, a test case declared as
        * `void my_test_case(const VeryExpensiveClass&)` will receive the stored fixture object by reference.
        * If the type does not match, the framework throws `std::bad_any_cast `.
        *
        * \tparam FSetup    Callable type implementing setup logic.
        * \tparam FTearDown Callable type implementing tear-down logic.
        *
        * \param setup_function Setup callable returning `std::any`.
        * \param tear_down    Tear-down callable consuming the setup result.
        *
        * \return Reference to the TestSuite instance to allow fluent chaining.
        *
        * ### Example:
        * \code
        * static auto module_suite = OP::utest::default_test_suite("ExpensiveInitSuite")
        *   .with_fixture(
        *           []() -> std::any { return std::any(std::in_place_type<VeryExpensiveClass>);},
        *           [](std::any& previous){
        *                   // some clean-up code
        *           })
        *   .declare("basic", [](const VeryExpensiveClass& shared_across_many_cases){ ... })
        *
        * \endcode
        */
        template <class FSetup, class FTearDown, 
            std::enable_if_t<!std::is_constructible_v<std::string, FSetup>, int> = 0>
        [[maybe_unused]] TestSuite& with_fixture(FSetup&& setup_function, FTearDown&& tear_down_function)
        {
            using fixture_impl_t = FunctionalPairTestFixture<FSetup, FTearDown>;
            return with_fixture(
                std::shared_ptr<TestFixture>(new fixture_impl_t(std::move(setup_function), std::move(tear_down_function)))
            );
        }

        template <class FSetup, class FTearDown>
        [[maybe_unused]] TestSuite& with_fixture(std::string fixture_name, FSetup&& setup_function, FTearDown&& tear_down_function)
        {
            using fixture_impl_t = FunctionalPairTestFixture<FSetup, FTearDown>;
            return with_fixture(
                std::shared_ptr<TestFixture>(new fixture_impl_t(
                    std::move(fixture_name), std::move(setup_function), std::move(tear_down_function)))
            );
        }

        template <class FSetup, std::enable_if_t<!std::is_constructible_v<std::string, FSetup>, int> = 0>
        [[maybe_unused]] TestSuite& with_fixture(FSetup&& init)
        {
            return with_fixture(
                std::shared_ptr<TestFixture>(new InitOnlyTestFixture<FSetup>(std::move(init)))
            );
        }

        template <class FSetup>
        [[maybe_unused]] TestSuite& with_fixture(std::string fixture_name, FSetup&& init)
        {
            return with_fixture(
                std::shared_ptr<TestFixture>(new InitOnlyTestFixture<FSetup>(std::move(fixture_name), std::move(init)))
            );
        }

        [[maybe_unused]] TestSuite& with_fixture(std::shared_ptr<TestFixture> fixture)
        {
            _fixtures.emplace_back(std::move(fixture));
            return *this;
        }

        /**Enumerate all test cases without run
        * \sa end()
        */
        iterator begin() const
        {
            return const_cast<test_container_t&>(_tests).begin();
        }
        
        /**Enumerate all test cases without run
        * \sa begin()
        */
        iterator end() const
        {
            return const_cast<test_container_t&>(_tests).end();
        }
        
        std::ostream& info()
        {
            return _info_stream;
        }
        
        std::ostream& error()
        {
            return _error_stream;
        }
        
        std::ostream& debug()
        {
            return info();
        }
        
        template<class Predicate>
        std::deque<TestResult> run_if(Predicate& predicate, TestRun& run_env)
        {
            std::deque<TestResult> result;
            if (_fixtures.empty())
            {
                DummyTestFixture dummy;
                run_if(predicate, dummy, run_env, result);
            }
            else
            {
                for (auto& fixture : _fixtures)
                    if (!run_if(predicate, *fixture, run_env, result))
                        break;
            }
            return result;
        }

        template <class Predicate>
        std::deque<TestResult> load_run(Predicate& predicate);

    private:

        struct DummyTestFixture : TestFixture
        {
            virtual std::any setup(TestSuite&)
            { /*do nothing*/ 
                return std::any{};
            }

            virtual void tear_down(TestSuite&, std::any&) noexcept
            { /*do nothing*/ }
        };

        template <class FInit>
        struct InitOnlyTestFixture : TestFixture
        {
            FInit _init;
            explicit InitOnlyTestFixture(FInit&& init) noexcept
                : _init(std::move(init))
            {
            }
            
            InitOnlyTestFixture(std::string fixture_name, FInit&& init) noexcept
                : TestFixture(std::move(fixture_name))
                , _init(std::move(init))
            {
            }

            virtual std::any setup(TestSuite& suite) override
            { 
                if constexpr (std::is_invocable_v<FInit, TestSuite&>)
                    return _init(suite);
                else
                    return _init();
            }

            virtual void tear_down(TestSuite&, std::any&) noexcept override
            { /*do nothing*/
            }
        };

        template <class FInit, class FTearDown>
        struct FunctionalPairTestFixture : InitOnlyTestFixture<FInit>
        {
            FTearDown _tear_down;

            FunctionalPairTestFixture(std::string fixture_name, FInit&& init, FTearDown&& tear_down) noexcept
                : InitOnlyTestFixture<FInit>(std::move(fixture_name), std::move(init))
                , _tear_down(std::move(tear_down))
            {
            }

            FunctionalPairTestFixture(FInit&& init, FTearDown&& tear_down) noexcept
                : InitOnlyTestFixture<FInit>(std::move(init))
                , _tear_down(std::move(tear_down))
            {
            }

            virtual void tear_down(TestSuite& suite, std::any& shared_state) noexcept override
            { 
                OP::currying::arguments(
                    std::ref(suite), UnpackAny(&shared_state))
                    .invoke(_tear_down);
            }
        };

        template <class Predicate>
        bool run_if(Predicate& predicate, TestFixture& fixture, TestRun& run_env, std::deque<TestResult>& result);

        test_container_t _tests;
        std::ostream& _info_stream;
        std::ostream& _error_stream;
        std::vector<std::shared_ptr<TestFixture>> _fixtures;
        template <class F>
        struct FunctionalTestCase : public TestCase
        {
            FunctionalTestCase(std::string&& name, F f) :
                TestCase(std::move(name)),
                _function(std::move(f))
            {
            }

        protected:
            void run(test_run_shared_args_t& retval) override
            {
                retval.invoke(_function);
            }

        private:
            F _function;
        };

        /**Handle test case that raises an exception*/
        template <class F>
        struct AnyExceptionTestCase : public FunctionalTestCase<F>
        {
            AnyExceptionTestCase(std::string&& name, F f) :
                FunctionalTestCase<F>(std::move(name), std::move(f))
            {
            }
        protected:
            void run(test_run_shared_args_t& retval) override
            {
                try
                {
                    FunctionalTestCase<F>::run(retval);
                    throw TestFail("exception was expected");
                }
                catch (...) {
                    //normal flow
                    retval.eval_arg<TestRuntime&>().debug() << "exception was raised as expected\n";
                }
            }
        };

    };

    struct TestRun
    {
        using test_suite_ptr = std::shared_ptr<TestSuite>;

        explicit TestRun(TestRunOptions options = TestRunOptions())
            : _options(options)
        {
        }
        
        static TestRun& default_instance()
        {
            static TestRun instance;
            return instance;
        }
        
        TestSuite& declare(test_suite_ptr suite)
        {
            return *_suites.emplace(suite->id(), std::move(suite))->second;
        }
        
        TestRunOptions& options()
        {
            return _options;
        }

        UnitTestEventSupplier& event_supplier() noexcept
        {
            return _event_supplier;
        }
        
        /**
        *   Just enumerate all test-suites without run
        * @tparam F predicate that matches to signature `bool F(TestSuite&)`
        */
        template <class F>
        void list_suites(F& f)
        {
            for (auto& p : _suites)
            {
                if (!f(*p.second))
                    return;
            }
        }

        std::shared_ptr<TestSuite> get_suite(const std::string& name) const
        {
            using namespace std::string_literals;
            if (auto found = _suites.find(name); found != _suites.end())
            {
                return found->second;
            }
            throw std::runtime_error("Unknown suite: '"s + name + "'");
        }

        std::shared_ptr<TestSuite> find_suite(const std::string& name) const
        {
            if (auto found = _suites.find(name); found != _suites.end())
            {
                return found->second;
            }
            return {};
        }

        [[maybe_unused]] std::deque<TestResult> run_all()
        {
            return run_if([](TestSuite&, TestCase&, TestFixture&) { return true; });
        }

        /**
        * Run all tests that match to the specified predicate.
        *
        * \tparam Predicate predicate that matches to signature `bool F(TestSuite&, TestCase&)`
        */
        template <class Predicate>
        std::deque<TestResult> run_if(Predicate predicate)
        {
            sig_abort_guard guard;
            std::deque<TestResult> result;
            _event_supplier
                .send_event<UnitTestEventSupplier::test_run_start>(
                    UnitTestEventSupplier::dummy_event_t{}
                );

            for (auto& suite_decl : _suites)
            {
                if (!run_suite(*suite_decl.second, predicate, result))
                    break;
            }
            _event_supplier
                .send_event<UnitTestEventSupplier::test_run_end>(
                    UnitTestEventSupplier::run_end_event_t{ result }
                );
            return result;
        }
        
    private:

        static TestResult handle_environment_crash(TestSuite& suite, const std::exception* pex)
        {
            TestResult crash_res(suite);
            crash_res._status = TestResult::Status::exception;
            suite.error() << "Unhandled exception, execution stopped.\n";
            if (pex)
                suite.error() << "----[exception-what]:" << pex->what() << '\n';
            return crash_res;
        }

        /**Helper RAII to intercept all standard C++ `abort` signals*/
        struct sig_abort_guard
        {
            sig_abort_guard()
            {
                _prev_handler = signal(SIGABRT, my_handler);
            }
            ~sig_abort_guard()
            {
                signal(SIGABRT, _prev_handler);
            }
            static void my_handler(int level)
            {
                throw TestAbort(level);
            }
            void(*_prev_handler)(int);
        };

        /** \brief Run single suite.
        * \return true for normal flow or false if failed-fast and overall run must be stopped immediately.
        */ 
        template <class Predicate>
        bool run_suite(TestSuite& suite, Predicate& predicate, std::deque<TestResult>& result)
        {
            bool has_error = false;
            try
            { // This try allows intercept exceptions only from suite initialization
                auto single_res = suite.run_if(predicate, *this);
                for (auto& res_to_move : single_res)
                {
                    result.emplace_back(std::move(res_to_move));
                    if (!res_to_move)
                        has_error = true;
                }
            }
            catch (const std::exception& ex)
            {
                result.push_back(handle_environment_crash(suite, &ex));
                has_error = true;
            }
            catch (...)
            {
                result.push_back(handle_environment_crash(suite, nullptr));
                has_error = true;
            }
            return !has_error;
        }


        using suites_t = std::multimap<std::string, std::shared_ptr<TestSuite> >;
        UnitTestEventSupplier _event_supplier;
        suites_t _suites;
        TestRunOptions _options;
    };

    namespace _inner
    {
        inline bool _unconditional_exception_raise(const std::string& x)
        {
            throw OP::utest::TestFail(x);
        }
    }
    
    namespace tools
    {
        /** Thread safe wrapper for centralized unit-test level random generation
         *  It is designed as a singleton, so use RandomGenerator::instance()
         *  By default class initialized by current time milliseconds, but it can
         *  be anytime re-seeded with help of #reseed method.
         */
        struct RandomGenerator
        {
            RandomGenerator()
                : _gen(std::random_device{}())
            {
            }

            static RandomGenerator& instance()
            {
                static RandomGenerator singlet;
                return singlet;
            }

            /** Allows reset current state of generator with concrete seed,
             * may be used to create reproducible tests results
             */
            void seed(size_t s)
            {
                std::lock_guard l(_gen_acc);
                _gen.seed(static_cast<std::mt19937::result_type>(s));
            }

            /**
             * Renders uniform distributed number in range [low, high)
             * @tparam Int type to generate
             * @param low minimal generated value
             * @param high excluding upper range (never generated)
             * @return
             */
            template <class Int>
            Int next_in_range(Int low, Int high)
            {
                std::uniform_int_distribution<Int> pick(low, high - 1);
                std::lock_guard l(_gen_acc);
                return pick(_gen);
            }
    
            template <class Int>
            Int next_in_range()
            {
                return next_in_range<Int>(std::numeric_limits<Int>::min(),
                                     std::numeric_limits<Int>::max());
            }
            
            template <class StrLike>
            inline StrLike& next_alpha_num(StrLike& buffer, size_t max_size, size_t min_size = 0)
            {
                return random_str(buffer, max_size, min_size, rand_alphanum);
            }

            /**
             * Generate random string of random length. It is most generic form of string
             * generation. Possible use cases:
             * - Generate random string between [3-5) characters:\code
             * std::string result;
             * RandomGenerator::instance().random_str(result, 3, 5, RandomGenerator::rand_alphanum);
             * \endcode
             *
             * - Generate single char wide-string only of variadic length:\code
             * std::wstring result;
             * RandomGenerator::instance().random_str(result, 10, 25, [](size_t rnd){return L'a';});
             * \endcode
             *
             * @tparam StrLike buffer to generate, it may be std::string or std::wstring
             * @tparam F lambda that generates next random character, it must match to
             *      of type `typename StrLike::value_type (size_t next_rnd)`
             * @param target result buffer (it is cleared before use)
             * @param max_size specify upper allowed length of result string. Random length
             *      will not exceed this parameter.
             * @param min_size specify lower allowed bound length of result string. Random
             *      length will never be less than it. May be 0 - then empty string allowed
             * @param value_factory lambda to generate random char
             * @return `target` reference
             */
            template <class StrLike, class F >
            inline StrLike& random_str(StrLike& target, size_t max_size, size_t min_size, F value_factory)
            {
                target.clear();
                if (!max_size || min_size > max_size)
                    return target;
                std::uniform_int_distribution<typename StrLike::size_type> pick(min_size, max_size);
                std::uniform_int_distribution<size_t> uint_pick(0);
                std::lock_guard lock(_gen_acc);
                auto l = pick(_gen);
                target.reserve(l);
                std::generate_n(std::back_insert_iterator<StrLike>(target), l,
                                [&](){return value_factory(uint_pick(_gen));});

                return target;
            }
            
            /**
             * Generate random string from characters specified by param `field`
             *
             * @tparam StrLike type like std::string, std::wstring,
             * @param target result buffer
             * @param field buffer with predefined characters
             * @param max_size max allowed size of the result string
             * @param min_size min allowed size of the result string
             * @return param `target`
             */
            template <class StrLike>
            inline StrLike& random_field(StrLike& target, const StrLike& field, size_t max_size, size_t min_size = 0)
            {
                return random_str(target, max_size, min_size, [&](size_t next_r){
                    return field[next_r % field.size()];
                });
            }

            template <class StrLike>
            inline const typename StrLike::value_type& random_field_item(
                    const StrLike& field)
            {
                return field[next_in_range(0, field.size())];
            }

            static char rand_alphanum(size_t next_rnd)
            {
                constexpr const static char alnum[] = {
                        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
                        'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                        'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
                        'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'
                };
                return alnum[ next_rnd % std::extent<decltype(alnum)>::value];
            }
            /** Clone existing generator */
            std::mt19937 generator() const
            {
                std::lock_guard lock(_gen_acc);
                return _gen;
            }
        private:
            std::mt19937 _gen;
            mutable std::mutex _gen_acc;
        };


        template <class T>
        inline T random()
        {
            return RandomGenerator::instance().template next_in_range<T>();
        }

        template <>
        inline std::uint8_t random<std::uint8_t>()
        {
            auto r = RandomGenerator::instance().template next_in_range<std::uint16_t>();
            return static_cast<std::uint8_t>((r >> 8) ^ r);
        }

        inline std::string& random(std::string& target, size_t max_size, size_t min_size = 0)
        {
            return RandomGenerator::instance().random_str( target, max_size, min_size,
                RandomGenerator::rand_alphanum);
        }

        template <>
        inline std::string random<std::string>()
        {
            std::string target;
            return random(target, 12, 12);
        }


        template <class Container1, class Container2, class ErrorHandler>
        inline bool compare(const Container1& co1, const Container2& co2, ErrorHandler& on_error = [](const typename Container2::value_type& v) {})
        {
            std::multiset<typename Container1::value_type> s1(std::begin(co1), std::end(co1));
            for (auto x : co2)
            {
                auto found = s1.find(x);
                if (found == s1.end())
                {
                    on_error(x);
                    return false;
                }
                s1.erase(found);
            }
            return s1.empty();
        }

        template <class It1, class It2>
        inline bool range_equals(It1 first1, It1 last1, It2 first2, It2 last2)
        {
            for (; first1 != last1 && first2 != last2; ++first1, ++first2)
                if (*first1 != *first2)
                    return false;
            return first1 == last1 && first2 == last2;
        }
        template <class It1, class It2, class Pred>
        inline bool range_equals(It1 first1, It1 last1, It2 first2, It2 last2, Pred pred)
        {
            for (; first1 != last1 && first2 != last2; ++first1, ++first2)
                if (!pred(*first1, *first2))
                    return false;
            return first1 == last1 && first2 == last2;
        }
        template <class Co1, class Co2>
        inline bool container_equals(const Co1& co1, const Co2& co2)
        {
            return range_equals(std::begin(co1), std::end(co1), std::begin(co2), std::end(co2));
        }
        template <class Co1, class Co2, class Pred>
        inline bool container_equals(const Co1& co1, const Co2& co2, Pred pred)
        {
            return range_equals(std::begin(co1), std::end(co1), std::begin(co2), std::end(co2), pred);
        }
        //
        template <class A, class B = A>
        inline bool sign_tolerant_cmp(A left, B right)
        {
            return static_cast<typename std::make_unsigned<A>::type>(left) ==
                static_cast<typename std::make_unsigned<B>::type>(right);
        }
        inline bool sign_tolerant_cmp(char left, unsigned char right)
        {
            return (unsigned char)left == right;
        }
    } //ns: tools


    template<class Exception>
    template<typename F, typename... Xetails>
    AssertExceptionWrapper<Exception> &AssertExceptionWrapper<Exception>::then(
        F f, Xetails &&... details)
    {
        if constexpr (std::is_invocable_r_v<bool, F, const Exception&>)
        {
            _owner.assert_true(f(_ex), std::forward<Xetails>(details) ...);
        }
        else
        {
            //on case of error there, use `F` either as `bool(const Exception&)` or `void(const Exception&)`
            f(_ex);
        }
        return *this;
    }

    inline std::ostream& TestRuntime::error() const
    {
        return _suite.error();
    }

    inline std::ostream& TestRuntime::info() const
    {
        return _run_options.log_level() >= ResultLevel::info ? _suite.info() : _null_stream;
    }

    inline std::ostream& TestRuntime::debug() const
    {
        return _run_options.log_level() > ResultLevel::info ? _suite.debug() : _null_stream;
    }

    inline tools::RandomGenerator& TestRuntime::randomizer() const
    {
        return tools::RandomGenerator::instance();
    }

    template<class Predicate>
    bool TestSuite::run_if(Predicate& predicate, TestFixture& fixture, TestRun& run_env, std::deque<TestResult>& result)
    {
        auto& options = run_env.options();
        //apply random seed if needed
        if(options.random_seed())
        {
            tools::RandomGenerator::instance().seed(*options.random_seed());
        }
        // RAII to wrap init_suite/shutdown_suite
        details::FixtureRAII initializer(*this, fixture); //delayed setup until real case need to run
        details::SendSuiteEventRAII suite_event_guard(*this, run_env.event_supplier());
        test_run_shared_args_t runtime_vars;
        runtime_vars.assign(of_var(run_env));
        runtime_vars.assign(of_var(*this));
            
        for (auto& tcase : _tests)
        {
            if (!predicate(*this, *tcase, fixture))
                continue;

            if (!initializer.has_value()) //not initialized yet
            {
                initializer.setup();
                runtime_vars.assign(UnpackAny(&initializer.value()));
                suite_event_guard.start(); //ensure suite_start been send
            }

            TestRuntime runtime(*this, *tcase, options);
            runtime_vars.assign(of_var(runtime));

            run_env.event_supplier().send_event<UnitTestEventSupplier::case_start>(
                UnitTestEventSupplier::start_case_event_t{
                    runtime, *tcase, fixture, std::chrono::steady_clock::now() }
            );

            result.emplace_back( tcase->execute(runtime_vars, options) );
            auto& last_run = result.back();
            run_env.event_supplier().send_event<UnitTestEventSupplier::case_end>(
                UnitTestEventSupplier::end_case_event_t{
                    runtime, fixture, last_run, std::chrono::steady_clock::now() }
            );

            if (options.fail_fast() && last_run.status() != TestResult::Status::ok)
                return false;
        }
        return true;
    }

    TestResult TestCase::load_execute(test_run_shared_args_t& run_args, unsigned run_number, unsigned warm_up)
    {
        auto& runtime = run_args.eval_arg<TestRuntime>();
        auto& env = run_args.eval_arg<TestRun>();
        auto& suite = run_args.eval_arg<TestSuite>();
        TestResult result(suite, this);

        typename UnitTestEventSupplier::load_exec_event_t event_args{ runtime, result, warm_up };

        //test is starting under loading...
        //warm-up cycles
        env.event_supplier().send_event<UnitTestEventSupplier::load_execute_warm>(event_args);
        while (warm_up--)
        {
            auto tr = single_execute(run_args);
            if (!tr) //warm-up failed
                return tr;
        }
        std::get<unsigned>(event_args) = run_number;
        env.event_supplier().send_event<UnitTestEventSupplier::load_execute_run>(event_args);

        //measurement cycles:
        result._start_time = std::chrono::steady_clock::now();
        for (; run_number; --run_number, ++result._run_number)
        {
            isolate_exception_run(run_args, result);
        }
        result._end_time = std::chrono::steady_clock::now();
        //??? result._status = TestResult::Status::ok;

        return result;
    }


    template <class StringLike>
    TestSuite& default_test_suite(const StringLike& name)
    {
        auto found = TestRun::default_instance().find_suite(name);
        if(!found)
        { 
            found = std::make_shared<TestSuite>(
                name, std::cout, std::cerr);
            TestRun::default_instance().declare(found);
        }
        return *found;
    };

    #define _OP_UNIQUE_MODULE_NAME_VAR(prefix, suffix) prefix ## suffix
    #define OP_DECLARE_TEST_CASE(suite_name, case_name, codebase) static auto& _OP_UNIQUE_MODULE_NAME_VAR(module_suite, __COUNTER__) = \
        OP::utest::default_test_suite(suite_name).declare(case_name, codebase)

}//ns: OP:utest
#endif //_OP_UNIT_TEST__H_
