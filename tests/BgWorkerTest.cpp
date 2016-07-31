#include <op/utest/unit_test.h>
#include <op/utest/unit_test_is.h>
#include <iostream>

#include <atomic>
#include <OP/service/BackgroundWorker.h>


void test_BackgroundWorkerGeneral(OP::utest::TestResult &tresult)
{
	struct BGImpl : public OP::service::BackgroundWorker
	{
		BGImpl() 
		{
		}
	};
	BGImpl pool;
	std::atomic<int> x1(0);

	pool.push([&]() {
		x1++;
	});
	for (int i = 0; i < 10000; ++i)
	{
		if (x1.load() > 0)
			break;
		std::this_thread::yield();
	}
	tresult.assert_true(x1.load() == 1);
}
static auto module_suite = OP::utest::default_test_suite("Background worker")
->declare(test_BackgroundWorkerGeneral, "general")
;
