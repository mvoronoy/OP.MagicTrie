#pragma once

#ifndef _OP_TRIE_SERVANTS_TRIEMEMORYSERVICEIMPL__H_
#define _OP_TRIE_SERVANTS_TRIEMEMORYSERVICEIMPL__H_


#include <op/common/DependencyManager.h>

#include <op/vtm/managers/EventSourcingSegmentManager.h>
#include <op/vtm/managers/InMemMemoryChangeHistory.h>

#include <op/trie/TrieOptions.h>

namespace OP::trie::servants
{
    namespace opc = OP::common;

    struct TrieMemoryServiceImpl
        : TrieMemoryService
        , TrieTransactionService
        , public opc::DependencyManager< 
            opc::expecting<OP::vtm::SegmentManager>(),
            opc::expecting<OP::utils::ThreadPool>(),
            opc::expecting<TrieResidenceService>()
          >
    {

        //
        //  TrieMemoryService
        //
        [[nodiscard]] virtual vtm::ReadonlyMemoryChunk readonly_block(
            FarAddress pos, segment_pos_t size) const noexcept override
        {
            return _transacted_segment_manager->readonly_block(pos, size);
        }

        [[nodiscard]] virtual vtm::MemoryChunk writable_block(
            FarAddress pos, segment_pos_t size, bool need_previous_content) override
        {
            return _transacted_segment_manager->writable_block(pos, size,
                need_previous_content ? vtm::WritableBlockHint::update_c : vtm::WritableBlockHint::new_c
            );
        }

        //
        //  TrieTransactionService 
        //

        [[nodiscard]] virtual transaction_ptr_t begin_transaction() override
        {
            return _transacted_segment_manager->begin_transaction();
        }

        [[nodiscard]] virtual event_supplier_t& event_supplier() override
        {
            return _transacted_segment_manager->event_supplier();
        }

        [[nodiscard]] virtual std::optional<transaction_id_t> current_transaction_id() const override
        {
            return _transacted_segment_manager->current_transaction_id();
        }

    protected:
        virtual void on_complete() override
        {
            auto& threads = resolve<OP::utils::ThreadPool>();
            auto& base_segment_manager = resolve<vtm::SegmentManager>();
            auto& residence = resolve<TrieResidenceService>();

            _transacted_segment_manager = std::make_unique<vtm::EventSourcingSegmentManager>(
                    base_segment_manager,
                    std::shared_ptr<vtm::MemoryChangeHistory>(
                        new vtm::InMemoryChangeHistory(threads))
            );
            
        }

    private:
        enum ServiceId
        {
            residence_manager = 0x1,
            trie_header       = 0x2,
            append_pool       = 0x3
        };
        std::unique_ptr<vtm::EventSourcingSegmentManager> _transacted_segment_manager;
    };

};

#endif //_OP_TRIE_SERVANTS_TRIEMEMORYSERVICEIMPL__H_
