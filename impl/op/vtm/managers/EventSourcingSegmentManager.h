#pragma once
#ifndef _OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_
#define _OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_

#include <thread>
#include <shared_mutex>
#include <queue>
#include <variant>

#include <op/common/Exceptions.h>
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

        struct ConcurrentAccessError
        {
            RWR _requested_range;
            transaction_id_t _requesting_transaction;
            RWR _locked_range;
            transaction_id_t _locking_transaction;
        };
        
        using query_region_result_t = std::variant<ShadowBuffer, ConcurrentAccessError>;
            
        MemoryChangeHistory() noexcept = default;
        virtual ~MemoryChangeHistory() = default;

        MemoryChangeHistory(const MemoryChangeHistory&) = delete;
        MemoryChangeHistory(MemoryChangeHistory&&) = delete;
        MemoryChangeHistory& operator = (const MemoryChangeHistory&) = delete;
        MemoryChangeHistory& operator =(MemoryChangeHistory&&) = delete;


        /** \brief Inside specific transaction retrieve memory buffer that must be used for 
        *   read or write operations for logical region. 
        * 
        * Method takes logical address space `range` and provides buffer where read/write operations
        * must be performed for specific transaction.
        * \param range - logical specification of region to retrieve;
        * \param tid - transaction id;
        * \param memory_type - type of memory to return;
        * \param init_data - optional source buffer to copy initialization data. May be nullptr.
        * \return Buffer or error information if requested region can causes ConcurrentLock state.
        *  Depending on the following preconditions return buffer may contain serialized copy of changes made in current transaction so far:
        *   - For `memory_type` flags other than `wr_no_history` buffer contains actual value of current transaction state. That includes
        *     copy of `init_data` (if specified) then previous updates related to region `range` in the current transaction).
        *   - For `memory_type` flag `wr_no_history` result is just random noise. 
        */
        [[nodiscard]] virtual query_region_result_t buffer_of_region(
            const RWR& range, transaction_id_t tid, MemoryRequestType memory_type, const void* init_data) = 0;

        /** \brief Destroy previously allocated by #buffer_of_region buffer for specific transaction 
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
        *   Notify implementation that new transaction has been started.
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

        virtual void iterate_shadows(transaction_id_t tid, bool (*)(const RWR&, const ShadowBuffer&, void*), void *user_args) = 0;
    };

    
    class EventSourcingSegmentManager : public SegmentManager
    {
    public:
        using transaction_ptr_t = typename SegmentManager::transaction_ptr_t;
        using transaction_id_t = typename Transaction::transaction_id_t;

        EventSourcingSegmentManager() = delete;

        EventSourcingSegmentManager(
            std::unique_ptr<SegmentManager> base_manager,
            std::shared_ptr<MemoryChangeHistory> history_manager
            ) 
            : _base_manager(std::move(base_manager))
            , _change_history_manager(std::move(history_manager))
            , _unsubscribers{
                _transaction_event_supplier.on<TransactionEvent::started>(
                std::bind(&MemoryChangeHistory::on_new_transaction, std::ref(*_change_history_manager), std::placeholders::_1)),
                _transaction_event_supplier.on<TransactionEvent::committed>(
                std::bind(&MemoryChangeHistory::on_commit, std::ref(*_change_history_manager), std::placeholders::_1)),
                _transaction_event_supplier.on<TransactionEvent::rolledback>(
                std::bind(&MemoryChangeHistory::on_rollback, std::ref(*_change_history_manager), std::placeholders::_1))
            }
        {
        }

        virtual ~EventSourcingSegmentManager() = default;


        virtual segment_pos_t segment_size() const noexcept override
        {
            return _base_manager->segment_size();
        }

        virtual segment_pos_t header_size() const noexcept override
        {
            auto result = _base_manager->header_size();
            if(_change_history_manager)
                result += 0;
            return OP::utils::align_on(result, SegmentDef::align_c);
        }

        virtual void ensure_segment(segment_idx_t index) override
        {
            return _base_manager->ensure_segment(index);
        }

        virtual segment_idx_t available_segments() override
        {
            return _base_manager->available_segments();
        }

        virtual void subscribe_event_listener(SegmentEventListener* listener) override
        {
            return _base_manager->subscribe_event_listener(listener);
        }


        [[nodiscard]] transaction_ptr_t begin_transaction() override
        {
            bool new_transaction_created = false;
            if (auto local = _opened_transactions.lock(); !local) //uses TLS
            {
                auto result =
                    std::make_shared<TransactionImpl>(_transaction_uid_gen.fetch_add(1), *this);
                _opened_transactions = result;
                _transaction_event_supplier.send<TransactionEvent::started>(result->transaction_id());
                return result;
            }
            else
                return local->recurrent();
        }

        [[nodiscard]] ReadonlyMemoryChunk readonly_block(
            FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) override
        {
            assert(pos.segment() == (pos + size).segment());//block must not exceed segment size

            auto result = _base_manager->readonly_block(pos, size, hint);

            auto current_transaction = _opened_transactions;
            auto local_tx = current_transaction.lock();
            if (!local_tx) //no transaction
                return result;
            if (local_tx->state() != TransactionState::active)
                throw OP::Exception(OP::vtm::ErrorCodes::er_transaction_ghost_state);
                
            RWR search_range(pos, size);
            //apply all event sourced to `new_buffer`
            auto buffer = _change_history_manager->buffer_of_region(
                search_range, local_tx->transaction_id(),
                MemoryRequestType::ro, result.at<std::uint8_t>(0));
            using access_error_t = typename MemoryChangeHistory::ConcurrentAccessError;
            if (std::holds_alternative<access_error_t>(buffer))
            {
                const auto& error = std::get<access_error_t>(buffer);
                throw ConcurrentLockException(
                    pos, local_tx->transaction_id(),
                    FarAddress(error._locked_range.pos()), error._locking_transaction);
            }
            return ReadonlyMemoryChunk(std::move(std::get<ShadowBuffer>(buffer)), size, pos);
        }


        [[nodiscard]] MemoryChunk writable_block(
            FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
        {
            assert(pos.segment() == (pos + size).segment());//block must not exceed segment size

            auto current_transaction = _opened_transactions.lock();
            if( !current_transaction ) //write is permitted in transaction scope only
                throw Exception(ErrorCodes::er_transaction_not_started);
            current_transaction->throw_if_write_disallowed();

            RWR search_range(pos, size);
            auto real_image = _base_manager->writable_block(pos, size);
            
            auto result = 
                (hint == WritableBlockHint::new_c) //no need for initial copy
                ? _change_history_manager->buffer_of_region(
                    search_range, current_transaction->transaction_id(),
                    MemoryRequestType::wr_no_history, nullptr)
                :  _change_history_manager->buffer_of_region(
                    search_range, current_transaction->transaction_id(),
                    MemoryRequestType::wr, real_image.at<std::uint8_t>(0))
                ;
            using access_error_t = typename MemoryChangeHistory::ConcurrentAccessError;
            if (std::holds_alternative<access_error_t>(result))
            {
                const auto& error = std::get<access_error_t>(result);
                throw ConcurrentLockException(
                    pos, current_transaction->transaction_id(),
                    FarAddress(error._locked_range.pos()), error._locking_transaction);
            }
            auto buffer = std::move(std::get<ShadowBuffer>(result));
            current_transaction->store_log_record(buffer.ghost());
            return MemoryChunk(std::move(buffer), size, pos);
        }

        [[nodiscard]] MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro) override
        {
            return this->writable_block(ro.address(), ro.count());
        }

        [[nodiscard]] std::shared_ptr<MemoryChangeHistory> history_manager()
        {
            return _change_history_manager;
        }

        /**
        * Implementation based integrity checking of this instance
        */
        virtual void _check_integrity(bool verbose) override
        {
            _base_manager->_check_integrity(verbose);
        }

        /** Ensure underlying storage is synchronized */
        virtual void flush() override
        {
            _base_manager->flush();
        }

    private:

        using wr_guard_t = std::lock_guard<std::shared_mutex>;
        using ro_guard_t = std::shared_lock<std::shared_mutex>;

        /** Must use 64 * 64 instead of 'segment_pos_t' as a size because merge of 2 segment_pos_t may produce 
        overflow of segment_pos_t */
        using RWR = typename MemoryChangeHistory::RWR;
        
        std::unique_ptr<SegmentManager> _base_manager;
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

        struct TransactionImpl;

        /** Abstract base for transaction implementations */
        struct HistoryAppendTransaction : public Transaction
        {
            using Transaction::Transaction;

            HistoryAppendTransaction(TransactionImpl* framed_tx)
                : Transaction(framed_tx->transaction_id())
                , _framed_tx(framed_tx)
            {
            }

            virtual void store_log_record(ShadowBuffer from) = 0;
            
            /** check if write operation is allowed */
            virtual bool allow_write() const noexcept
            {
                return _tr_state == TransactionState::active;
            }
            
            void throw_if_write_disallowed() const
            {
                if (!allow_write())
                {
                    if(_tr_state == TransactionState::active)
                        throw OP::Exception(OP::vtm::ErrorCodes::er_ro_transaction_started);
                    else
                        throw OP::Exception(OP::vtm::ErrorCodes::er_transaction_ghost_state);
                }
            }

            virtual std::shared_ptr<Transaction> merge_thread() override
            {
                return _framed_tx->merge_thread();
            }

            virtual void unmerge_thread() override
            {
                _framed_tx->unmerge_thread();
            }

            TransactionState state() const
            {
                return _tr_state;
            }

        protected:
            /**After commit/rollback transaction must not be used anymore*/
            TransactionState _tr_state = TransactionState::active;
            TransactionImpl* _framed_tx = nullptr;
        };


        /** Soft implementation of Transaction when caller tries create nested transactions. Mostly
        * delegates all implementation to the parent `TImpl
        */ 
        struct SavePoint : public HistoryAppendTransaction
        {
            SavePoint(TransactionImpl* framed_tx, HistoryAppendTransaction *previous)
                : HistoryAppendTransaction(framed_tx)
                , _previous(previous)
            {
            }

            virtual std::shared_ptr<Transaction> recurrent() override
            {
                return _framed_tx->recurrent();
            }

            virtual void store_log_record(ShadowBuffer from) override
            {
                _transaction_log.emplace_back(std::move(from));
            }

            void commit() override
            {
                if(_tr_state >= TransactionState::sealed_rollback_only)
                    throw Exception(OP::vtm::ErrorCodes::er_transaction_ghost_state);
                // No real commit for WR
                close(); 
            }

            void rollback() override
            {
                if(_tr_state >= TransactionState::sealed_noop)
                    throw Exception(OP::vtm::ErrorCodes::er_transaction_ghost_state);
                //reset captured blocks to unused state
                for(auto& to_garbage: _transaction_log)
                {
                    _framed_tx->_owner._change_history_manager->destroy(
                        transaction_id(), std::move(to_garbage)
                    );
                }
                _transaction_log.clear();
                close();
            }
        private:

            void close()
            {
                _framed_tx->restore_recurrent_chain(_previous);
                next_state(_tr_state); //disable accept changes in this
            }

            using transaction_log_t = std::deque<ShadowBuffer>;
            transaction_log_t _transaction_log;
            HistoryAppendTransaction* _previous;
        };

        /**
        * Implementation that prevents any modifications but allows read requests from multiple threads.
        */
        template <class TImpl>
        struct MergedThreadReadOnlyTransaction : 
            public HistoryAppendTransaction, 
            public std::enable_shared_from_this<HistoryAppendTransaction>
        {
            MergedThreadReadOnlyTransaction(TImpl* framed_tx)
                : HistoryAppendTransaction(framed_tx)//framed_tx->transaction_id())
            {
            }

            virtual std::shared_ptr<Transaction> recurrent() override
            {
                return shared_from_this();
            }

            /** Disable write operation */
            constexpr virtual bool allow_write() const noexcept override
            {
                return false;
            }

            virtual void store_log_record(ShadowBuffer from) override
            {
                // code is unreachable, but for case ...
                throw_if_write_disallowed();
            }

            void commit() override
            {
                //do nothing
            }

            void rollback() override
            {
                //do nothing
            }
        };

        /** Implement Transaction interface */
        struct TransactionImpl : 
            public HistoryAppendTransaction, 
            std::enable_shared_from_this<HistoryAppendTransaction>
        {
            TransactionImpl(transaction_id_t id, EventSourcingSegmentManager& owner) noexcept
                : HistoryAppendTransaction(id)
                , _owner(owner)
            {
            }

            /** Client code may claim nested transaction. Instead of real transaction just provide save-point
            * @return new instance of SavePoint with the same transaction-id as current one
            */
            virtual std::shared_ptr<Transaction> recurrent() override
            {
                auto result = std::make_shared<SavePoint>(this, _active_save_point);
                _active_save_point = result.get();
                return result;
            }

            void restore_recurrent_chain(HistoryAppendTransaction* previous)
            {
                std::swap(_active_save_point, previous);
            }

            /**Form transaction log*/
            virtual void store_log_record(ShadowBuffer buffer) override
            {
                throw_if_write_disallowed(); //don't allow changes in ghost state
                if (_active_save_point)
                    _active_save_point->store_log_record(std::move(buffer));
                // implementation doesn't need explictly store record, it is managed by MemoryChangeHistory
            }

            void rollback() override
            {
                throw_if_write_disallowed();
                if (_thread_merge_count)
                    throw OP::Exception(vtm::ErrorCodes::er_cannot_close_transaction_while_merged_thread);
                //invoke events on transaction end
                _owner._transaction_event_supplier.send<TransactionEvent::before_rollback>(transaction_id());
                next_state(_tr_state); //disable accept changes in this
                _owner._transaction_event_supplier.send<TransactionEvent::rolledback>(transaction_id());
                _owner.dispose_transaction(*this);
            }

            void commit() override
            {
                throw_if_write_disallowed();
                if (_thread_merge_count)
                    throw OP::Exception(vtm::ErrorCodes::er_cannot_close_transaction_while_merged_thread);

                //invoke events on transaction end
                _owner._transaction_event_supplier.send<TransactionEvent::before_commit>(transaction_id());
                _owner._change_history_manager->iterate_shadows(transaction_id(),
                    +[](const RWR& region, const ShadowBuffer& source, void*user_def)->bool {
                        EventSourcingSegmentManager& owner = *reinterpret_cast<EventSourcingSegmentManager*>(user_def);
                        auto wr_access =
                            owner.raw_writable_block(FarAddress(region.pos()), region.count(), WritableBlockHint::new_c);
                        wr_access.byte_copy(source.get(), source.size());
                        return true; //continue iteration
                    }, &_owner);
                next_state(_tr_state); //disable accept changes in this
                _owner._transaction_event_supplier.send<TransactionEvent::committed>(transaction_id());
                _owner.dispose_transaction(*this);
            }
                
            virtual std::shared_ptr<Transaction> merge_thread() override
            {
                auto current = _owner._opened_transactions.lock();
                if (current)
                    throw OP::Exception(
                        vtm::ErrorCodes::er_thread_owns_transaction, 
                        std::this_thread::get_id(), current->transaction_id()
                );
                current = std::make_shared<MergedThreadReadOnlyTransaction<TransactionImpl>>(this);
                assert(current->transaction_id() == this->transaction_id());
                _owner._opened_transactions = current;
                ++_thread_merge_count;
                return current;
            }

            virtual void unmerge_thread() override
            {
                auto current = _owner._opened_transactions.lock();
                if (!current || !_thread_merge_count)
                    throw OP::Exception(
                        vtm::ErrorCodes::er_transaction_not_started,
                        std::this_thread::get_id()
                    );
                --_thread_merge_count;
                assert(current->transaction_id() == this->transaction_id());
                _owner._opened_transactions.reset();
            }

            EventSourcingSegmentManager& owner()
            {
                return _owner;
            }

            EventSourcingSegmentManager& _owner;
            TransactionState _tr_state = TransactionState::active;
            std::atomic<unsigned> _thread_merge_count = 0;
            HistoryAppendTransaction* _active_save_point = nullptr; //TLS must grant thread safety for update this field
        };

        /*just provide access to parent's writable-block*/
        MemoryChunk raw_writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
        {
            return _base_manager->writable_block(pos, size, hint);
        }

        void dispose_transaction(Transaction& tx)
        {
            if (_opened_transactions.expired())
                throw OP::Exception(OP::vtm::ErrorCodes::er_transaction_not_started, tx.transaction_id());
            _opened_transactions.reset();
        }

        static inline thread_local std::weak_ptr<HistoryAppendTransaction> _opened_transactions;
        std::atomic<transaction_id_t> _transaction_uid_gen = 121;//just a magic number, actually it can be any reasonable small
        typename TransactionEvent::event_supplier_t _transaction_event_supplier;
        std::array<typename TransactionEvent::event_supplier_t::unsubscriber_t, 3> _unsubscribers;
    };
        

}//ns::OP::vtm

#endif //_OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_
