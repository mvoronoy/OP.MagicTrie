#pragma once

#include <any>

#include <op/common/ThreadPool.h>

#include <op/vtm/InMemMemoryChangeHistory.h>

namespace test
{
    /** \brief Init function to create InMemoryChangeHistory instance for `TestSuite::with_fixture` method.
    *   Must be used together with #tear_down_InMemoryChangeHistory to deallocate resources
    */
    inline std::any init_InMemoryChangeHistory(OP::utest::TestSuite& tsuite)
    {
        using namespace OP::vtm;
        OP::utils::ThreadPool* tp = new OP::utils::ThreadPool;
        return std::shared_ptr<MemoryChangeHistory>(
            new InMemoryChangeHistory(*tp),
            [tp](MemoryChangeHistory* to_delete){
                delete to_delete;
                delete tp;
            }
        );
    }

    /** \brief tear-down resources previously allocated by #init_InMemoryChangeHistory.
    */
    inline void tear_down_InMemoryChangeHistory(std::any& resources)
    {
            /*do nothing as custom deleter of std::shared_ptr<MemoryChangeHistory> deletes ThreadPool*/

    //    auto mem_change_history = std::any_cast<std::shared_ptr<MemoryChangeHistory>>(resources);
    //    auto narrow_instance = std::dynamic_pointer_cast<InMemoryChangeHistory>(mem_change_history);
    //    delete (& narrow_instance->thread_pool());
    //    narrow_instance.reset();
    //    mem_change_history.reset();
    }


} //ns:test
