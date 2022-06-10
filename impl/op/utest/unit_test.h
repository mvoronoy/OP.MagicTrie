#pragma once
#ifndef _OP_UNIT_TEST__H_
#define _OP_UNIT_TEST__H_

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
#include <signal.h> 
#include <random>
#include <op/utest/unit_test_is.h>
#include <op/common/IoFlagGuard.h>
#include <op/common/ftraits.h>


/**Render inline information like __FILE__, __LINE__ in lazy way (rendered only on demand) */
#define _OP_LAZY_INLINE_INFO(...) ([&]()->OP::utest::Details { OP::utest::Details res;\
        res << "{File:" << __FILE__ << " at:" << __LINE__  << "}" __VA_ARGS__ ; return res;\
    })
/** Allows place usefull information to details output.
* Usage:
* \code
* ...
* OP_CODE_DETAILS() << "Detail of exception with number " << 57;

* \endcode
*/
#define OP_CODE_DETAILS(...)  _OP_LAZY_INLINE_INFO(__VA_ARGS__)()

#define OP_TEST_STRINGIFY(a) #a

/** Exposes assert functionality if for some reason function have no access to TestRuntime instance.
* Usage:
* \code
* ...
*   OP_UTEST_ASSERT(1==0, << "Logic is a power! Following number:" << 57);
*/
#define OP_UTEST_ASSERT(condition, ...) ([&](bool test)->void{ \
        if(!test){ auto msg ( std::move(OP_CODE_DETAILS( << OP_TEST_STRINGIFY(condition) << " - "  __VA_ARGS__ )));\
            OP::utest::_inner::_uncondition_exception_raise( msg.result() ); } \
    }(condition))

/**The same as OP_UTEST_ASSERT but unconditionally failed*/
#define OP_UTEST_FAIL(...) OP_UTEST_ASSERT(false,  __VA_ARGS__ )
namespace OP
{
    namespace utest
    {

        struct TestFail;
        /**
        *   Level of result logging
        */
        enum class ResultLevel
        {
            /**Display errors only*/
            error = 1,
            /**Display information messages and errors*/
            info = 2,
            /**Display all messages*/
            debug = 3
        };

        namespace _inner {

            inline bool _uncondition_exception_raise(std::string x);

            /** Do nothing buffer */
            class null_buffer : public std::streambuf
            {
            public:
                int overflow(int c) { return c; }
            };


        } //inner namespace
        struct Identifiable
        {
            using id_t = std::string;

            template <class Name>
            explicit Identifiable(Name name) :
                _id(std::forward<Name>(name))
            {}

            Identifiable() = delete;
            Identifiable(const Identifiable&) = delete;

            virtual ~Identifiable() = default;
            const id_t& id() const
            {
                return _id;
            }
        private:
            id_t _id;
        };
        /**ostream-like class that allows:

        -# memory effective inline construction. For example:
            OP::utils::Details() << 123 << "abc";
        */
        struct Details
        {
            Details()
            {
            }
            Details(Details&& other)
                : _result(std::move(other._result))
            {
            }
            Details(const Details& other)
                : Details()
            {
                operator<<(as_stream(), other);
            }

            std::string result() const
            {
                return _result.str();
            }
            bool is_empty()
            {
                _result.seekp(0, std::ios_base::end);
                auto offset = _result.tellp();
                return offset == (std::streamoff)0;
            }
            std::ostream& as_stream()
            {
                return _result;
            }
            template <class T>
            friend inline Details& operator << (Details& d, T&& t)
            {
                d.as_stream() << std::forward<T>(t);
                return d;
            }
            template <class T>
            friend inline Details operator << (Details&& d, T&& t)
            {
                Details inl(std::move(d));
                inl.as_stream() << std::forward<T>(t);
                return std::move(inl);
            }
            template <class Os>
            friend inline std::ostream& operator <<(Os& os, const Details& d)
            {
                os << d._result.str();
                return os;
            }

        private:
            std::stringstream _result;
        };

        /**Specialization of exception to distinguish fail from aborted state*/
        struct TestFail : std::logic_error
        {
            TestFail() :
                std::logic_error("fail")
            {
            }
            explicit TestFail(const std::string& text) :
                std::logic_error(text)
            {
            }
        };
        /**Demarcate abort exception. It is intentionally has no any inheritence*/
        struct TestAbort : public TestFail
        {
            TestAbort()
                : TestFail("abort triggered")
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

            using assert_predicate_t = bool(const Exception&);
            /**
                Apply additional check 'f' to exception ex
                \param f predicate bool(const Exception& ) for additional exception check. Just return false to raise assert
            */
            template <class Predicate, typename ...Xetails>
            this_t& then(Predicate f, Xetails&& ...details)
            {
                _owner.assert_true(f(_ex), std::forward<Xetails>(details) ...);
                return *this;
            }
            /**
                Apply additional check 'f' to exception ex by invoking regular test methods from TestRuntime
            */
            using assert_handler_t = void(const Exception&);
            this_t& then(assert_handler_t f)
            {
                f(_ex);
                return *this;
            }
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

            TestResult(const std::string& name)
                : _name(name)
                , _status(not_started)
                , _run_number(0)
            {}

            const std::string& test_name() const
            {
                return _name;
            }

            enum Status
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

            bool operator !() const
            {
                return _status != ok;
            }
            Status status() const
            {
                return _status;
            }
            double ms_duration() const
            {
                return std::chrono::duration<double, std::milli>(_end_time - _start_time).count();
            }
            const std::string& status_to_str() const
            {
                static const std::string values[] = {
                    "not started", "failed", "exception", "aborted", "ok"
                };
                return values[(_status - _first_) % (_last_ - _first_)];
            }
        private:
            Status _status;
            unsigned _run_number;
            time_point_t _start_time, _end_time;
            const std::string _name;
        };

        /** Expose operations to support unit tests */
        struct TestRuntime
        {
            friend struct TestCase;
            friend struct TestRun;

            TestRuntime(
                TestSuite& suite, const std::string& name, ResultLevel level)
                : _suite(suite)
                , _access_result(new std::recursive_mutex())
                , _name(name)
                , _log_level (level)
            {
            }
            TestRuntime(const TestRuntime&) = delete;
            
            TestSuite& suite() const
            {
                return _suite;
            }
            const std::string& test_name() const
            {
                return _name;
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
            *   then applied multiple Args to it. Marker can be one of standart existing in namespace OP::utest :
            * \li equals - to assert equality of 2 comparable arguments (including STL containers);
            * \li eq_sets - to assert equality of 2 arbitrary containers but with items that can be
            *               automatically compared (by `==` comparator). Both containers are also checked against same items order.
            * \li less - to assert strict less between 2 args
            * \li greater - to assert strict greater between 2 args
            * \li negate or logical_not to invert other Markers. For example `assert_that< negate<less> >(3, 2)` - evaluates
            *               `!(3 < 2)` that is effectively `3>=2`
            * \li is_null - to assert arg is nullptr
            */
            template<class Marker, class ...Args>
            void assert_that(Args&& ...args)
            {
                Marker m;
                //consiously don't use make_tuple to have reference to argument
                auto pack_arg = std::tuple<Args...>(std::forward<Args>(args)...);
                if (!details::apply_prefix(m, pack_arg, std::make_index_sequence<Marker::args_c>()))
                {
                    auto bind_fail = [this](auto&& ... t) {
                        fail(std::forward<decltype(t)>(t)...);
                    };
                    //need pass argument to fail
                    details::apply_rest< Marker::args_c>(bind_fail, pack_arg,
                        std::make_index_sequence<(sizeof ... (Args)) - Marker::args_c>());
                }
            }

            /**
            *   Assert that functor `f` raises exception of expected type `Exception`
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
                    debug() << "... catched as expected\n";
                    return AssertExceptionWrapper<Exception>(*this, ex);
                }
                fail(std::forward<Xetails>(details)...);
                throw 1; //fake line to avoid warning about return value. `fail` will unconditionally raise the exception
            }
            /**Unconditional fail*/
            template<typename ...Xetails>
            void fail(Xetails&& ...details)
            {
                guard_t g(*_access_result);
                ((error() << std::forward<Xetails>(details)), ...);
                fail();
            }
            void fail()
            {
                throw TestFail();
            }
        private:
            TestSuite& _suite;
            const std::string _name;

            typedef std::unique_ptr<std::recursive_mutex> mutex_ptr_t;
            mutex_ptr_t _access_result;
            typedef std::unique_lock<mutex_ptr_t::element_type> guard_t;

            _inner::null_buffer _null_buffer;
            mutable std::ostream _null_stream{ &_null_buffer };
            ResultLevel _log_level = ResultLevel::info;
        };

        typedef std::shared_ptr<TestRuntime> test_result_ptr_t;
        /**Abstract definition of test invokation*/
        struct TestCase : public Identifiable
        {
            template <class Name>
            explicit TestCase(Name&& name) :
                Identifiable(std::forward<Name>(name))
            {
            }
            virtual ~TestCase() {}
            /**Invoke test single times*/
            TestResult execute(TestRuntime& runtime)
            {
                TestResult retval(id());
                retval._start_time = std::chrono::steady_clock::now();
                do_run(runtime, retval);
                retval._end_time = std::chrono::steady_clock::now();
                retval._run_number = 1;
                return retval;
            }
            /**
            *   Start same test multiple times
            *   @param result - acummulate results of all execution into single one. At exit this paramter
            *           contains summary time execution (without warm-up) and status of last executed test
            *   @param run_number - number of times to execue test-case
            *   @param warm_up - some number of executions before measure time begins. Allows warm-up CPU, internal cache and so on...
            */
            TestResult load_execute(TestRuntime& runtime, unsigned run_number, unsigned warm_up = 10)
            {
                while (warm_up--)
                {
                    auto tr = execute(runtime);
                    if (!tr) //warm-up failed
                        return tr;
                }
                TestResult result(id());
                result._start_time = std::chrono::steady_clock::now();
                for (; run_number; --run_number, ++result._run_number)
                {
                    do_run(runtime, result);
                }
                result._end_time = std::chrono::steady_clock::now();
                result._status = TestResult::ok;
                return result;
            }
        protected:
            virtual void run(TestRuntime& retval) = 0;
        
        private:
            template <class Exception>
            static inline void render_exception_status(TestRuntime& runtime, Exception const& e)
            {
                if (e.what() && *e.what())
                {
                    runtime.error() << e.what();
                }
            }
            void do_run(TestRuntime& runtime, TestResult& retval)
            {
                try
                {
                    this->run(runtime);
                    retval._status = TestResult::ok;
                }
                catch (TestAbort const& e)
                {
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::aborted;
                    render_exception_status(runtime, e);
                }
                catch (TestFail const& e)
                {
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::failed;
                    render_exception_status(runtime, e);
                }
                catch (std::exception const& e)
                {
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::exception;

                    runtime.error() << "----[exception-what]:" << e.what() << "\n";
                }
                catch (...)
                { //hide any other exception
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::exception;
                }
            }

        };

        struct TestRun;
        /**Represent set of unit test runners */
        struct TestSuite : 
            public Identifiable, 
            public std::enable_shared_from_this<TestSuite>
        {
            using this_t = TestSuite;
            using test_executor_ptr = std::unique_ptr<TestCase>;
            using test_container_t = std::deque<test_executor_ptr>;
            using iterator = typename test_container_t::iterator;
            template <class Name>
            TestSuite(
                TestRun &run_env, Name&& name, std::ostream& info_stream, std::ostream& error_stream) 
                : Identifiable(std::forward<Name>(name))
                , _run_env(run_env)
                ,_info_stream(info_stream)
                ,_error_stream(error_stream)
            {
            }

            /**
            * Declare optional startup function that is invoked before each suite start. This 
            * method useful 
            * to setup test environment or create data shared accross all tests in this suite.
            * \param init_function caller provided suite initialization. This function can
            *   return void or `std::any` to share some data accross test-cases in this suite.
            *   Input parameters must accept 0 or 1 argument of `TestSuite&` type. \code
            * static auto module_suite = OP::utest::default_test_suite("ExpensiveInitSuite")
            *   .before_suite([]() -> std::any { return std::make_shared<VeryExpensiveClass>();}
            *   .after_suite([](std::any& previous){ // some clean-up code }
            *   .declare("basic", [](const VeryExpensiveClass& shared_accross_many_cases){ ... })
            *
            * \endcode
            * \sa #after_suite
            */
            template <class F>
            TestSuite& before_suite(F init_function)
            {
                using namespace std::string_literals;
                if ((bool)_init_suite)
                    throw std::runtime_error("TestSuite('"s + _name + "') already has defined `before_suite`"s);

                using function_traits_t = OP::utils::function_traits<F>;
                
                static_assert(
                    function_traits_t::arity_c == 0 ||
                    (function_traits_t::arity_c == 1
                        && std::is_same_v<TestSuite&, typename function_traits_t::arg_i<0>>),
                    "Functor for `before_suite` must accept 0 or 1 argument of `TestSuite&` type");
                //Create wrapper that accept exactly 1 TestSuite parameter
                _init_suite = [this, f = std::move(init_function)](TestSuite& owner)
                {
                    if constexpr (function_traits_t::arity_c == 0)
                    { //user function has no arguments
                        auto res = f();
                        //if init function  returns something - then treate as a shared state
                        if constexpr (!std::is_same_v<void, decltype(res)>)
                            _suite_shared_state = std::move(res);
                    }
                    else
                    {
                        auto res = f(*this);
                        //if init function returns something - then treate as a shared state
                        if constexpr (!std::is_same_v<void, decltype(res)>)
                            _suite_shared_state = std::move(res);
                    }
                };
                return *this;
            }

            template <class F>
            TestSuite& after_suite(F tear_down)
            {                                                   
                if ((bool)_shutdown_suite)
                    throw std::runtime_error("TestSuite('"s + _name + "') already has defined `after_suite`"s);
                _shutdown_suite = [this, f = std::move(tear_down)](TestSuite& owner)
                {
                    if constexpr (function_traits_t::arity_c == 0)
                    {
                        f();
                    }
                    else
                    {
                        f(*this);
                    }
                };
            }

            /**
            *   Register test case in this suite to execute.
            * \param name - symbolic name of case inside the suite.
            * \param f - functor to execute during test run. Input parameters for the functor
            *            may be empty or be any combination in any order of the following 
            *           types:
            *               - TestSuite& - accept this instance of test suite;
            *               - TestRuntime& - accept utility class with a lot of useful 
            *                   logging and assert-methods in it;
            *               - `std::any` or user type with value constructed inside `#before_suite`
            */
            template <class F>
            TestSuite& declare(std::string name, F f)
            {
                auto bind_expression = make_bind_expression(std::move(f));
                return this->declare_case(
                    std::make_unique<FunctionalTestCase<decltype(bind_expression)>>(
                        std::move(name),
                        std::move(bind_expression)
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
            TestSuite& declare_disabled(std::string n, F f)
            {
                return *this;
            }

            template <class F>
            TestSuite& declare_exceptional(std::string name, F f)
            {
                auto bind_expr = make_bind_expression(std::move(f));
                return this->declare_case(
                    std::unique_ptr<TestCase> (new AnyExceptionTestCase<decltype(bind_expr)>(
                        std::move(name),
                        std::move(bind_expr)
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
            template <class Predicate>
            std::deque<TestResult> run_if(Predicate& predicate)
            {
                // RAII to wrap init_suite/shutdown_suite
                std::unique_ptr<SuiteInitRAII> initializer; //delayed untill real case need to run
                
                std::deque<TestResult> result;
                for (auto& tcase : _tests)
                {
                    if (!predicate(*this, *tcase))
                        continue;
                    if (!initializer) //not initialized yet
                        initializer = std::make_unique<SuiteInitRAII>(this);

                    TestRuntime runtime(
                        *this, tcase->id(), _run_env.options().log_level());

                    //allow output error right after appear
                    info() << "\t[" << tcase->id() << "]...\n";
                    result.emplace_back( tcase->execute(runtime) );

                    info()
                        << "\t[" << tcase->id() << "] done with status:"
                        << "-=[" << result.back().status_to_str() << "]=-"
                        << " in:" << std::fixed << result.back().ms_duration() << "ms\n";
                }
                return result;
            }
        private:
            using bootstrap_t = std::function<void(TestSuite&)>;
            
            struct SuiteInitRAII
            {
                SuiteInitRAII(this_t* owner)
                    :_owner(owner)
                {
                    _initialized = false;
                    if (_owner->_init_suite)
                        _owner->_init_suite(*_owner);
                    _initialized = true;
                }
                ~SuiteInitRAII()
                {
                    if (_initialized && _owner->_shutdown_suite)
                        _owner->_shutdown_suite(*_owner);
                    _owner->_suite_shared_state.reset();
                }
                this_t* _owner;
                bool _initialized;
            };

            TestRun& _run_env;
            std::string _name;
            test_container_t _tests;
            std::ostream& _info_stream;
            std::ostream& _error_stream;
            std::any _suite_shared_state;
            bootstrap_t _init_suite, _shutdown_suite;

            template <typename traits_t, typename F, size_t... I>
            void do_method(TestRuntime& result, F& func, std::index_sequence<I...>)
            {
                func( inject_arg< std::decay_t<traits_t::arg_i<I>> >(result)... );
            }
            template <class TArg>
            auto& inject_arg(TestRuntime& result)
            {
                using namespace std::string_literals;

                if constexpr (std::is_same_v<TestRuntime, TArg>)
                    return result;
                else if constexpr (std::is_same_v<TestSuite, TArg>)
                    return *this;
                else
                {
                    if (!_suite_shared_state.has_value())
                        throw std::runtime_error(
                            "Test case:'"s + _name + "/"s + result.test_name()
                            + "' expected shared value of type '"s
                            + typeid(TArg).name()
                            + "', but TestSuite::before_suite did not provided");
                    if constexpr (std::is_same_v<std::any, TArg>)
                        return _suite_shared_state;
                    else 
                    { //treate this case as arbitrary argument, so try extract it from std::any
                        return *std::any_cast<TArg>(&_suite_shared_state);
                    }
                }
            }
            /**Pack user defined test-case into exactly 1 argument lambda*/
            template <class F>
            auto make_bind_expression(F f)
            {
                using function_traits_t = OP::utils::function_traits<F>;

                return [this, f = std::move(f)](TestRuntime& result) ->void
                {
                    do_method<function_traits_t>(
                        result, 
                        f, 
                        std::make_index_sequence<function_traits_t::arity_c>{});
                };
                
            }

            template <class F>
            struct FunctionalTestCase : public TestCase
            {
                FunctionalTestCase(std::string&& name, F f) :
                    TestCase(std::move(name)),
                    _function(std::move(f))
                {
                }
            protected:
                void run(TestRuntime& retval) override
                {
                    _function(retval);
                }
            private:
                F _function;
            };
            
            /**Handle test case that raises an exception*/
            template <class F>
            struct AnyExceptionTestCase : public FunctionalTestCase<F>
            {

                AnyExceptionTestCase(std::string&& name, F f) :
                    FunctionalTestCase(std::move(name), std::move(f))
                {
                }
            protected:
                void run(TestRuntime& retval) override
                {
                    try
                    {
                        FunctionalTestCase::run(retval);
                        throw TestFail("exception was expected");
                    }
                    catch (...) {
                        //normal case 
                        retval.debug() << "exception was raised as normal case\n";
                    }
                }
            };

        };

        inline std::ostream& TestRuntime::error() const
        {
            return _suite.error();
        }

        inline std::ostream& TestRuntime::info() const
        {
            return _log_level >= ResultLevel::info ? _suite.info() : _null_stream;
        }

        inline std::ostream& TestRuntime::debug() const
        {
            return _log_level > ResultLevel::info ? _suite.debug() : _null_stream;
        }

        /** Incapsulate test suite configurable options */
        struct TestRunOptions
        {
            TestRunOptions()
            {
                _intercept_sig_abort = true;
                _output_width = 40;
            }
            /**Modifies permission to intercept 'abort' from test code. Set true if C-style assert shouldn't break test execution*/
            TestRunOptions& intercept_sig_abort(bool new_value)
            {
                _intercept_sig_abort = new_value;
                return *this;
            }
            bool intercept_sig_abort() const
            {
                return _intercept_sig_abort;
            }
            std::uint16_t output_width() const
            {
                return _output_width;
            }
            TestRunOptions& output_width(std::uint16_t output_width)
            {
                _output_width = output_width;
                return *this;
            }
            ResultLevel log_level() const
            {
                return _log_level;
            }
            TestRunOptions& log_level(ResultLevel level)
            {
                _log_level = level;
                return *this;
            }

        private:
            bool _intercept_sig_abort;
            std::uint16_t _output_width;
            ResultLevel _log_level = ResultLevel::info;
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
            TestSuite& declare(test_suite_ptr& suite)
            {
                return *_suites.emplace(suite->id(), suite)->second;
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
            std::deque<TestResult> run_all()
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
                        << std::setw(_options.output_width() - suite_decl.first.length())
                        << ""
                        << std::endl;

                    stream_guard.reset();
                    try 
                    { // This try allows intercept exceptions only from suite initialization
                        auto single_res = suite_decl.second->run_if(predicate);
                        for (auto& r : single_res)
                            result.emplace_back(std::move(r));
                    }
                    catch (const std::exception& ex)
                    {
                        result.push_back(handle_environemnt_crash(
                            *suite_decl.second, &ex));
                    }
                    catch (...)
                    {
                        result.push_back(handle_environemnt_crash(
                            *suite_decl.second, nullptr));
                    }
                }
                return result;
            }
        private:
            static TestResult handle_environemnt_crash(TestSuite&suite, const std::exception* pex)
            {
                TestResult crash_res("ENVIRONMENT");
                crash_res._status = TestResult::aborted;
                suite.error() << "Test Environment Crash, aborted.";
                if (pex)
                    suite.error() << "----[exception-what]:" << pex->what();
                suite.error() << "\n";
                return crash_res;
            }
            /**Helper RAII to intercept all standart C++ `abort` signals*/
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
                static void my_handler(int param)
                {
                    throw TestAbort();
                }
                void(*_prev_handler)(int);
            };

            using suites_t = std::multimap<Identifiable::id_t, std::shared_ptr<TestSuite> >;
            suites_t _suites;
            TestRunOptions _options;
        };
        namespace _inner {

            inline bool _uncondition_exception_raise(std::string x)
            {
                throw OP::utest::TestFail(std::move(x));
            }
        }
        namespace tools
        {
            inline size_t wrap_rnd()
            {
                static std::mt19937 gen;
                return gen();
            }

            template <class V, class F >
            inline V& randomize_str(V& target, size_t max_size, size_t min_size, F value_factory)
            {
                target.clear();
                if (!max_size || min_size > max_size)
                    return target;
                auto l = (min_size == max_size) ? min_size : (
                    (wrap_rnd() % (max_size - min_size)) + min_size);
                target.reserve(max_size);
                std::generate_n(std::back_insert_iterator<V>(target), l, value_factory);

                return target;
            }


            template <class T>
            inline T randomize();

            inline std::string& randomize(std::string& target, size_t max_size, size_t min_size = 0)
            {
                return randomize_str(target, max_size, min_size,
                    []()->std::string::value_type {
                        return static_cast<std::string::value_type>((wrap_rnd() % std::abs('_' - '0')) + '0');
                    });
            }

            template <>
            inline std::string randomize<std::string>()
            {
                std::string target;
                return randomize(target, 12, 12);
            }

            inline int random_value()
            {
                return static_cast<int>(wrap_rnd() % std::numeric_limits<int>::max());
            }

            template <>
            inline std::uint8_t randomize<std::uint8_t>()
            {
                std::string target;
                return static_cast<std::uint8_t>(std::rand());
            }
            template <>
            inline uint16_t randomize<std::uint16_t>()
            {
                std::string target;
                return static_cast<std::uint16_t>(std::rand());
            }
            template <>
            inline std::uint64_t randomize<std::uint64_t>()
            {
                return (static_cast<std::uint64_t>(wrap_rnd()) << 32)
                    | static_cast<std::uint64_t>(wrap_rnd());
            }


            /*inline std::string random_value_string()
            {
                std::string r;
                return randomize(r, 256);
            }*/
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
        }


        template <class Name>
        TestSuite& default_test_suite(Name&& name)
        {
            return TestRun::default_instance().declare(
                std::make_shared<TestSuite>(
                    TestRun::default_instance(), std::forward<Name>(name), std::cout, std::cerr)
            );
    };

    } //utest
}//OP
#endif //_OP_UNIT_TEST__H_
