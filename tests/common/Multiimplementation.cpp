#include <op/utest/unit_test.h>
#include <op/common/StackAlloc.h>

using namespace OP::utest;
namespace {
    constexpr int is_called_from_const = 0x8000;
    constexpr int is_impl1 = 0x10000;
    constexpr int is_impl2 = 0x20000;

    struct Abstract
    {
        int _data;
        virtual ~Abstract() = default;
        virtual int method() = 0;
        virtual int method() const = 0;
    protected:
        Abstract(int arg) :_data(arg) {};
    };

    struct TestImpl1 : Abstract
    {
        TestImpl1(int ini)
            : Abstract(ini)
        {

        }
        virtual int method()
        {
            return _data | is_impl1;
        }
        virtual int method() const
        {
            return _data | is_impl1 | is_called_from_const;
        }
    };

    struct TestImpl2 : Abstract
    {
        TestImpl2(int ini)
            : Abstract(ini)
        {
        }

        virtual int method()
        {
            return _data | is_impl2;
        }
        virtual int method() const
        {
            return _data | is_impl2 | is_called_from_const;
        }
    };

    struct NoVDtorInterface
    {
        // no virtual destructor declaration
        virtual void some() = 0;
    };
    
    template <class T>
    struct NoVDtorImpl : NoVDtorInterface
    {
        static_assert(!std::has_virtual_destructor_v<NoVDtorInterface >,
            "For the test purpose base destructor must not be virtual");
        T t{};
        static inline int destructor_called = 0;
        
        ~NoVDtorImpl()
        {
            ++destructor_called;
        }

        virtual void some()
        {
            t += t * (t-1);
        }
    };
    void test_Construct(OP::utest::TestRuntime& tresult)
    {
        using test_t = OP::Multiimplementation<Abstract, TestImpl1, TestImpl2>;
        test_t reserved_memory;
        tresult.assert_false(reserved_memory.has_value() || reserved_memory); //check both methods
        reserved_memory.construct<TestImpl1>(11);
        tresult.assert_true(reserved_memory.has_value() && reserved_memory); //check both methods
        tresult.assert_true(reserved_memory->method() & is_impl1);
        tresult.assert_that<equals>(reserved_memory->method() & ~(is_impl1), 11);
        tresult.assert_true(const_cast<const test_t&>(reserved_memory)->method() & is_called_from_const);

        tresult.assert_exception<std::runtime_error>([&]() {
            reserved_memory.construct<TestImpl2>(17);
        });

        reserved_memory = TestImpl2(17);
        tresult.assert_that<equals>(reserved_memory->method() & ~(is_impl2), 17);
        tresult.assert_true(reserved_memory->method() & is_impl2);
        tresult.assert_true(const_cast<const test_t&>(reserved_memory)->method() & is_called_from_const);

        test_t reserved_memory2;
        reserved_memory = reserved_memory2;
        tresult.assert_false(reserved_memory.has_value());

        reserved_memory2.construct<TestImpl1>(57);
        
        reserved_memory = reserved_memory2;
        tresult.assert_true(reserved_memory.has_value());
        tresult.assert_true(reserved_memory->method() & is_impl1);
        tresult.assert_that<equals>(reserved_memory->method() & ~(is_impl1), 57);

        reserved_memory = TestImpl1(11); //assign the same type
        reserved_memory = std::move(reserved_memory2);
        tresult.assert_false(reserved_memory2.has_value());

        tresult.assert_true(reserved_memory.has_value());
        tresult.assert_true(reserved_memory->method() & is_impl1);
        tresult.assert_that<equals>(reserved_memory->method() & ~(is_impl1), 57);

        reserved_memory2 = TestImpl2(17); //change the type

        reserved_memory = reserved_memory2;
        tresult.assert_true(reserved_memory.has_value());
        tresult.assert_true(reserved_memory2.has_value());
        tresult.assert_true(reserved_memory->method() & is_impl2);
        tresult.assert_that<equals>(reserved_memory->method() & ~(is_impl2), 17);

    }

    void test_Destruct(OP::utest::TestRuntime& tresult)
    {
        using test_t = OP::Multiimplementation<Abstract, TestImpl1, TestImpl2>;
        test_t reserved_memory;
        reserved_memory.construct<TestImpl1>(11);
        tresult.assert_true(reserved_memory.has_value());
        reserved_memory.destroy();
        tresult.assert_exception<OP::not_initialized_error>([&]() {
            tresult.assert_true(reserved_memory->method() & is_impl1);
            });
        tresult.assert_false(reserved_memory.has_value());
    }

    void test_DestructNoVDtor(OP::utest::TestRuntime& tresult)
    {
        using impl1_t = NoVDtorImpl <float>;
        using impl2_t = NoVDtorImpl <int>;
        using test_t = OP::Multiimplementation < NoVDtorInterface,
            impl1_t, impl2_t>;

        {//scope to call destructor
            test_t reserved_memory;
            reserved_memory.construct<impl1_t>();
            tresult.assert_true(reserved_memory.has_value());
        }
        tresult.assert_that<equals>(1, impl1_t::destructor_called);
        tresult.assert_that<equals>(0, impl2_t::destructor_called);

        test_t reserved_memory2;
        reserved_memory2.construct<impl2_t>();
        reserved_memory2.destroy();
        tresult.assert_false(reserved_memory2.has_value());
        tresult.assert_that<equals>(1, impl1_t::destructor_called);
        tresult.assert_that<equals>(1, impl2_t::destructor_called);

        impl1_t::destructor_called = impl2_t::destructor_called = 0;

        test_t reserved_memory3(impl2_t{});
        reserved_memory3 = impl1_t{};//+1 destructor
        tresult.assert_that<equals>(1, impl1_t::destructor_called);
        tresult.assert_that<equals>(2, impl2_t::destructor_called);

    }

    static auto& module_suite = OP::utest::default_test_suite("Multiimplementation")
        .declare("construct", test_Construct)
        .declare("destruct", test_Destruct)
        .declare("destruct-no-vdtor", test_DestructNoVDtor)
        ;
}