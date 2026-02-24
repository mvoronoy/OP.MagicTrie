#pragma once

#include <any>

#include <op/common/ThreadPool.h>

#include <op/vtm/managers/InMemMemoryChangeHistory.h>

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

} //ns:test
