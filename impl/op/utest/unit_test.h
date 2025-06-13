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
#include <op/common/Console.h>
#include <op/common/has_member_def.h>
#include <op/common/Currying.h>
#include <op/common/NullStream.h>

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
        
        [[nodiscard]] bool is_load_run() const
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
    struct TestSuite;
    struct TestRun;
    struct TestRuntime;

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
        using time_point_t = std::chrono::steady_clock::time_point;
        friend struct TestCase;
        friend struct TestRun;

        explicit TestResult(std::string name)
            : _name(std::move(name))
            , _status(Status::not_started)
            , _run_number(0)
        {}

        [[maybe_unused]] [[nodiscard]] const std::string& test_name() const
        {
            return _name;
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
        constexpr static size_t status_size_c = ((size_t)Status::_last_ - (size_t)Status::_first_);

        constexpr bool operator !() const
        {
            return _status != Status::ok;
        }
        [[nodiscard]] constexpr Status status() const
        {
            return _status;
        }
        
        [[nodiscard]] double ms_duration() const
        {
            return std::chrono::duration<double, std::milli>(_end_time - _start_time).count();
        }

        [[nodiscard]] unsigned run_number() const
        {
            return _run_number;
        }

        [[nodiscard]] double avg_duration() const
        {
            return _run_number > 1 ? (ms_duration() / _run_number) : ms_duration();
        }


        [[nodiscard]] static const std::string& status_to_str(Status status) 
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

        using colored_wrap_t = console::color_meets_value_t<std::string>;

        [[nodiscard]] static colored_wrap_t status_to_colored_str(Status status) 
        {
            using fclr_t = colored_wrap_t(*)(const std::string&);
            const std::string& str = status_to_str(status);
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
            return 
                coloring_seq[((size_t)status - (size_t)Status::_first_) % status_size_c](str);

        }
        [[nodiscard]] colored_wrap_t status_to_colored_str() const
        {
            return status_to_colored_str(_status);
        }
        
    private:
        Status _status;
        unsigned _run_number;
        time_point_t _start_time, _end_time;
        const std::string _name;
    };

    namespace tools //forward decl
    {
        struct RandomGenerator;
    }//ns:tools

    /** Expose operations to support unit tests */
    struct TestRuntime
    {
        friend struct TestCase;
        friend struct TestRun;

        TestRuntime(
            TestSuite& suite, std::string name, TestRunOptions run_options)
            : _suite(suite)
            , _access_result(new std::recursive_mutex())
            , _name(std::move(name))
            , _run_options(std::move(run_options))
        {
        }

        TestRuntime(const TestRuntime&) = delete;

        [[maybe_unused]] [[nodiscard]] TestSuite& suite() const
        {
            return _suite;
        }
        const std::string& test_name() const
        {
            return _name;
        }

        const TestRunOptions& run_options() const
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
        const std::string _name;

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
        // Shared state from `before_suite`
        Unpackable<std::any> >;

    /** Abstract definition of single test */
    struct TestCase 
    {
        template <class Name>
        explicit TestCase(Name&& name) :
            _name(std::forward<Name>(name))
        {
        }

        virtual ~TestCase() = default;

        const std::string& id() const
        {
            return _name;
        }

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
        TestResult single_execute(test_run_shared_args_t& runtime)
        {
            TestResult retval(_name);
            isolate_exception_run(runtime, retval);
            retval._run_number = 1;
            return retval;
        }

        /**
        *   Start same test multiple times
        *   @param result - accumulate results of all execution into single one. At exit this parameter
        *           contains summary time execution (without warm-up) and status of last executed test
        *   @param run_number - number of times to execute test-case
        *   @param warm_up - some number of executions before measure time begins. Allows
         *                  warm-up CPU, internal cache and so on...
        */
        TestResult load_execute(test_run_shared_args_t& runtime, unsigned run_number, unsigned warm_up = 10)
        {
            auto &info_stream = runtime.eval_arg<TestRuntime>().info();
            info_stream << "=> test is starting under loading...\n"
                << console::esc<console::blue_t>("\twarm-up cycles:(")
                << console::esc<console::bright_cyan_t>(warm_up)
                << console::esc<console::blue_t>(")...\n");
            while (warm_up--)
            {
                auto tr = single_execute(runtime);
                if (!tr) //warm-up failed
                    return tr;
            }
            info_stream
                << console::esc<console::blue_t>("\tmeasurement cycles:(")
                << console::esc<console::bright_cyan_t>(run_number)
                << console::esc<console::blue_t>(")...\n");
            TestResult result(_name);
            result._start_time = std::chrono::steady_clock::now();
            for (; run_number; --run_number, ++result._run_number)
            {
                isolate_exception_run(runtime, result);
            }
            result._end_time = std::chrono::steady_clock::now();
            result._status = TestResult::Status::ok;

            return result;
        }
        
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

                runtime.eval_arg<TestRuntime&>().error()
                    << "\n----["
                    << console::esc<console::red_t>("exception")<<"] - what:("
                    << e.what() 
                    << ")\n";
            }
            catch (...)
            { //stop propagation of any other exception
                retval._status = TestResult::Status::exception;
            }
        }
        const std::string _name;
    };

    /** Represent logical grouping of multiple test cases */
    struct TestSuite :
        public std::enable_shared_from_this<TestSuite>
    {
        using this_t = TestSuite;
        using test_executor_ptr = std::unique_ptr<TestCase>;
        using test_container_t = std::deque<test_executor_ptr>;
        using iterator = typename test_container_t::iterator;
        template <class Name>
        TestSuite(
            Name&& name, std::ostream& info_stream, std::ostream& error_stream)
            : _name(std::forward<Name>(name))
            ,_info_stream(info_stream)
            ,_error_stream(error_stream)
        {
        }

        const std::string& id() const
        {
            return _name;
        }
        /**
        * Declare optional startup function that is invoked before each suite start. This
        * method useful
        * to setup test environment or create data shared across all tests in this suite.
        * \param init_function caller provided suite initialization. This function can
        *   return void or `std::any` to share some data across test-cases in this suite.
        *   Input parameters must accept 0 or 1 argument of `TestSuite&` type. \code
        * static auto module_suite = OP::utest::default_test_suite("ExpensiveInitSuite")
        *   .before_suite([]() -> std::any { return std::make_shared<VeryExpensiveClass>();}
        *   .after_suite([](std::any& previous){ // some clean-up code }
        *   .declare("basic", [](const VeryExpensiveClass& shared_across_many_cases){ ... })
        *
        * \endcode
        * \sa #after_suite
        */
        template <class F>
        [[maybe_unused]] TestSuite& before_suite(F init_function)
        {
            using namespace std::string_literals;
            if ((bool)_init_suite)
                throw std::runtime_error("TestSuite('"s + _name + "') already has definition `before_suite`"s);

            using function_traits_t = OP::utils::function_traits<F>;
            static_assert(std::is_invocable_v<F> || std::is_invocable_v<F, TestSuite&>,
                "Functor for `before_suite` must accept 0 or 1 argument of `TestSuite&` type");
            using result_t = typename function_traits_t::result_t;
            //Create wrapper that has no return and 0 params
            if constexpr (std::is_same_v<void, result_t>)
            {//init function returns nothing, so use as is
                _init_suite =
                    OP::currying::arguments(
                        std::ref(*this), of_unpackable(this->_suite_shared_state))
                    .def(std::move(init_function));

            }
            else 
            {//init function provides some shared value, store it for others cases
                _init_suite = [
                    fwrap = std::move(init_function),
                    args = OP::currying::arguments(
                        std::ref(*this), of_unpackable(this->_suite_shared_state)), 
                    &suite_shared_state = _suite_shared_state
                ] () mutable {
                    if constexpr (std::is_same_v<std::any, result_t>)
                        suite_shared_state = std::move(args.invoke(fwrap));
                    else //not a std::any, need to assign
                        suite_shared_state.emplace<result_t>(args.invoke(fwrap));
                };
            }

            return *this;
        }

        template <class F>
        [[maybe_unused]] TestSuite& after_suite(F tear_down)
        {
            using namespace std::literals;
            using function_traits_t = OP::utils::function_traits<F>;
            if ((bool)_shutdown_suite)
                throw std::runtime_error("TestSuite('"s + _name + "') already has defined `after_suite`"s);
            _shutdown_suite =
                OP::currying::arguments(std::ref(*this)).def(std::move(tear_down));
            return *this;
        }

        /**
         *   Register a test case in this suite to execute.
         * \param name - Symbolic name of the case inside the suite.
         * \param f - Functor to execute during the test run. Input parameters for the functor
         *            may be empty or any combination, in any order, of the following
         *            types:
         *               - TestSuite& - Accepts this instance of the test suite.
         *               - TestRuntime& - Accepts a utility class with multiple useful
         *                   logging and assert methods.
         *               - `std::any` or a user type with value constructed inside `#before_suite`.
         * \param tags - Optional list of tags that allows starting or filtering out cases at runtime. For example,
         *               create a tag "long" for long-running tests and then exclude it for smoke testing.
         */
        template <class F>
        TestSuite& declare(std::string name, F f, 
            std::initializer_list<std::string> tags = {})
        {
            return this->declare_case(
                std::make_unique<FunctionalTestCase<F>>(
                    std::move(name),
                    std::move(f)
                    )
            );
        }

        /**
        * Same as 2 argument version but renders unique name by test order number
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
        
        /** Configurable options of this suite */
        TestRunOptions& options()
        {
            return _options;
        }

        template <class Predicate>
        std::deque<TestResult> run_if(Predicate& predicate);

        template <class Predicate>
        std::deque<TestResult> load_run(Predicate& predicate);

    protected:
        /** Initialization right before suite run 
        */
        virtual void before_exec()
        {
            if (_init_suite)
            {
                _init_suite();
            }
        } 
        /** Tear down right after suite run */
        virtual void after_exec()
        {
            if (_shutdown_suite)
            {
                _shutdown_suite();
            }
            _suite_shared_state.reset();
        } 

    private:
        using bootstrap_t = std::function<void()>;

        struct SuiteInitRAII
        {
            explicit SuiteInitRAII(this_t* owner)
                :_owner(owner)
            {
                _owner->before_exec();
                _initialized = true;
            }

            ~SuiteInitRAII()
            {
                if (_initialized)
                {
                    _owner->after_exec();
                }
            }

            this_t* _owner;
            bool _initialized = false;
        };

        TestRunOptions _options;
        std::string _name;
        test_container_t _tests;
        std::ostream& _info_stream;
        std::ostream& _error_stream;

        std::any _suite_shared_state;
        bootstrap_t _init_suite, _shutdown_suite;

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
        using test_suite_ptr = std::shared_ptr<TestSuite> ;
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

        [[maybe_unused]] std::deque<TestResult> run_all()
        {
            return run_if([](TestSuite&, TestCase&) {return true; });
        }

        /**
        * Run all tests that match to predicate specified
        * \tparam Predicate predicate that matches to signature `bool F(TestSuite&, TestCase&)`
        */
        template <class Predicate>
        std::deque< TestResult > run_if(Predicate predicate)
        {
            std::deque<TestResult> result;
            sig_abort_guard guard;

            for (auto& suite_decl : _suites)
            {
                IoFlagGuard stream_guard(suite_decl.second->info());

                suite_decl.second->info()
                    << "==[" << suite_decl.first << "]"
                    << std::setfill('=')
                    << std::setw(
                            static_cast<int>(_options.output_width() - suite_decl.first.length()))
                    << ""
                    << std::endl;

                stream_guard.reset();
                suite_decl.second->options() = _options;

                try
                { // This try allows intercept exceptions only from suite initialization
                    auto single_res = suite_decl.second->run_if(predicate);
                    for (auto& r : single_res)
                        result.emplace_back(std::move(r));
                }
                catch (const std::exception& ex)
                {
                    result.push_back(handle_environment_crash(
                            *suite_decl.second, &ex));
                    if (_options.fail_fast())
                        break;
                }
                catch (...)
                {
                    result.push_back(handle_environment_crash(
                            *suite_decl.second, nullptr));
                    if (_options.fail_fast())
                        break;
                }
            }
            return result;
        }

    private:
        static TestResult handle_environment_crash(TestSuite&suite, const std::exception* pex)
        {
            TestResult crash_res("Suite");
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

        using suites_t = std::multimap<std::string, std::shared_ptr<TestSuite> >;
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
         *  By default class inititialized by current time milliseconds, but it can
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
    std::deque<TestResult> TestSuite::run_if(Predicate& predicate)
    {
        //apply random seed if needed
        if(_options.random_seed())
        {
            tools::RandomGenerator::instance().seed(*_options.random_seed());
        }
        // RAII to wrap init_suite/shutdown_suite
        std::unique_ptr<SuiteInitRAII> initializer; //delayed until real case need to run
        test_run_shared_args_t runtime_vars;
        runtime_vars.assign(of_var(*this));
        runtime_vars.assign(of_unpackable(this->_suite_shared_state));
            
        std::deque<TestResult> result;
        for (auto& tcase : _tests)
        {
            if (!predicate(*this, *tcase))
                continue;

            if (!initializer) //not initialized yet
            {
                initializer = std::make_unique<SuiteInitRAII>(this);
            }
            TestRuntime runtime(
                    *this, tcase->id(), _options);
            runtime_vars.assign(of_var(runtime));

            //allow output error right after appear
            info() << "\t[" << tcase->id() << "]...\n";
            result.emplace_back( tcase->execute(runtime_vars, _options) );
            auto& last_run = result.back();
            IoFlagGuard stream_guard(info());
            info()
                    << "\t[" << tcase->id() << "] done with status:"
                    << "-=[" << last_run.status_to_colored_str()
                    << "]=-"
                    << " in:" 
                    << std::fixed << std::setprecision(3) 
                    << last_run.ms_duration() << "ms"
                ;
            if(result.back().run_number() > 1)
            {
                info() << ", (avg:"
                    << console::esc<console::bright_cyan_t>(last_run.avg_duration() ) << "ms)"
                    ;
            }
            info() <<'.' << std::endl //need flush
                ;
            if (_options.fail_fast() && result.back().status() != TestResult::Status::ok)
                break;
        }
        return result;
    }

    template <class Name>
    TestSuite& default_test_suite(Name&& name)
    {
        return TestRun::default_instance().declare(
            std::make_shared<TestSuite>(
                std::forward<Name>(name), std::cout, std::cerr)
        );
    };

}//ns: OP:utest
#endif //_OP_UNIT_TEST__H_
