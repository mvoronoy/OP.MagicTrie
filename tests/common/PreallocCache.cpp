#include <op/utest/unit_test.h>

#include <op/common/PreallocCache.h>

namespace
{
    using namespace OP::utest;

    // virtual base for pointer cast
    struct A 
    {  
        explicit A(OP::utest::TestRuntime& tresult)
            :_tresult(tresult){}

        virtual ~A() = default; 

        virtual void say() = 0;

        OP::utest::TestRuntime& _tresult;
    };

    struct B : A
    { 
        explicit B(OP::utest::TestRuntime& tresult, int* evidence)
            : A{tresult}
            , _evidence{evidence}
        {
        }

        virtual void say()
        {
            ++*_evidence;
            _tresult.debug() << "B::say\n";
        } 
        int* _evidence;
    };


    void test_Basic(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::common;
        PreallocCache <int, 2> prx;
        tresult.assert_that<equals>(prx.statistic()._in_free, 2);
        
        auto p1 = prx.allocate(5);
        tresult.assert_that<equals>(*p1, 5);

        tresult.assert_that<equals>(prx.statistic()._total_allocations, 1);
        tresult.assert_that<equals>(prx.statistic()._times_allocation_used_heap, 0);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 0);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 0);
        tresult.assert_that<equals>(prx.statistic()._in_free, 1);
        
        auto p2 = prx.allocate(7);
        tresult.assert_that<equals>(*p2, 7);

        //should take from heap
        auto p3 = prx.allocate(101);
        tresult.assert_that<equals>(*p3, 101);
        tresult.assert_that<equals>(prx.statistic()._total_allocations, 3);
        tresult.assert_that<equals>(prx.statistic()._times_allocation_used_heap, 1);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 0);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 0);
        tresult.assert_that<equals>(prx.statistic()._in_free, 0);

        p1.reset(); //should return to cache
        tresult.assert_that<equals>(prx.statistic()._in_free, 1);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 1);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 1);

        p3.reset(); //delete to heap
        tresult.assert_that<equals>(prx.statistic()._in_free, 1);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 1);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 2);

        p2 = prx.allocate(8); //on background frees p2(7)
        tresult.assert_that<equals>(*p2, 8);
        tresult.assert_that<equals>(prx.statistic()._in_free, 1);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 2);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 3);

    }

    void test_Inheritance(OP::utest::TestRuntime& tresult)
    {
        using namespace OP::common;

        int evidence = 0;
        PreallocCache<B, 2> b_cache;

        auto b1 = b_cache.allocate(tresult, &evidence);
        tresult.assert_that<equals>(b_cache.statistic()._total_allocations, 1);
        tresult.assert_that<equals>(b_cache.statistic()._times_allocation_used_heap, 0);
        tresult.assert_that<equals>(b_cache.statistic()._total_deallocations, 0);
        tresult.assert_that<equals>(b_cache.statistic()._total_recycle, 0);
        tresult.assert_that<equals>(b_cache.statistic()._in_free, 1);

        // downcast to the parent type
        using a_t = std::unique_ptr<A, typename decltype(b1)::deleter_type>;
        a_t a1 = std::move(b1);
        tresult.assert_that<equals>(b_cache.statistic()._total_allocations, 1);
        tresult.assert_that<equals>(b_cache.statistic()._total_deallocations, 0);
        tresult.assert_that<equals>(b_cache.statistic()._total_recycle, 0);
        tresult.assert_that<equals>(b_cache.statistic()._in_free, 1);

        tresult.assert_that<equals>(evidence, 0);
        a1->say();
        tresult.assert_that<equals>(evidence, 1);
        a1.reset();

        tresult.assert_that<equals>(b_cache.statistic()._total_allocations, 1);
        tresult.assert_that<equals>(b_cache.statistic()._total_deallocations, 1);
        tresult.assert_that<equals>(b_cache.statistic()._total_recycle, 1);
        tresult.assert_that<equals>(b_cache.statistic()._in_free, 2);
    }

    void test_Edge(OP::utest::TestRuntime & tresult)
    {
        using namespace OP::common;
        PreallocCache <int, 1> prx;
        tresult.assert_that<equals>(prx.statistic()._in_free, 1);

        auto p1 = prx.allocate(5);
        tresult.assert_that<equals>(*p1, 5);

        tresult.assert_that<equals>(prx.statistic()._total_allocations, 1);
        tresult.assert_that<equals>(prx.statistic()._times_allocation_used_heap, 0);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 0);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 0);
        tresult.assert_that<equals>(prx.statistic()._in_free, 0);

        auto p2 = prx.allocate(7);
        tresult.assert_that<equals>(*p2, 7);

        tresult.assert_that<equals>(prx.statistic()._total_allocations, 2);
        tresult.assert_that<equals>(prx.statistic()._times_allocation_used_heap, 1);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 0);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 0);
        tresult.assert_that<equals>(prx.statistic()._in_free, 0);

        p1.reset();

        tresult.assert_that<equals>(prx.statistic()._total_allocations, 2);
        tresult.assert_that<equals>(prx.statistic()._times_allocation_used_heap, 1);
        tresult.assert_that<equals>(prx.statistic()._total_deallocations, 1);
        tresult.assert_that<equals>(prx.statistic()._total_recycle, 1);
        tresult.assert_that<equals>(prx.statistic()._in_free, 1);
    }

    static auto& module_suite = OP::utest::default_test_suite("PreallocCache")
        .declare("basic", test_Basic)
        .declare("inheritance", test_Inheritance)
        .declare("edge", test_Edge)
    ;

} //ns:
