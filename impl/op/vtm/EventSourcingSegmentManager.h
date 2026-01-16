#pragma once
#ifndef _OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_
#define _OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_

#include <thread>
#include <shared_mutex>
#include <queue>

#include <op/common/Exceptions.h>
#include <op/common/SpanContainer.h>
#include <op/common/Unsigned.h>

#include <op/flur/flur.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/vtm_error.h>

namespace OP::vtm
{
    enum class ReadIsolation : std::uint32_t
    {
        /** Raise exception on any try to read concurrent. So caller may retry later. */
        Prevent = 10,
        /** Concurrent block reads most recent commit */
        ReadCommitted,
        /** Concurrent block may read uncommitted data */
        ReadUncommitted
    };

    enum class MemoryRequestType : std::uint_fast8_t
    {
        /** block allocated but not usable yet*/
        init = 0,
        ro,
        wr,
        wr_no_history
    };

    /** \brief Interface to manage storage of history change log.
    * 
    * This is supposed to work together with EventSourcingSegmentManager allowing separate memory management operations from
    * transaction support.
    */ 
    struct MemoryChangeHistory
    {
        /** Must use 64 * 64 instead of 'segment_pos_t' as a size because merge of 2 segment_pos_t may produce
        overflow of segment_pos_t */
        using RWR = OP::Range<far_pos_t, far_pos_t>;
        using transaction_id_t = typename Transaction::transaction_id_t;
            
        MemoryChangeHistory() noexcept = default;
        virtual ~MemoryChangeHistory() = default;

        MemoryChangeHistory(const MemoryChangeHistory&) = delete;
        MemoryChangeHistory(MemoryChangeHistory&&) = delete;
        MemoryChangeHistory& operator = (const MemoryChangeHistory&) = delete;
        MemoryChangeHistory& operator =(MemoryChangeHistory&&) = delete;


        /** \brief Inside specific transaction retrieve memory buffer that must be used for 
        *   read or write operations for logical range.
        * 
        * Method takes logical address space `range` and provides buffer where read/write operations
        * must be performed for specific transaction.
        * \param range - logical specification of range;
        * \param tid - transaction id;
        * \param memory_type - type of memory to return;
        * \param init_data - optional source buffer to copy initialization data. May be nullptr.
        */
        [[nodiscard]] virtual ShadowBuffer allocate(
            const RWR& range, transaction_id_t tid, MemoryRequestType memory_type, const void* init_data) = 0;
        /** \brief Destroy previously allocated by #allocate buffer for specific transaction 
        *
        * \param tid - transaction id;
        * \param buffer - buffer to destroy.
        */
        virtual void destroy(transaction_id_t tid, ShadowBuffer buffer) = 0;

        /** Change behavior what to do on memory block race condition with another transaction. 
        * \return previous isolation policy.
        */
        [[maybe_unused]] virtual ReadIsolation read_isolation(ReadIsolation new_level) = 0;

        /**
        *   Notify Notify implementation that new transaction has been started.
        *
        * \param tid - transaction id.
        */
        virtual void on_new_transaction(transaction_id_t tid) = 0;

        /** 
        * Notify implementation that transaction going to complete successfully. Method called right after
        * all change history records were applied to the storage.
        * 
        * \param tid - transaction id.
        */
        virtual void on_commit(transaction_id_t tid) = 0;

        /**
        * Notify implementation that transaction going to rollback.
        *
        * \param tid - transaction id.
        */
        virtual void on_rollback(transaction_id_t tid) = 0;
    };


    class EventSourcingSegmentManager : public SegmentManager
    {
        friend SegmentManager;
    public:
        using transaction_id_t = typename Transaction::transaction_id_t;

        EventSourcingSegmentManager() = delete;

        virtual ~EventSourcingSegmentManager() = default;

        [[nodiscard]] transaction_ptr_t begin_ro_transaction() 
        {
            const ro_guard_t g(_opened_transactions_lock);
            if (!_opened_transactions.empty())
            {
                throw Exception(OP::vtm::ErrorCodes::er_cannot_start_ro_transaction);
            }
            return transaction_ptr_t(
                new ReadOnlyTransaction(_transaction_uid_gen.fetch_add(1), *this));
        }

        [[nodiscard]] transaction_ptr_t begin_transaction() override
        {
            wr_guard_t g(_opened_transactions_lock);
            if (_ro_tran > 0)
            {//there are already RO-tran in the scope
                return transaction_ptr_t(
                    new ReadOnlyTransaction(_transaction_uid_gen.fetch_add(1), *this));
            }
            auto thread_id = std::this_thread::get_id();
            auto insres = _opened_transactions.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(thread_id),
                std::forward_as_tuple());
            if (insres.second) //just insert
            {
                auto new_tran_id = _transaction_uid_gen.fetch_add(1);
                insres.first->second = transaction_impl_ptr_t(
                    new TransactionImpl(new_tran_id, *this));
                //notify history manager about upcoming loading 
                _change_history_manager->on_new_transaction(new_tran_id);
                return insres.first->second;
            }
            //transaction already exists, just create save-point
            if(auto* impl = dynamic_cast<TransactionImpl*>(insres.first->second.get()))
                return impl->recursive();
            return insres.first->second;
        }

        [[nodiscard]] ReadonlyMemoryChunk readonly_block(
            FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) override
        {
            assert(pos.segment() == (pos + size).segment());//block must not exceed segment size
            transaction_impl_ptr_t current_transaction = 
                get_current_transaction().static_pointer_cast<TransactionImpl>();

            auto result = SegmentManager::readonly_block(pos, size, hint);
            if (_ro_tran)
                return result;
            if( !current_transaction ) //no tran
                return result;
                
            RWR search_range(pos, size);
            //apply all event sourced to `new_buffer`
            auto buffer = _change_history_manager->allocate(
                search_range, current_transaction->transaction_id(), 
                MemoryRequestType::ro, result.at<std::uint8_t>(0));
            return ReadonlyMemoryChunk(std::move(buffer), size, pos);
        }


        [[nodiscard]] MemoryChunk writable_block(
            FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
        {
            assert(pos.segment() == (pos + size).segment());//block must not exceed segment size
            if( _ro_tran > 0)
            {
                throw Exception(ErrorCodes::er_ro_transaction_started);
            }
            transaction_impl_ptr_t current_transaction = 
                get_current_transaction().static_pointer_cast<TransactionImpl>();
            if( !current_transaction ) //write is permitted in transaction scope only
                throw Exception(ErrorCodes::er_transaction_not_started);
                
            RWR search_range(pos, size);
            auto result = SegmentManager::writable_block(pos, size);
            
            
            auto buffer = 
                (hint == WritableBlockHint::new_c) //no need for initial copy
                ? _change_history_manager->allocate(
                    search_range, current_transaction->transaction_id(),
                    MemoryRequestType::wr_no_history, nullptr)
                :  _change_history_manager->allocate(
                    search_range, current_transaction->transaction_id(),
                    MemoryRequestType::wr, result.at<std::uint8_t>(0))
                ;

            auto ghost = buffer.ghost();
            current_transaction->store_log_record(std::move(buffer), result.pos());
            return MemoryChunk(std::move(ghost), size, pos);
        }

        [[nodiscard]] MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro) override
        {
            return this->writable_block(ro.address(), ro.count());
        }

        [[nodiscard]] std::shared_ptr<MemoryChangeHistory> history_manager()
        {
            return _change_history_manager;
        }
    protected:
        EventSourcingSegmentManager(
            const char* file_name, 
            std::fstream file, 
            bip::file_mapping mapping, 
            segment_idx_t segment_size,
            std::shared_ptr<MemoryChangeHistory> _history_manager
            ) 
            : SegmentManager(file_name, std::move(file), std::move(mapping), segment_size)
            , _transaction_uid_gen(121)//just a magic number, actually it can be any number
            , _change_history_manager(std::move(_history_manager))
        {

        }

        virtual transaction_ptr_t get_current_transaction()
        {
            //this method can be replaced if compiler supports 'thread_local' keyword
            const ro_guard_t g(_opened_transactions_lock);
            auto found = _opened_transactions.find(std::this_thread::get_id());
            return found == _opened_transactions.end() ? transaction_ptr_t{} : found->second;
        }
            
    private:

        using wr_guard_t = std::lock_guard<std::shared_mutex>;
        using ro_guard_t = std::shared_lock<std::shared_mutex>;

        /** Must use 64 * 64 instead of 'segment_pos_t' as a size because merge of 2 segment_pos_t may produce 
        overflow of segment_pos_t */
        using RWR = typename MemoryChangeHistory::RWR;
            
        std::shared_ptr<MemoryChangeHistory> _change_history_manager;

        enum class TransactionState : std::uint8_t
        {
            /** Transaction state when commit/rollbacks are possible and allowed */
            active = 0,
            /** Transaction state when only rollbacks are allowed (1) */
            sealed_rollback_only,
            /** Transaction state when no other operations are allowed (2) */
            sealed_noop
        };
            
        /** support evolution of TransactionState when commit/rollback happens */
        static TransactionState& next_state(TransactionState& state) noexcept
        {
            return reinterpret_cast<TransactionState&>(
                ++reinterpret_cast<std::uint8_t&>(state));
        }

        /** Soft implementation of Transaction when caller tries create nested transactions. Mostly
        * delegates all implementation to the parent `TImpl
        */ 
        template <class TImpl>
        struct SavePoint : public Transaction
        {
            using transaction_log_t = decltype(TImpl::_transaction_log); //aka std::vector<history_iterator_t>
            using log_iterator_t = typename transaction_log_t::iterator;

            SavePoint(TImpl* framed_tran)
                : Transaction(framed_tran->transaction_id())
                , _framed_tran(framed_tran)
            {
                _from = _framed_tran->_transaction_log.size();
            }

            void commit() override
            {
                if(_tr_state >= TransactionState::sealed_rollback_only)
                    throw Exception(OP::vtm::ErrorCodes::er_transaction_ghost_state);
                bool has_something_to_erase = false;
                // No real commit for WR
                next_state(_tr_state); //disable changes in this
            }

            void rollback() override
            {
                if(_tr_state >= TransactionState::sealed_noop)
                    throw Exception(OP::vtm::ErrorCodes::er_transaction_ghost_state);
                //reset blocks to unused state
                _framed_tran->utilize_unused_blocks(
                    _framed_tran->_transaction_log.begin() + _from
                );
                next_state(_tr_state);
            }

            log_iterator_t begin() const
            {
                return _framed_tran->_transaction_log.begin() + _from;
            }

            log_iterator_t end() const
            {
                return _framed_tran->_transaction_log.end();
            }

            /** delegate call to the framed transaction */
            void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
            {
                _framed_tran->register_handle(std::move(handler));
            }
                
            TImpl* _framed_tran;
            size_t _from;
            /**After commit/rollback SavePoint must not be used anymore*/
            TransactionState _tr_state = TransactionState::active;
        };
            
        /** Implement Transaction interface */
        struct TransactionImpl : public Transaction
        {
            TransactionImpl(transaction_id_t id, EventSourcingSegmentManager& owner) noexcept
                : Transaction(id)
                , _owner(owner)
            {
            }

            /**Client code may claim nested transaction. Instead of real transaction just provide save-point
            * @return new instance of SavePoint with the same transaction-id as current one
            */
            transaction_ptr_t recursive()
            {
                return transaction_ptr_t{ new SavePoint{this} };
            }

            /**Form transaction log*/
            void store_log_record(ShadowBuffer from, std::uint8_t* dest)
            {
                if(_tr_state != TransactionState::active)
                    throw Exception(vtm::ErrorCodes::er_transaction_ghost_state);
                _transaction_log.emplace_back(std::move(from), dest);
            }
                
            virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
            {
                _end_listener.emplace_back(std::move(handler));
            }

            template <class Iter>    
            void utilize_unused_blocks(Iter begin)
            {
                while(begin != _transaction_log.end())
                {
                    _owner._change_history_manager->destroy(transaction_id(), std::move(begin->first));
                    begin = _transaction_log.erase(begin); //to avoid double scan
                }
            }

            void rollback() override
            {
                if(_tr_state >= TransactionState::sealed_noop)
                    throw Exception(vtm::ErrorCodes::er_transaction_ghost_state);
                //invoke events on transaction end
                for (auto& ev : _end_listener)
                {
                    ev->on_rollback();
                }
                utilize_unused_blocks(_transaction_log.begin());
                _owner._change_history_manager->on_rollback(transaction_id());
                _owner.dispose_transaction(transaction_id());
            }

            void commit() override
            {
                if(_tr_state >= TransactionState::sealed_rollback_only)
                    throw Exception(vtm::ErrorCodes::er_transaction_ghost_state);
                //invoke events on transaction end
                for (auto& ev : _end_listener)
                {
                    ev->on_commit();
                }

                for (auto& [from, to] : _transaction_log)
                {
                    memcpy(to, from.get(), from.size());
                    _owner._change_history_manager->destroy(transaction_id(), std::move(from));
                }
                _owner._change_history_manager->on_commit(transaction_id());
                _owner.dispose_transaction(transaction_id());
            }
                
            /**Allows discover state if transaction still exists (not in ghost state) */
            bool is_active() const
            {
                return _tr_state == TransactionState::active;
            }
                
            EventSourcingSegmentManager& _owner;

            using transaction_end_listener_t = std::vector<std::unique_ptr<BeforeTransactionEnd>>;

            TransactionState _tr_state = TransactionState::active;
            using save_to_t = std::pair<ShadowBuffer, std::uint8_t*>;
            std::deque<save_to_t> _transaction_log;
            transaction_end_listener_t _end_listener;
        };

        struct ReadOnlyTransaction : public Transaction
        {
            
            ReadOnlyTransaction(transaction_id_t id, EventSourcingSegmentManager& owner)
                : Transaction(id)
                , _owner(owner)
            {
                ++_owner._ro_tran;
            }

            ~ReadOnlyTransaction()
            {
            }

            virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
            {
                _end_handlers.emplace_back(std::move(handler));
            }

            /** RO transaction has nothing to rollback, so just notify subscribers about rollback */
            virtual void rollback()
                override
            {
                --_owner._ro_tran;
                for(auto& h: _end_handlers)
                    h->on_rollback();
            }

            /** RO transaction has nothing to commit, so just notify subscribers about rollback */
            virtual void commit() override
            {
                --_owner._ro_tran;
                for (auto& h: _end_handlers)
                    h->on_commit();
            }

        private:
                
            std::vector< std::unique_ptr<BeforeTransactionEnd>> _end_handlers;
            EventSourcingSegmentManager& _owner;
        };

        using transaction_impl_ptr_t = TransactionPtr<TransactionImpl>;

        /*just provide access to parent's writable-block*/
        MemoryChunk raw_writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
        {
            return SegmentManager::writable_block(pos, size, hint);
        }

        void dispose_transaction(transaction_id_t erased_tran_id)
        {
            // iterate association with threads (potentially can be more than one) and erase matches with tran-id
            const wr_guard_t acc_capt(_opened_transactions_lock);
            for(auto i = _opened_transactions.begin(); i != _opened_transactions.end(); )
            {
                if (erased_tran_id == i->second->transaction_id())
                {
                    i = _opened_transactions.erase(i);
                }
                else
                    ++i;
            }
        }

        using opened_transactions_t = std::unordered_map<std::thread::id, transaction_ptr_t>;
        opened_transactions_t _opened_transactions;
        mutable std::shared_mutex _opened_transactions_lock;

        std::atomic<Transaction::transaction_id_t> _transaction_uid_gen;

        std::atomic<size_t> _ro_tran = 0;
        mutable std::mutex _dispose_lock;
    };
        

}//ns::OP::vtm

#endif //_OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_
