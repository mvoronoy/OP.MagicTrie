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
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <cstdint>
#include <signal.h> 
#include <random>
#include <op/utest/unit_test_is.h>


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

/** Exposes assert functionality if for some reason function have no access to TestResult instance.
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
            typedef std::string id_t;
            
            template <class Name>
            explicit Identifiable(Name name) :
                _id(std::forward<Name>(name))
            {}

            Identifiable() = delete;
            Identifiable(const Identifiable &) = delete;

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
            friend inline Details& operator << (Details&d, T && t)
            {
                d.as_stream() << std::forward<T>(t);
                return d;
            }
            template <class T>
            friend inline Details operator << (Details&& d, T && t)
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

        /** Result of test execution */
        struct TestResult
        {
            friend struct TestCase;
            friend struct TestRun;
            typedef std::chrono::steady_clock::time_point time_point_t;
            
            /** Helper class to organize chain of additional checks after assert_exception */
            template <class Exception>
            struct AssertExceptionWrapper
            {
                using this_t = AssertExceptionWrapper<Exception>;
                AssertExceptionWrapper(TestResult& owner, Exception ex)
                    : _owner(owner)
                    , _ex(std::move(ex))
                    {}

                using assert_predicate_t = bool(const Exception&);
                /** 
                    Apply additional check 'f' to exception ex 
                    \param f predicate bool(const Exception& ) for additional exception check. Just return false to raise assert
                */
                template <typename ...Xetails>
                this_t& then(assert_predicate_t f, Xetails&& ...details)
                {
                    _owner.assert_that([&](){ return f(_ex); }, std::forward<Xetails>(details) ...);
                    return *this;
                }
                /**
                    Apply additional check 'f' to exception ex by invoking regular test methods from TestResult
                */
               
                using assert_handler_t = void(TestResult& tresult, const Exception&);
                this_t& then(assert_handler_t f)
                {
                    f(_owner, _ex);
                    return *this;
                }
            private:
                TestResult& _owner;
                Exception _ex;
            };

            explicit TestResult(std::shared_ptr<TestSuite> suite) :
                _suite(std::move(suite)),
                _status(not_started),
                _run_number(0),
                _access_result(new std::recursive_mutex())
            {
            }
            TestResult(const TestResult&) = delete;
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
            ResultLevel log_level() const
            {
                return _log_level;
            }
            void log_level(ResultLevel level)
            {
                _log_level = level;
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
            inline std::ostream& error() const;            
            inline std::ostream& info() const;

            inline std::ostream& debug() const
            {
                return _log_level >  ResultLevel::info ? info() : _null_stream;
            }

            unsigned run_number() const
            {
                return _run_number;
            }
            time_point_t start_time() const
            {
                return _start_time;
            }
            time_point_t end_time() const
            {
                return _end_time;
            }

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
                catch (const Exception&ex)
                {
                    debug() << "Exception " << typeid(Exception).name();
                    if constexpr (std::is_base_of_v<std::exception, Exception>)
                        debug() << ex.what();
                    debug() << "... Successfully catched as expected\n";
                    return AssertExceptionWrapper<Exception>(*this, ex);
                }
                fail(std::forward<Xetails>(details)...);
                throw 1; //fake line to avoid warning about return value. `fail` will unconditionally raise the exception
            }
            /**Unconditional fail*/
            template<typename ...Xetails>
            void fail( Xetails&& ...details)
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

            Status _status;
            unsigned _run_number;
            time_point_t _start_time, _end_time;
            std::shared_ptr<TestSuite> _suite;

            typedef std::unique_ptr<std::recursive_mutex> mutex_ptr_t;
            mutex_ptr_t _access_result;
            typedef std::unique_lock<mutex_ptr_t::element_type> guard_t;

            _inner::null_buffer _null_buffer;
            mutable std::ostream _null_stream{ &_null_buffer };
            ResultLevel _log_level = ResultLevel::info;
        };
        typedef std::shared_ptr<TestResult> test_result_ptr_t;
        /**Abstract definition of test invokation*/
        struct TestCase : public Identifiable
        {
            template <class Name>
            explicit TestCase(Name && name) :
                Identifiable(std::forward<Name>(name))
            {
            }
            virtual ~TestCase(){}
            /**Invoke test single times*/
            TestResult& execute(TestResult& retval)
            {
                retval._start_time = std::chrono::steady_clock::now();
                do_run(retval);
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
            TestResult& load_execute(TestResult& result, unsigned run_number, unsigned warm_up = 10)
            {
                while (warm_up--)
                {
                    auto& tr = execute(result);
                    if (!tr) //warm-up failed
                        return tr;
                }
                result._start_time = std::chrono::steady_clock::now();
                result._run_number = 0;
                for (; run_number; --run_number, ++result._run_number)
                {
                    do_run(result);
                }
                result._end_time = std::chrono::steady_clock::now();
                result._status = TestResult::ok;
                return result;
            }
        protected:
            virtual void run(TestResult& retval) = 0;
        private:
            template <class Exception>
            static inline void render_exception_status(TestResult& retval, Exception const & e)
            {
                if (e.what() && *e.what())
                {
                    retval.error() << e.what();
                }
            }
            void do_run(TestResult& retval)
            {
                try
                {
                    this->run(retval);
                    retval._status = TestResult::ok;
                }
                catch (TestAbort const &e)
                {
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::aborted;
                    render_exception_status(retval, e);
                }
                catch (TestFail const &e)
                {
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::failed;
                    render_exception_status(retval, e);
                }
                catch (std::exception const &e)
                {
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::exception;

                    retval.error() << "----[exception-what]:" << e.what() << "\n";
                }
                catch (...)
                { //hide any other exception
                    retval._end_time = std::chrono::steady_clock::now();
                    retval._status = TestResult::exception;
                }
            }

        };

        struct TestRun;
        /**Represent set of test executors*/
        struct TestSuite : public Identifiable, public std::enable_shared_from_this<TestSuite>
        {
            template <class Name>
            TestSuite(Name&& name, std::ostream& info_stream, std::ostream& error_stream) :
                Identifiable(std::forward<Name>(name)),
                _info_stream(info_stream),
                _error_stream(error_stream)
            {
            }


            inline TestSuite* declare(std::function<void(TestResult&)> f, std::string n = std::string())
            {
                std::string name = (n.empty()) ? typeid(f).name() : std::string(std::move(n));
                return this->declare_case(
                    std::make_shared<FunctionalTestCase>(
                    std::move(name),
                    std::move(f)
                    )
                    );
            }
            inline TestSuite* declare(std::function<void(void)> f, std::string nm = std::string())
            {
                std::function<void(TestResult&)> functor = [f](TestResult&){f();};
                return this->declare(std::move(functor), std::move(nm));
            }
            
            template <class F>
            inline TestSuite* declare_disabled(F f, std::string n = std::string())
            {
                return this;
            }

            inline TestSuite* declare_exceptional(void (f)(TestResult&), std::string n = std::string())
            {
                auto fwrap = std::function< void(TestResult&) >(f);
                std::string name = (n.empty()) ? typeid(n).name() : std::string(std::move(n));
                std::shared_ptr<TestCase> pt(new AnyExceptionTestCase(
                    std::move(name),
                    std::move(fwrap)
                    ));
                return this->declare_case(pt);
            }
            
            TestSuite* declare_case(std::shared_ptr<TestCase> exec)
            {
                _tests.push_back(exec);
                return this;
            }
            /**Enumerate all test cases without run
            * @tparam F callback for enumerate cases, it should match to signature `bool F(TestCase&)`. Method
            *   continues iteration if predicate returns true and stops right after false
            */
            template <class F>
            void list_cases(F && f)
            {
                for (auto& t : _tests)
                {
                    if (!f(*t))
                        return;
                }
            }
            std::ostream& info()
            {
                return _info_stream;
            }
            std::ostream& error()
            {
                return _error_stream;
            }
        private:
            std::string _name;
            typedef std::shared_ptr<TestCase> test_executor_ptr;
            typedef std::deque<test_executor_ptr> test_container_t;
            test_container_t _tests;
            std::ostream& _info_stream;
            std::ostream& _error_stream;

            
            struct FunctionalTestCase : public TestCase
            {
                template <class F>
                FunctionalTestCase(std::string && name, F&& f) :
                    TestCase(std::move(name)),
                    _function(std::forward<F>(f))
                {
                }
            protected:
                void run(TestResult& retval) override
                {
                    _function(retval);
                }
            private:
                std::function<void(TestResult&)> _function;
            };
            /**Handle test case that raises an exception*/
            struct AnyExceptionTestCase : public FunctionalTestCase
            {
                template <class F>
                AnyExceptionTestCase(std::string && name, F&& f) :
                    FunctionalTestCase(std::move(name), std::forward<F>(f))
                {
                }
            protected:
                void run(TestResult& retval) override
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

        inline std::ostream& TestResult::error() const
        {
            return _suite->error();
        }
            
        inline std::ostream& TestResult::info() const
        {
            return _log_level >= ResultLevel::info ? _suite->info() : _null_stream;
        }

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
            typedef std::shared_ptr<TestSuite> test_suite_ptr;
            explicit TestRun(TestRunOptions options = TestRunOptions())
                : _options(options)
            {
            }
            static TestRun& default_instance()
            {
                static TestRun instance;
                return instance;
            }
            void declare(test_suite_ptr& suite)
            {
                _suites.emplace(suite->id(), suite);
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
                    //std::function<bool(TestCase&)> curying = std::bind(f, *(p.second), _1);
                    if (!f(*p.second))
                        return;
                }
            }
            std::vector<test_result_ptr_t> run_all()
            {
                return run_if([](TestSuite&, TestCase&){return true; });
            }
            /**
            * Run all tests that match to predicate specified
            * @tparam F predicate that matches to signature `bool F(TestSuite&, TestCase&)`
            */
            template <class F>
            std::vector< test_result_ptr_t > run_if(F f)
            {
                std::vector<std::shared_ptr<TestResult> > result;
                sig_abort_guard guard;

                for (auto& p : _suites)
                {
                    p.second->info() << "==["<< p.first <<"]"<< std::setfill ('=') << std::setw(_options.output_width() - p.first.length()) << ""<< std::endl;
                    for_each_case_if(*p.second,
                        f,
                        [&result](std::shared_ptr<TestResult> res){
                        result.emplace_back(std::move(res));
                    }
                    );
                }
                return result;
            }
        private:
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

            template <class Predicate, class Function>
            void for_each_case_if(TestSuite &suite, Predicate && p, Function && f)
            {
                suite.list_cases([&](TestCase& test){
                    if (p(suite, test))
                    {
                        std::shared_ptr<TestResult> result{ std::make_shared<TestResult>(suite.shared_from_this()) };
                        result->log_level(_options.log_level()); 
                        //allow output error right after appear
                        suite.info() <<"\t["<<test.id()<<"]...\n";
                        test.execute(*result);

                        suite.info()
                            <<"\t["<<test.id()<< "] done with status:"
                            << "-=["<<result->status_to_str() << "]=-" 
                            << " in:"<<std::fixed<< result->ms_duration()<< "ms\n";
                        f(std::move(result));
                    }
                    return true;//always continue
                });
            }

            typedef std::multimap<Identifiable::id_t, std::shared_ptr<TestSuite> > suites_t;
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
                return randomize( target, 12, 12 );
            }

            inline int random_value()
            {
                return static_cast<int>(wrap_rnd() % std::numeric_limits<int>::max()) ;
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
            inline bool compare(const Container1& co1, const Container2& co2, ErrorHandler& on_error = [](const typename Container2::value_type& v){})
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
                    if (!pred(*first1,*first2))
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
                return static_cast< typename std::make_unsigned<A>::type >(left) == 
                    static_cast<typename std::make_unsigned<B>::type >(right);
            }
            inline bool sign_tolerant_cmp(char left, unsigned char right)
            {
                return (unsigned char)left == right;
            }
        }
        
        
        template <class Name>
        inline std::shared_ptr<TestSuite> default_test_suite(Name && name)
        {
            auto r = std::make_shared<TestSuite>(std::forward<Name>(name), std::cout, std::cerr);
            TestRun::default_instance().declare(r);
            return r;
        };

    } //utest
}//OP
#endif //_OP_UNIT_TEST__H_
