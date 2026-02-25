#include <iostream>
#include <variant>
#include <cassert>
#include <array>

#include <op/common/EventSupplier.h>

#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>


namespace
{
    enum class SomeEv : int
    {
        a = 0, b, c
    };

    using a_payload = std::tuple<int>;
    using b_payload = std::tuple<int, float, const char*>;
    using c_payload = std::tuple<int, int, int>;

    const char expected_b_value[] = "lhsAjkfy836qfCvd*";

    using supplier_t = OP::events::EventSupplier<
        OP::Assoc<SomeEv::a, a_payload>,
        OP::Assoc<SomeEv::b, b_payload>,
        OP::Assoc<SomeEv::c, c_payload>>;
    
    static supplier_t _fix_issue_inst;

    using namespace std::placeholders;
    using namespace OP::utest;

    struct TestProgram
    {
        using unsubscriber_t = typename supplier_t::unsubscriber_t;

        OP::utest::TestRuntime& _runtime;
        std::array<unsubscriber_t, 3> _b_unsub;

        size_t 
            _evidence_of_a = 0, 
            _evidence_of_b = 0,
            _evidence_of_c = 0;

        TestProgram(supplier_t& sup, OP::utest::TestRuntime& runtime)
            : _runtime(runtime)
            , _b_unsub{
                sup.on<SomeEv::b>(std::bind(&TestProgram::handle_b, this, _1, _2)),
                sup.on<SomeEv::b>(std::bind(&TestProgram::handle_b2, this, _1)),
                sup.on<SomeEv::b>(std::bind(&TestProgram::handle_b3, this)) 
            }
        {
        }

        //handler can use explicitly declared payload
        void handle_a(const a_payload&)
        {
            ++_evidence_of_a;
            _runtime.debug() << "TestProgram::handle_a has been called\n";
        }

        //handler can use decomposed by field payload 
        void handle_a2(int x)
        {
            ++_evidence_of_a;
            _runtime.debug() << "TestProgram::::handle_a2 ("<< x <<")been called\n";
        }

        void handle_b(int a, float b)
        {
            ++_evidence_of_b;
            _runtime.debug() << "TestProgram::handle_b(" << a << ", " << b << "f) been called\n";
        }

        void handle_b2(const char* bstr)
        {
            ++_evidence_of_b;
            _runtime.assert_that<equals>(bstr, expected_b_value);
            _runtime.debug() << "TestProgram::handle_b2 ignoring int and float args:(" << bstr << ") been called\n";
        }

        void handle_b3()
        {
            ++_evidence_of_b;
            _runtime.debug() << "TestProgram::handle_b3 ignore all args:() been called\n";
        }

        void handle_c()
        {
            ++_evidence_of_c;
            _runtime.debug() << "TestProgram::handle_c ignore all args:() been called\n";
        }
    };

    void test_General(OP::utest::TestRuntime& result) 
    {
        supplier_t supplier;

        TestProgram program(supplier, result);

        if(1==1)
        {
            auto unsub1 = std::make_tuple(
                supplier.on<SomeEv::a>(std::bind(&TestProgram::handle_a, &program, _1)),
                //can subscribe on same event (SomeEv::a) and same method even twice if needed
                supplier.on<SomeEv::a>(std::bind(&TestProgram::handle_a, &program, _1)),
                //same event (SomeEv::a) but another handler and using #subscribe
                supplier.subscribe(SomeEv::a, std::bind(&TestProgram::handle_a2, &program, _1))
            );

            supplier.send<SomeEv::b>({ 1, 15.f, expected_b_value }); //makes payload b_payload then send all subscribers (so far single Program::handle_b)
            supplier.send<SomeEv::a>(57); // send all subscribers 57 as a_payload
            supplier.send<SomeEv::c>({ 0, 0, 0 }); // send to nowhere, since no single subscriber 

            result.assert_that<equals>(program._evidence_of_a, 3);
            result.assert_that<equals>(program._evidence_of_b, 3);
            result.assert_that<equals>(program._evidence_of_c, 0);
            //unsubscribe all `a`
        }
        supplier.send<SomeEv::a>(57); // send 57 to nowhere
        result.assert_that<equals>(program._evidence_of_a, 3,
            "unsubscribe all `a` failed, send `a` to nowhere must keep the same evidence value");

        result.info() << "Test unsubscribe is allowed during event handling...\n";
        typename supplier_t::unsubscriber_t hx;
        size_t hx_invoke_count = 0;
        hx = supplier.on<SomeEv::c>([&]() {
            ++hx_invoke_count;
            hx->unsubscribe();
            });

        supplier.send<SomeEv::c>({ 0, 0, 0 });
        result.assert_that<equals>(hx_invoke_count, 1);
        result.assert_that<equals>(program._evidence_of_c, 0);

        auto c_unsub = supplier.on<SomeEv::c>(std::bind(&TestProgram::handle_c, &program));
        supplier.send<SomeEv::c>({ 0, 0, 0 });
        result.assert_that<equals>(program._evidence_of_c, 1, "check later bind success");
        result.assert_that<equals>(hx_invoke_count, 1, "no more increments for unsubscribed handler");
    }

    static auto& module_suite = OP::utest::default_test_suite("EventSupplier")
        .declare("general", test_General)
        ;

    //OP_DECLARE_TEST_CASE("EventSupplier", "general",
    //    []);
} //ns:
