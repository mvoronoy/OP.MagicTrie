#ifndef _UNIT_TEST__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1
#define _UNIT_TEST__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1

#include <vector>
#include <deque>
#include <iostream>
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
/** Allows place usefull information to detail output.
* Usage:
* \code
* ...
* OP_CODE_DETAILS( << "Dtail of exception with number " << 57 );

* \endcode
*/
#define OP_CODE_DETAILS(...) OP::utest::detail() << "{File:" << __FILE__ << " at:" << __LINE__  << "}\n" ## __VA_ARGS__ 
#define OP_TEST_STRINGIFY(a) #a

/** Exposes assert functionality if for some reason function have no access to TestResult instance.
* Usage:
* \code
* ...
*   OP_UTEST_ASSERT(1==0, << "Logic is a power! Following number:" << 57);
*/
#define OP_UTEST_ASSERT(condition, ...) (void)((!!(condition)) || OP::utest::_inner::_uncondition_exception_raise( (OP_CODE_DETAILS( << OP_TEST_STRINGIFY(condition) << " - " ## __VA_ARGS__ )).result() ) )
/**The same as OP_UTEST_ASSERT but unconditionally failed*/
#define OP_UTEST_FAIL(...) (void)(OP::utest::_inner::_uncondition_exception_raise( (OP_CODE_DETAILS( << OP_TEST_STRINGIFY(condition) << " - " ## __VA_ARGS__ )).result() ) )
namespace OP
{
    namespace utest{
        
        namespace _inner {
            template <class X>
            inline bool _uncondition_exception_raise(X&&x)
            {
                throw OP::utest::TestFail(std::forward<X>(x));
            }
            template <class F, class ... Args >
            inline std::function< std::result_of_t< F&(Args...) > > make_function(F&& f)
            {
                return std::forward<F>(f);
            }

            inline std::function< void() > make_function(void f())
            {
                return std::function< void() >(f);
            }

            /**
            *
            *   @copyright teebuf and teestream
            *       idea and implementation copied from article http://wordaligned.org/articles/cpp-streambufs where sources are distributed
            *       over public domain
            */
            class multiplex_buf : public std::streambuf
            {
            public:

                multiplex_buf()
                {
                }

                /**
                * Bind some stream-buffer to multiplexor
                */
                void bind(std::streambuf * sb)
                {
                    _buffers.emplace_back(sb);
                }
                /**
                *   Remove previously #bind stream-buf from this multiplexor
                */
                void unbind(std::streambuf * sb)
                {
                    auto pos = std::find(_buffers.begin(), _buffers.end(), sb);
                    if (pos != _buffers.end())
                        _buffers.erase(pos);
                }
            private:
                // This tee buffer has no buffer. So every character "overflows"
                // and can be put directly into the teed buffers.
                virtual int overflow(int c)
                {
                    if (c == EOF)
                    {
                        return !EOF;
                    }
                    else
                    {
                        bool is_eof = false;
                        for (auto sb : _buffers)
                        {
                            is_eof = is_eof || EOF == sb->sputc(c);
                        }
                        return is_eof ? EOF : c;
                    }
                }

                // Sync both teed buffers.
                virtual int sync()
                {
                    int sync_stat = 0;
                    for (auto sb : _buffers)
                    {
                        sync_stat = sb->pubsync() == 0 && sync_stat == 0 ? 0 : -1;
                    }
                    return sync_stat;
                }
            private:
                typedef std::vector<std::streambuf *> multiplex_t;
                multiplex_t _buffers;
            };

            class multiplex_stream : public std::ostream
            {
            public:
                // Construct an ostream which tees output to the supplied
                // ostreams.
                template <class ... Tx>
                multiplex_stream(Tx && ... os)
                    : std::ostream(&_tbuf)
                {
                    bind(std::forward<Tx>(os)...);
                }
                template <class ... Tx>
                void bind(Tx &&... tx)
                {
                    this->do_bind(std::forward<Tx>(tx)...);
                }
                void unbind(std::ostream*os)
                {
                    _tbuf.unbind(os->rdbuf());
                }
            private:
                template <class Os>
                void do_bind(Os && os)
                {
                    _tbuf.bind(os->rdbuf());
                }
                template <class Os, class ... Tx>
                void do_bind(Os && os, Tx &&... tx)
                {
                    _tbuf.bind(os->rdbuf());
                    this->do_bind(std::forward<Tx>(tx)...);
                }
                void do_bind()
                {
                }
                multiplex_buf _tbuf;
            };

        } //inner namespace
        struct Identifiable
        {
            typedef std::string id_t;
            template <class Name>
            Identifiable(Name && name) :
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

        struct detail
        {
            detail()
            {
                _result_multiplex.bind(&_result);
            }
            detail(detail&& other):
                _result(std::move(other._result))
            {
            }
            detail(const detail&) = delete;
            //detail& operator = (detail&&) = default;

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
            _inner::multiplex_stream& as_stream()
            {
                return _result_multiplex;
            }
        private:
            std::ostringstream _result;
            _inner::multiplex_stream _result_multiplex;
        };
        
        inline detail& operator << (detail& det, int t)
        {
            det.as_stream() << (t);
            return det;
        }

        template <class T>
        inline detail& operator << (detail& det, T && t)
        {
            det.as_stream() << std::forward<T>(t);
            return det;
        }
        inline std::ostream& operator << (std::ostream& os, const detail& det)
        {
            os << det.result().c_str();
            return os;
        }

        /**Specialization of exception to distinguish fail from aborted state*/
        struct TestFail : std::logic_error
        {
            TestFail() :
                std::logic_error(nullptr),
                _is_abort(false)
            {
            }
            TestFail(const std::string& text) :
                std::logic_error(text),
                _is_abort(false)
            {
            }
            bool is_abort() const
            {
                return _is_abort;
            }
        protected:
            void set_abort(bool is_abort)
            {
                _is_abort = is_abort;
            }
        private:
            bool _is_abort;
        };

        struct TestAbort : public TestFail
        {
            TestAbort()
            {
                set_abort(true);
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
            typedef std::chrono::high_resolution_clock::time_point time_point_t;
            TestResult(std::shared_ptr<TestSuite>& suite) :
                _suite(suite),
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
                /**some test condition was not met, see #status_details for details*/
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
            
            inline std::ostream& info() const;
            /*
            template<typename T, typename... Us>
            inline std::ostream& error(T &&t, Us &&... us) const;
            */
            detail& status_details()
            {
                return _status_details;
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
            template<class Comparator, class Xetails>
            void assert_that(Comparator && cmp, Xetails &&details)
            {
                if (!std::forward<Comparator>(cmp)())
                {
                    fail(std::forward<Xetails>(details));
                }
            }
            template<class Marker, class Left, class Right, class Xetails>
            void assert_that(Left&& left, Right&& right, Xetails &&details)
            {
                if (!OP::utest::that<Marker>(std::forward<Left>(left), std::forward<Right>(right)))
                {
                    fail(std::forward<Xetails>(details));
                }
            }
            template<class Left, class Right, class Cmp, class Xetails>
            void assert_thax(Left&& left, Right&& right, Cmp cmp, Xetails &&details)
            {
                if (!cmp(std::forward<Left>(left), std::forward<Right>(right)))
                {
                    fail(std::forward<Xetails>(details));
                }
            }
            /**Unconditional fail*/
            template<typename ...Xetails>
            void fail(Xetails&& ...details)
            {
                guard_t g(*_access_result);
                do_log(std::forward<Xetails>(details)...);
                throw TestFail();
            }
        protected:
            void join_stream(std::ostream& os)
            {
                guard_t g(*_access_result);
                _status_details.as_stream().bind(&os);
            }
            void leave_stream(std::ostream& os)
            {
                guard_t g(*_access_result);
                _status_details.as_stream().unbind(&os);
            }
        private:
            void do_log()
            {
            }
            template <class T, class ... Tx>
            void do_log(T && t, Tx && ... tx)
            {
                _status_details << std::forward<T>(t);
                do_log(std::forward<Tx>(tx)...);
            }
            detail _status_details;
            Status _status;
            unsigned _run_number;
            time_point_t _start_time, _end_time;
            std::shared_ptr<TestSuite> _suite;

            typedef std::unique_ptr<std::recursive_mutex> mutex_ptr_t;
            mutex_ptr_t _access_result;
            typedef std::unique_lock<mutex_ptr_t::element_type> guard_t;

        };
        typedef std::shared_ptr<TestResult> test_result_ptr_t;
        /**Abstract definition of test invokation*/
        struct TestCase : public Identifiable
        {
            template <class Name>
            TestCase(Name && name) :
                Identifiable(std::forward<Name>(name))
            {
            }
            virtual ~TestCase(){}
            /**Invoke test single times*/
            TestResult& execute(TestResult& retval)
            {
                retval._start_time = std::chrono::high_resolution_clock::now();
                do_run(retval);
                retval._end_time = std::chrono::high_resolution_clock::now();
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
                    if (!result) //warm-up failed
                        return result;
                }
                result._start_time = std::chrono::high_resolution_clock::now();
                result._run_number = 0;
                for (; run_number; --run_number, ++result._run_number)
                {
                    do_run(result);
                }
                result._end_time = std::chrono::high_resolution_clock::now();
                result._status = TestResult::ok;
                return result;
            }
        protected:
            virtual void run(TestResult& retval) = 0;
        private:
            void do_run(TestResult& retval)
            {
                try
                {
                    this->run(retval);
                    retval._status = TestResult::ok;
                }
                catch (TestFail const &e)
                {
                    retval._end_time = std::chrono::high_resolution_clock::now();
                    retval._status = e.is_abort() ? TestResult::aborted : TestResult::failed;
                    if (e.what() && *e.what())
                    {
                        if (!retval._status_details.is_empty())
                            retval._status_details << "\n";
                        retval._status_details << e.what();
                    }
                }
                catch (std::exception const &e)
                {
                    retval._end_time = std::chrono::high_resolution_clock::now();
                    retval._status = TestResult::exception;
                    if (e.what() && *e.what())
                    {
                        if (!retval._status_details.is_empty())
                            retval._status_details << "\n";
                        retval._status_details << e.what();
                    }
                }
                catch (...)
                { //hide any other exception
                    retval._end_time = std::chrono::high_resolution_clock::now();
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


            TestSuite* declare(std::function<void(TestResult&)> f, std::string n = std::string())
            {
                std::string name = (n.empty()) ? typeid(f).name() : std::string(std::move(n));
                return this->declare_case(
                    std::make_shared<FunctionalTestCase<decltype(f)> >(
                    name,
                    std::move(f)
                    )
                    );
            }
            template < class Name = const char*>
            TestSuite* declare(std::function<void(void)> f, std::string n = std::string())
            {
                std::string name = (n.empty()) ? typeid(f).name() : std::string(std::move(n));
                std::shared_ptr<TestCase> pt(new FunctionalTestCase<decltype(f)>(
                    name,
                    std::move(f)
                    ));
                return this->declare_case(pt);
            }
            TestSuite* declare(void (f)(), std::string n = std::string())
            {
                auto fwrap = std::function< void() >(f);
                std::string name = (n.empty()) ? typeid(n).name() : std::string(std::move(n));
                std::shared_ptr<TestCase> pt(new FunctionalTestCase<decltype(fwrap)>(
                    name,
                    std::move(fwrap)
                    ));
                return this->declare_case(pt);

            }
            TestSuite* declare(void (f)(TestResult&), std::string n = std::string())
            {
                auto fwrap = std::function< void(TestResult&) >(f);
                std::string name = (n.empty()) ? typeid(n).name() : std::string(std::move(n));
                std::shared_ptr<TestCase> pt(new FunctionalTestCase<decltype(fwrap)>(
                    name,
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

            template <class F>
            struct FunctionalTestCase : public TestCase
            {
                template <class Name>
                FunctionalTestCase(Name && name, F&& f) :
                    TestCase(std::forward<Name>(name)),
                    _function(std::forward<F>(f))
                {
                }
            protected:
                void run(TestResult& retval)
                {
                    _function(retval);
                }
            private:
                F _function;
            };
        };
        /** Specialization for functions without arguments, it can use OP_UTEST_ASSERT
        * instead of access to TestResult methods
        */
        template <>
        void TestSuite::FunctionalTestCase< std::function<void()> >::run(TestResult& retval)
        {
            _function();
        }
        inline std::ostream& TestResult::info() const
        {
            return _suite->info();
        }
        /*
        template<typename T, typename... Us>
        inline std::ostream& TestResult::error(T &&t, Us &&... us) const
        {
            guard_t g(_access_result);
            _suite->error() << std::forward<T>(t);
            error(std::forward<Us>(us)...);
        }
        */
        template <class Name>
        inline std::shared_ptr<TestSuite> default_test_suite(Name && name)
        {
            auto r = std::make_shared<TestSuite>(std::forward<Name>(name), std::cout, std::cerr);
            TestRun::default_instance().declare(r);
            return r;
        };

        struct TestReport
        {

        };
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
        private:
            bool _intercept_sig_abort;
            std::uint16_t _output_width;
        };
        struct TestRun
        {
            typedef std::shared_ptr<TestSuite> test_suite_ptr;
            TestRun(TestRunOptions options = TestRunOptions())
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
            std::vector< test_result_ptr_t > run_if(F &f)
            {
                std::vector<std::shared_ptr<TestResult> > result;
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
                        std::shared_ptr<TestResult> result = std::make_shared<TestResult>(suite.shared_from_this());
                        //allow output error right after appear
                        result->join_stream(suite.error());
                        suite.info() <<"\t["<<test.id()<<"]...\n";
                        test.execute(*result);

                        suite.info()
                            <<"\t["<<test.id()<< "] done with status:"
                            <<result->status_to_str()
                            << " in:"<<std::fixed<< result->ms_duration()<< "ms\n";
                        result->leave_stream(suite.error());
                        f(std::move(result));
                    }
                    return true;//always continue
                });
            }

            typedef std::multimap<Identifiable::id_t, std::shared_ptr<TestSuite> > suites_t;
            suites_t _suites;
            TestRunOptions _options;
        };
        namespace tools
        {
            inline std::string& randomize(std::string& target, size_t max_size, size_t min_size = 0)
            {
                target.clear();
                if (!max_size || min_size > max_size)
                    return target;
                auto l = (min_size == max_size) ? min_size : (
                    (std::rand() % (max_size - min_size)) + min_size);
                target.reserve(l);
                while (l--)
                    target += (static_cast<char>(std::rand() % ('_' - '0')) + '0');
                return target;
            }
            template <class V, class T, class F >
            inline V& randomize(V& target, size_t max_size, size_t min_size = 0, F & value_factory = random_value<T>)
            {
                target.clear();
                if (!max_size || min_size > max_size)
                    return target;
                auto l = (min_size == max_size) ? min_size : (
                    (std::rand() % (max_size - min_size)) + min_size);
                for (std::insert_iterator a(target, std::begin(target)); l--; ++a)
                    *a = value_factory();
                return target;
            }

            template<class T>
            inline T random_value()
            {
                return static_cast<T>(std::rand());
            }
            template<>
            inline std::uint64_t random_value<std::uint64_t>()
            {
                return (static_cast<std::uint64_t>(std::rand()) << 32)
                    | static_cast<std::uint64_t>(std::rand());
            }

            template<>
            inline std::string random_value<std::string>()
            {
                std::string r;
                return randomize(r, 256);
            }
            template <class Container1, class Container2, class ErrorHandler>
            inline bool compare(const Container1& co1, const Container2& co2, ErrorHandler& on_error = [](const Container2::value_type& v){})
            {
                std::multiset<Container1::value_type> s1(std::begin(co1), std::end(co1));
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
            template <class A>
            inline bool sign_tolerant_cmp(A left, A right)
            {
                return static_cast< std::make_unsigned<A>::type >(left) == 
                    static_cast< std::make_unsigned<A>::type >(right);
            }
            inline bool sign_tolerant_cmp(char left, unsigned char right)
            {
                return (unsigned char)left == right;
            }
        }
    } //utest
}//OP
#endif //_UNIT_TEST__H_78eda8e4_a367_427c_bc4a_0048a7a3dfd1