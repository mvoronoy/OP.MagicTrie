#include <iostream>
#include <cassert>
#include <concepts>
#include <span>

#include <op/common/DependencyManager.h>
#include <op/common/SequenceReorder.h>
#include <op/utest/unit_test.h>

namespace 
{

    using namespace OP::utest;
    using namespace std::string_literals;

    namespace test
    {
        using namespace OP::common;
        struct RawMemoryManagerMarker {};
        //Example interface
        struct RawMemoryManager
        {
            using view_t = std::span<std::uint8_t>;

            bool _marker_constructor_called = false;
            constexpr RawMemoryManager() = default;
            constexpr RawMemoryManager(RawMemoryManagerMarker) noexcept
                : _marker_constructor_called(true)
            {
            }

            virtual ~RawMemoryManager() noexcept = default;
            virtual view_t method(unsigned offset) = 0;
        };
        // Example interface implementation
        struct RawMemoryManagerImpl : RawMemoryManager 
        {
            std::uint8_t _data[16]{};
            constexpr RawMemoryManagerImpl() = default;

            constexpr RawMemoryManagerImpl(RawMemoryManagerMarker marker) noexcept
                : RawMemoryManager(marker)
            {
            }
            
            virtual view_t method(unsigned offset) override
            { 
                assert(offset < sizeof(_data));
                return view_t{_data, offset}; 
            }
        };

        //Example service
        struct ThreadService 
        { 
            ThreadService(const ThreadService&) = delete;
            ThreadService() = default;
            void run() {} 
        };

        enum class PlainEnum
        {
            abc,
            xyz
        };

        // Example dependency resolver implementation without defaults
        struct TransactionalMemoryManager : 
            DependencyManager< expecting<RawMemoryManager>(), expecting<ThreadService>(), expecting<PlainEnum>()>
        {
            using DependencyManager::base_t;    

            const static inline std::string virtual_constructor_evidence_c{ "TransactionalMemoryManager::on_complete" };

            TransactionalMemoryManager(OP::utest::TestRuntime& tresult)
                : _tresult(tresult)
            {
            }

            auto at(unsigned offset)
            {
                return resolve<RawMemoryManager>().method(offset);
            }

        protected:
            virtual void on_complete()
            {
                _tresult.set_evidence(virtual_constructor_evidence_c, 1);
                _tresult.debug() 
                    << "virtual constructor of `TransactionalMemoryManager` called with:\n"
                    << "\tts = "  << &base_t::resolve<ThreadService>() << '\n'
                    << "\tmem = " << &base_t::resolve<RawMemoryManager>()
                    << "\n";
                _tresult.assert_true(&base_t::resolve<ThreadService>());
                _tresult.assert_true(&base_t::resolve<RawMemoryManager>());
            }
        private:
            OP::utest::TestRuntime& _tresult;
        };


        static auto sample_factory = []() -> RawMemoryManagerImpl {
            //std::cout << "sample_factory called...\n";
            return RawMemoryManagerImpl{ RawMemoryManagerMarker {} };
        };

        struct SomeLateManager : DependencyManager< 
            expecting<RawMemoryManager>(), 
            expecting<ThreadService>(),
            or_default_factory(sample_factory) >
        {
            using DependencyManager::base_t;
            const static inline std::string report_evidence_c = "SomeLateManager::report"s;
            const std::string virtual_constructor_evidence;//unique for each class instance

            SomeLateManager(OP::utest::TestRuntime& tresult)
                : _tresult(tresult)
                , virtual_constructor_evidence(std::format("SomeLateManager#{}::on_complete", instance_unique_id()))
            {
                tresult.set_evidence(virtual_constructor_evidence, 0);
            }

            RawMemoryManager& get_memory_manager()
            {
                return resolve<RawMemoryManager>();
            }

            ThreadService& get_transaction_service()
            {
                return resolve<ThreadService>();
            }

            void report()
            {
                _tresult.set_evidence(report_evidence_c, 1);

                _tresult.debug() << "Owns:\n"
                    << "\tts = " << &base_t::resolve<ThreadService>() << '\n'
                    << "\tmem = " << &base_t::resolve<RawMemoryManager>() << "\n";
                _tresult.assert_true(&base_t::resolve<ThreadService>(), "ThreadService is not assigned");
                _tresult.assert_true(&base_t::resolve<RawMemoryManager>(), "RawMemoryManager interface is not assigned");

                _tresult.assert_true(base_t::resolve<RawMemoryManager>()._marker_constructor_called,
                    "RawMemoryManagerImpl constructed with wrong constructor");
                
            }
        protected:
            
            virtual void on_complete()
            {
                ++_tresult.evidence<int>(virtual_constructor_evidence);
            }

        private:
            static int instance_unique_id() noexcept
            {
                static int id_gen = 0;
                return ++id_gen;
            }
            OP::utest::TestRuntime& _tresult;
        };

    }//ns:test
    template <class DG>
    struct PubExpl
    {
        using tuple_t = typename DG::instances_tuple_t;

        template <size_t I, size_t J>
        static void callback()
        {
            std::cout << "--------------------\n"
                << "I[" << I << "] = " << typeid(std::tuple_element_t<I, tuple_t>).name()
                << "\nJ[" << J << "] = " << typeid(std::tuple_element_t<J, tuple_t>).name();
            using reorder_p_t = typename DG::ReorderServicePredicate;

            std::cout << "\nmutual = " << std::boolalpha << reorder_p_t::template predicate<I, J> << "\n";

        }
    };

    //template <typename F, size_t ...Ix>
    //void explore_pair(std::index_sequence<Ix...>);
    template <typename F, size_t I0>
    void explore_pair(std::index_sequence<I0>)
    {
    }
    template <typename F>
    void explore_pair()
    {
    }

    template <typename F, size_t I0, size_t I1, size_t ...Ix>
    void explore_pair(std::index_sequence<I0, I1, Ix...>)
    {
        F::template callback<I0, I1>();
        explore_pair<F>(std::index_sequence<I0, Ix...>{});
        explore_pair<F>(std::index_sequence<I1, Ix...>{});
        //explore_pair<F>(std::index_sequence<Ix...>{});
    }


    void test_Basic(OP::utest::TestRuntime& tresult) 
    {
        test::ThreadService reference_service;
        OP::common::ServiceRegistry registry;
        using tmm = decltype(test::TransactionalMemoryManager(tresult));

        std::cout << std::boolalpha 
            <<"of-threads=" << tmm::has_dependency_of_v<test::ThreadService> << "\n"
            <<"of-enum=" << tmm::has_dependency_of_v<test::PlainEnum> << "\n"
            << "of-RawMemoryManager=" << tmm::has_dependency_of_v<test::RawMemoryManager> << "\n"
            << "of-self=" << tmm::has_dependency_of_v<test::TransactionalMemoryManager> << "\n"
            ;

        auto& all1 = registry.resolve_dependencies(
            std::unique_ptr<test::RawMemoryManager>{ new test::RawMemoryManagerImpl(test::RawMemoryManagerMarker{}) },
            std::ref(reference_service),
            test::TransactionalMemoryManager(tresult),
            test::PlainEnum::abc
        );
        tresult.check_evidence<equals>(
            test::TransactionalMemoryManager::virtual_constructor_evidence_c,
            1,
            OP_CODE_DETAILS("virtual constructor wasn't called")
        );

        using dg_t = std::decay_t<decltype(all1)>;
        static_assert(std::is_same_v<
            dg_t::constructor_order_index_t, 
            OP::common::SequenceReorder<0, 1, 3, 2>>, "not the same constructor index");
        static_assert(std::is_same_v<
            dg_t::destructor_order_index_t, 
            OP::common::SequenceReorder<2, 3, 1, 0>>, "not the same destructor index");
    }

    void test_OrDefault(OP::utest::TestRuntime& tresult)
    {
        test::ThreadService reference_service;
        OP::common::ServiceRegistry registry;

        auto& dep_graph1 = registry.resolve_dependencies(
            std::ref(reference_service),
            test::SomeLateManager{ tresult } //virtual constructor going to call default factory
        
        );

        tresult.check_evidence<equals>(
            dep_graph1.get<test::SomeLateManager>().virtual_constructor_evidence, //uses uniq id of each SomeLateManager instance
            1,
            OP_CODE_DETAILS("virtual constructor wasn't called")
        );

        dep_graph1.get<test::SomeLateManager>().report();

        tresult.check_evidence<equals>(
            test::SomeLateManager::report_evidence_c,
            1,
            OP_CODE_DETAILS("test method wasn't called")
        );

        //test can call twice, test default isn't same as provided
        auto& dep_graph2 = registry.resolve_dependencies(
            std::shared_ptr<test::RawMemoryManager>{ new test::RawMemoryManagerImpl(test::RawMemoryManagerMarker{}) },
            std::ref(reference_service),
            test::SomeLateManager(tresult)
        );
        tresult.check_evidence<equals>(
            dep_graph2.get<test::SomeLateManager>().virtual_constructor_evidence, //uses uniq id of each SomeLateManager instance
            1,
            OP_CODE_DETAILS("virtual constructor wasn't called")
        );


        tresult.assert_that<not_equals>(
            &dep_graph1.get<test::SomeLateManager>().get_memory_manager(),
            &dep_graph2.get<test::SomeLateManager>().get_memory_manager(),
            OP_CODE_DETAILS("2 instances must own different references of resolved RawMemoryManager, because of `or_default_factory`")
        );
        
        // but test::ThreadService must be the same:

        tresult.assert_that<equals>(
            &dep_graph1.get<test::SomeLateManager>().get_transaction_service(),
            &dep_graph2.get<test::SomeLateManager>().get_transaction_service(),
            OP_CODE_DETAILS("2 instances must own the same references to TransactionService")

            );

    }

    namespace test
    {
        using namespace OP::common;

        using call_order_vector_t = std::vector<int>;

        template <int n, class ... Bx>
        struct TestLifecycleOrder : public 
                //define dependency of multiple types
                OP::common::DependencyManager<expecting<Bx>()...>
        {
            constexpr static int nid_c = n;

            call_order_vector_t& _call_order;
            bool _moved_out = false;

            TestLifecycleOrder(call_order_vector_t& call_order)
                :_call_order(call_order)
            {
            }
            //control moving semantic to avoid redundant destructor count
            TestLifecycleOrder(TestLifecycleOrder&& other) noexcept
                : _call_order(other._call_order)
            {
                other._moved_out = true;
            }

            ~TestLifecycleOrder()
            {
                if(!_moved_out)
                    _call_order.push_back(n); 
            }

            int test_service(int base) const
            {
                return base * n;
            }

        protected:
            void on_complete() override
            {
                _call_order.push_back(n);
            }

        };
        
        using base_dependency_low_t = TestLifecycleOrder<1>;
        using base_dependency_middle_t = TestLifecycleOrder<2, base_dependency_low_t>;
        using base_dependency_high_t = TestLifecycleOrder<3, base_dependency_low_t, base_dependency_middle_t, PlainEnum>;
        using stand_alone_t = TestLifecycleOrder<4>;

        struct TestDependsOn : OP::common::DependencyManager<
            expecting<base_dependency_middle_t>(),
            expecting<base_dependency_low_t>(),
            expecting<base_dependency_high_t>(),
            expecting<stand_alone_t>()
        >
        {
            ~TestDependsOn()
            {
                std::cout << "~TestDependsOn\n";
            }

            int crc() const
            {
                return resolve<base_dependency_middle_t>().test_service(1) +
                    resolve<base_dependency_low_t>().test_service(10) +
                    resolve<base_dependency_high_t>().test_service(100) +
                    resolve<stand_alone_t>().test_service(1000);
            }
        };
    }//ns:test

    void test_Dtor(OP::utest::TestRuntime& tresult)
    {
        using namespace test;
        call_order_vector_t test_order, copy_of_order;
        {
            OP::common::ServiceRegistry registry;
            auto& all1 = registry.resolve_dependencies(
                /*0*/base_dependency_high_t{ test_order },
                /*1*/std::unique_ptr<base_dependency_middle_t>{new base_dependency_middle_t{ test_order }},
                /*2*/base_dependency_low_t{ test_order },
                /*3*/stand_alone_t{ test_order },
                /*4*/PlainEnum::abc
            );
            for(const auto& n: test_order)
                tresult.debug() << n << ", ";
            tresult.debug() << '\n';
            tresult.assert_that<equals>(test_order, std::vector{1, 2, 4, 3});
            copy_of_order = test_order;
            test_order.clear();
        } //destructors must populate test_order once again in reverse
        std::reverse(test_order.begin(), test_order.end());
        for (const auto& n : test_order)
            tresult.debug() << n << ", ";
        tresult.debug() << '\n';
        tresult.assert_that<equals>(copy_of_order, test_order);
    }
    
    static auto& module_suite = OP::utest::default_test_suite("DependencyManager")
        .declare("basic", test_Basic)
        .declare("or_default", test_OrDefault)
        .declare("dtor", test_Dtor)
    ;

}//ns:

