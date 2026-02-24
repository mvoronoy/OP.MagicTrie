#pragma once

#include <any>

#include <op/common/ThreadPool.h>

#include <op/vtm/managers/InMemMemoryChangeHistory.h>
#include <op/vtm/managers/AppendOnlyLogFileRotation.h>

namespace test
{
    struct ChangeHistoryFactory
    {
        virtual ~ChangeHistoryFactory() = default;
        virtual std::unique_ptr<OP::vtm::MemoryChangeHistory> create() = 0;

    };

    struct InMemoryChangeHistoryFactory : ChangeHistoryFactory
    {
        virtual std::unique_ptr<OP::vtm::MemoryChangeHistory> create() override
        {
            return std::unique_ptr<OP::vtm::MemoryChangeHistory>(
                new OP::vtm::InMemoryChangeHistory(_tp)
            );
        }

        OP::utils::ThreadPool _tp = OP::utils::ThreadPool(2);
    };

    struct AppendLogFileRotationChangeHistoryFactory : ChangeHistoryFactory
    {
        virtual std::unique_ptr<OP::vtm::MemoryChangeHistory> create() override
        {
            using namespace std::string_literals;
            std::unique_ptr<OP::vtm::CreationPolicy> policy(
                    new OP::vtm::FileCreationPolicy(_tp,
                        OP::vtm::FileRotationOptions{}.transactions_per_file(5),
                        std::filesystem::path("."),
                        "a0"s, ".tlog"s));
            return OP::vtm::AppendLogFileRotationChangeHistory::create_new(_tp, std::move(policy));
        }

        OP::utils::ThreadPool _tp = OP::utils::ThreadPool(2);
    };


    template <class TFactory>
    struct HistoryFactoryTestFixture : OP::utest::TestFixture
    {
        std::any setup(OP::utest::TestSuite&) override
        {
            return std::make_shared<TFactory>();
        }

        virtual void tear_down(OP::utest::TestSuite&, std::any&) noexcept override
        {
            //shared_ptr shutdowns automatically
        }

    };

    /** \brief Init function to create InMemoryChangeHistory instance for `TestSuite::with_fixture` method.
    *   Must be used together with #tear_down_InMemoryChangeHistory to deallocate resources
    */
    template <class TFactory>
    inline std::any memory_change_history_factory()
    {
        return std::shared_ptr<ChangeHistoryFactory>(new TFactory);
    }

    ///** \brief tear-down resources previously allocated by #init_InMemoryChangeHistory.
    //*/
    //inline void tear_down_InMemoryChangeHistory(std::any& resources)
    //{
    //        /*do nothing as custom deleter of std::shared_ptr<MemoryChangeHistory> deletes ThreadPool*/

    ////    auto mem_change_history = std::any_cast<std::shared_ptr<MemoryChangeHistory>>(resources);
    ////    auto narrow_instance = std::dynamic_pointer_cast<InMemoryChangeHistory>(mem_change_history);
    ////    delete (& narrow_instance->thread_pool());
    ////    narrow_instance.reset();
    ////    mem_change_history.reset();
    //}

    ///** \brief Init function to create AppendLogFileRotationChangeHistory instance for `TestSuite::with_fixture` method.
    //*   Must be used together with #tear_down_AppendLogFileRotationChangeHistory to deallocate resources.
    //*/
    //inline std::any init_event_source_with_AppendLogFileRotationChangeHistory(OP::utest::TestSuite& tsuite)
    //{
    //    using namespace std::string_literals;
    //    using namespace OP::vtm;

    //    OP::utils::ThreadPool* tp = new OP::utils::ThreadPool;
    //    std::unique_ptr<CreationPolicy> policy(
    //            new FileCreationPolicy(*tp,
    //                FileRotationOptions{}.transactions_per_file(5),
    //                std::filesystem::path("."),
    //                "a0"s, ".tlog"s));
    //    auto frt = AppendLogFileRotationChangeHistory::create_new(*tp, std::move(policy));

    //    return std::any(
    //        std::static_pointer_cast<MemoryChangeHistory>(frt)
    //    );
    //}

    ///** \brief tear-down resources previously allocated by #init_event_source_with_AppendLogFileRotationChangeHistory.
    //*/
    //inline void tear_down_AppendLogFileRotationChangeHistory(std::any& resources)
    //{
    //    using namespace OP::vtm;

    //    auto change_history = std::any_cast<std::shared_ptr<MemoryChangeHistory>>(resources);
    //    auto narrow_instance = std::dynamic_pointer_cast<AppendLogFileRotationChangeHistory>(change_history);
    //    delete (&narrow_instance->thread_pool());
    //    narrow_instance.reset();
    //    change_history.reset();
    //}

} //ns:test
