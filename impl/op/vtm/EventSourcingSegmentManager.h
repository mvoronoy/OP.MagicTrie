#pragma once
#ifndef _OP_TRIE_EVENTSOURCINGSEGMENTMANAGER__H_
#define _OP_TRIE_EVENTSOURCINGSEGMENTMANAGER__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/Transactional.h>
#include <op/common/Exceptions.h>
#include <op/common/SpanContainer.h>
#include <op/common/Unsigned.h>
#include <thread>
#include <shared_mutex>
#include <queue>

namespace OP
{
    using namespace vtm;
    namespace trie
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

        class EventSourcingSegmentManager : public SegmentManager
        {
            friend SegmentManager;
        public:

            EventSourcingSegmentManager() = delete;

            ~EventSourcingSegmentManager()
            {
                _done_captured_worker.store(true);
                if (1 == 1) 
                {
                    std::unique_lock acc(_dispose_lock);
                    _cv_disposer.notify_one();
                }
                _captured_worker.join();
            }

            /** Change behavior of read, what to do on detection conflict with write blocks of another transaction.
            \return previous isolation policy
            */
            ReadIsolation read_isolation(ReadIsolation new_level)
            {
                return _read_isolation.exchange(new_level);
            }
            
            [[nodiscard]] transaction_ptr_t begin_ro_transaction() 
            {
                const ro_guard_t g(_opened_transactions_lock);
                if (!_opened_transactions.empty())
                {
                    throw Exception(er_cannot_start_ro_transaction);
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
                    insres.first->second = transaction_impl_ptr_t(
                        new TransactionImpl(_transaction_uid_gen.fetch_add(1), *this));
                    return insres.first->second;
                }
                //transaction already exists, just create save-point
                if(auto* impl = dynamic_cast<TransactionImpl*>(insres.first->second.get()))
                    return impl->recursive();
                return insres.first->second;
            }

            ReadonlyMemoryChunk readonly_block(FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) override
            {
                transaction_impl_ptr_t current_transaction = 
                    std::static_pointer_cast<TransactionImpl>(get_current_transaction());

                auto result = SegmentManager::readonly_block(pos, size, hint);
                if (_ro_tran)
                    return result;
                RWR search_range(pos, size);
                history_ordered_log_t transaction_log;

                // find all previously used block that have any intersection with query
                // to check if readonly block is allowed
                const std::unique_lock acc_captured(_captured_lock);
                auto blocks_in_touch = _captured.intersect_with(search_range);
                if(blocks_in_touch == _captured.end() //no prev blocks at all
                    && (hint & ReadonlyBlockHint::ro_keep_lock) != ReadonlyBlockHint::ro_keep_lock //block has no retain policy
                    )
                {
                    return result;
                }
                auto current_isolation = _read_isolation.load();
                for(; blocks_in_touch != _captured.end(); ++blocks_in_touch)
                {
                    auto& block_profile = blocks_in_touch->second;
                    if(!block_profile._is_ro) 
                    {// writable block
                        //combine write & read allowed only in the same tran
                        //check tran is the same
                        if (current_transaction
                            && block_profile._used_in_transaction == current_transaction->transaction_id())
                        {// form event log ordered by history 
                            transaction_log.emplace(blocks_in_touch);
                        }
                        else
                        {//cannot capture because other WR- tran exists over this block
                            switch( current_isolation )
                            {
                            default:
                            case ReadIsolation::ReadCommitted:
                                //do nothing, Ignore this WR block, proceed with origin RO memory. 
                                break;
                            case ReadIsolation::Prevent:
                                //allow caller to retry later
                                throw ConcurentLockException(
                                    "cannot capture RO while it is used as WR in other transaction, or no trasnsaction used");
                            case ReadIsolation::ReadUncommitted:
                                { //DIRTY-READ logic
                                    transaction_log.emplace(blocks_in_touch);
                                    break;
                                }
                            }
                        }
                        
                    }
                }
                //apply all event sourced to `new_buffer`
                shadow_buffer_t new_buffer = shadow_buffer_t{new std::uint8_t[result.count()], [](std::uint8_t *p) {delete[]p; } };
                memcpy(new_buffer.get(), result.at<std::uint8_t>(0), result.count()); //first copy origin from disk
                for (; !transaction_log.empty(); transaction_log.pop())
                {// iterate transaction log from oldest to newest and apply changes on result memory block
                    auto& ev_src = transaction_log.top();
                    /** there are 4 relative positions of intersection existing block and new one:
                    * 1. existing on left:\code
                    * [--existing--]
                    *       [--new--]
                    * \endcoed
                    * 2. existing on right:\code
                    *      [--existing--]
                    * [--new--]
                    * \endcoed
                    * 3. existing adsorbs new:\code
                    * [------existing------]
                    *       [--new--]
                    * \endcoed
                    * 4. new adsorbs existing:\code
                    *   [-existing-]
                    * [------new------]
                    * \endcoed
                    */
                    auto joined_zone = OP::zones::join_zones(search_range, ev_src->first);
                    auto offset_in_src = OP::utils::uint_diff_int(joined_zone.pos(), ev_src->first.pos());
                    assert(offset_in_src >= 0);
                    auto offset_in_dest = OP::utils::uint_diff_int(joined_zone.pos(), pos.address);
                    assert(offset_in_dest >= 0);
                    memcpy(new_buffer.get() + offset_in_dest, 
                        ev_src->second._shadow.get() + offset_in_src, 
                        joined_zone.count()); 
                }
                // at this point it is possible that no-tran at the outer scope, but we sure no intersection with other WR blocks
                if(!current_transaction)
                    return result; //just default impl
                // at last add history record
                // Note: if there is exactly matched block it will be also appended to multimap
                auto iter = _captured.emplace(search_range,
                    BlockProfile{true/*RO*/, 
                    current_transaction->next_history_id(), 
                    current_transaction->transaction_id(),
                    new_buffer,
                    static_cast<std::uint8_t>(hint)
                    });
                current_transaction->store(iter);
                return ReadonlyMemoryChunk(0,
                                std::move(new_buffer),
                                size,
                                std::move(pos),
                                std::move(SegmentManager::get_segment(pos.segment))
                            );
            }

            MemoryChunk writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
            {
                if( _ro_tran > 0)
                {
                    throw Exception(er_ro_transaction_started);
                }
                transaction_impl_ptr_t current_transaction = 
                    std::static_pointer_cast<TransactionImpl>(get_current_transaction());
                if( !current_transaction ) //write is permitted in transaction scope only
                    throw Exception(er_transaction_not_started);
                
                auto result = SegmentManager::readonly_block(
                    pos, size, ReadonlyBlockHint::ro_no_hint_c); //it is not a mistake to use readonly(!)
                shadow_buffer_t new_buffer = shadow_buffer_t{
                    new std::uint8_t[result.count()], 
                    [](std::uint8_t *p) {delete[]p; } };
                //first copy origin from disk
                memcpy(
                    new_buffer.get(), result.at<std::uint8_t>(0), result.count()); 
                RWR search_range(pos, size);
                //temp ordered sequence that is populated and used as a transaction log
                history_ordered_log_t transaction_log;

                // find all previously used block that have any intersection with query
                // to check if readonly block is allowed
                const std::unique_lock acc_captured(_captured_lock);
                auto blocks_in_touch = _captured.intersect_with(search_range);
                for(; blocks_in_touch != _captured.end(); ++blocks_in_touch)
                {
                    auto& block_profile = blocks_in_touch->second;
                    // for WR any block from another transaction is a point to reject
                    if (block_profile._used_in_transaction != current_transaction->transaction_id())
                    {//cannot capture because other transaction exists
                        throw ConcurentLockException("cannot capture WR-block while it is used in another transaction");
                    }
                    // form ordered by history event log
                    if(!block_profile._is_ro) //only WR blocks contains changes
                        transaction_log.emplace(blocks_in_touch);
                }
                //apply all event sourced to `new_buffer`
                for (;!transaction_log.empty(); transaction_log.pop())
                {// iterate transaction log from oldest to newest and apply changes on result memory block
                    auto& ev_src = transaction_log.top();
                    /** there are 4 relative positions of intersection existing block and new one:
                    * 1. existing on left:\code
                    * [--existing--]
                    *       [--new--]
                    * \endcoed
                    * 2. existing on right:\code
                    *      [--existing--]
                    * [--new--]
                    * \endcoed
                    * 3. existing adsorbs new:\code
                    * [------existing------]
                    *       [--new--]
                    * \endcoed
                    * 4. new adsorbs existing:\code
                    *   [-existing-]
                    * [------new------]
                    * \endcoed
                    */
                    auto joined_zone = OP::zones::join_zones(search_range, ev_src->first);
                    auto offset_in_src = OP::utils::uint_diff_int(joined_zone.pos(), ev_src->first.pos());
                    assert(offset_in_src >= 0);
                    auto offset_in_dest = OP::utils::uint_diff_int(joined_zone.pos(), pos.address);
                    assert(offset_in_dest >= 0);
                    memcpy(new_buffer.get() + offset_in_dest, 
                        ev_src->second._shadow.get() + offset_in_src, 
                        joined_zone.count()); 
                }
                // at last add history record
                // Note: if there is exactly matched block it will be also appended to multimap
                auto iter = _captured.emplace(std::make_pair(search_range,
                    BlockProfile{false/*WR*/, 
                    current_transaction->next_history_id(), 
                    current_transaction->transaction_id(),
                    new_buffer,
                    static_cast<std::uint8_t>(hint) }));
                current_transaction->store(iter);
                return MemoryChunk(
                                std::move(new_buffer),
                                size,
                                std::move(pos),
                                std::move(SegmentManager::get_segment(pos.segment))
                            );
            }

            MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro)  override
            {
                return this->writable_block(ro.address(), ro.count());
            }

        protected:
            EventSourcingSegmentManager(const char * file_name, bool create_new, bool readonly) 
                : SegmentManager(file_name, create_new, readonly)
                , _transaction_uid_gen(121)//just a magic number, actually it can be any number
                , _captured_worker(&EventSourcingSegmentManager::release_captured_worker, this)
            {

            }
            virtual transaction_ptr_t get_current_transaction()
            {
                //this method can be replaced if compiler supports 'thread_local' keyword
                const ro_guard_t g(_opened_transactions_lock);
                auto found = _opened_transactions.find(std::this_thread::get_id());
                return found == _opened_transactions.end() ? std::shared_ptr<Transaction>() : found->second;
            }
            
        private:
            using shared_lock_t = std::shared_mutex;
            using wr_guard_t = std::lock_guard<shared_lock_t>;
            using ro_guard_t = std::shared_lock<shared_lock_t>;

            /** Must use 64 * 64 instead of 'segment_pos_t' as a size because merge of 2 segment_pos_t may produce 
            overflow of segment_pos_t */
            using RWR = OP::Range<far_pos_t, far_pos_t>;
            using shadow_buffer_t = std::shared_ptr<std::uint8_t>;

            /**Properties of captured block in particular transaction*/
            struct BlockProfile
            {
                bool _is_ro;
                std::uint64_t _order;
                Transaction::transaction_id_t _used_in_transaction;
                shadow_buffer_t _shadow;
                std::uint8_t _flag;  //either ReadonlyBlockHint or WritableBlockHint
            };
            using block_in_use_t = OP::zones::SpanMap<RWR, BlockProfile>;

            /** impl std::greater to order priority_queue in reverse order
             from `block_in_use_t` by second._order */
            struct comp_by_order_t 
            {
                bool operator ()(const typename block_in_use_t::iterator& left, const typename block_in_use_t::iterator& right) const
                { 
                    return left->second._order > right->second._order; 
                }
            };

            /** temp history-ordered sequence that is populated and used as a transaction log */
            using history_ordered_log_t = std::priority_queue<
                typename block_in_use_t::iterator, 
                std::vector<typename block_in_use_t::iterator>, 
                comp_by_order_t>;
            /** history naturally ordered sequence of events, since std::map::iterator is stable can store it.*/
            using linear_log_t = std::vector<block_in_use_t::iterator>;
            
            using transaction_state_t = std::uint8_t;
            /**Transaction state when only rollbacks are allowed*/
            static constexpr transaction_state_t ts_active_c = 0;
            /**Transaction state when only rollbacks are allowed*/
            static constexpr transaction_state_t ts_sealed_rollback_only_c = ts_active_c + 1;
            /**Transaction state when no other operations are allowed*/
            static constexpr transaction_state_t ts_sealed_noop_c = ts_sealed_rollback_only_c+1;

            struct TransactionImpl;
            struct SavePoint : public Transaction, std::enable_shared_from_this<SavePoint>
            {
                using iterator = typename linear_log_t::iterator;

                SavePoint(TransactionImpl* framed_tran)
                    : Transaction(framed_tran->transaction_id())
                    , _framed_tran(framed_tran)
                {
                    _from = _framed_tran->_transaction_log.size();
                }
                void commit() override
                {
                    if(_tr_state >= ts_sealed_rollback_only_c)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    // No real commit for WR, since save point have to wait untill entire transaction complete
                    // but we can remove all read locks unless ReadonlyBlockHint::ro_keep_lock used
                    _framed_tran->_owner.apply_transaction_log(
                        begin(), _framed_tran->_transaction_log.end(),
                        [](const auto& map_iter){
                            return map_iter->second._is_ro //block for read
                             && !(map_iter->second._flag & (std::uint8_t)ReadonlyBlockHint::ro_keep_lock); //keep lock in tran
                        });
                    ++_tr_state;
                }
                void rollback() override
                {
                    if(_tr_state >= ts_sealed_noop_c)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    _framed_tran->_owner.apply_transaction_log(begin(), _framed_tran->_transaction_log.end(),
                        [](const auto& ){return true;/* on rollback remove all used blocks*/});
                    ++_tr_state;
                }
                iterator begin() const
                {
                    return _framed_tran->_transaction_log.begin() + _from;
                }
                
                void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
                {
                    _framed_tran->register_handle(std::move(handler));
                }
                
                TransactionImpl* _framed_tran;
                size_t _from;
                /**After commit/rollback SavePoint must not be used anymore*/
                transaction_state_t _tr_state = ts_active_c;
            };
            
            /**Implement Transaction interface*/
            struct TransactionImpl : public Transaction, std::enable_shared_from_this<TransactionImpl>
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
                    return std::make_shared<SavePoint>(this);
                }

                /**Form transaction log*/
                void store(typename block_in_use_t::iterator& pos)
                {
                    if(_tr_state)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    _transaction_log.emplace_back(pos);
                }
                
                virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
                {
                    _end_listener.emplace_back(std::move(handler));
                }
                
                void rollback() override
                {
                    if(_tr_state >= ts_sealed_noop_c)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    //invoke events on transaction end
                    for (auto& ev : _end_listener)
                    {
                        ev->on_rollback();
                    }
                    _garbage_collect.reserve(_garbage_collect.size() + _transaction_log.size());

                    _owner.apply_transaction_log(_transaction_log.begin(), _transaction_log.end(), 
                            [this](const auto& map_iter){
                                //let's postpone real memory dealloc for background worker
                                _garbage_collect.emplace_back(map_iter->second._shadow);
                                return true; /*unconditionally erase all blocks*/
                    });
                    
                    //notify background worker to dispose this tran
                    _owner.dispose_transaction(shared_from_this());
                }

                void commit() override
                {
                    if(_tr_state >= ts_sealed_rollback_only_c)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    //invoke events on transaction end
                    for (auto& ev : _end_listener)
                    {
                        ev->on_commit();
                    }
                    _garbage_collect.reserve(_garbage_collect.size() + _transaction_log.size());
                    //iterate all save-points in direct order and apply commit
                    _owner.apply_transaction_log(_transaction_log.begin(), _transaction_log.end(), 
                            [&, this](const auto&map_iter){
                                if( !map_iter->second._is_ro )
                                {//copy shadow buffer to disk
                                    //block must not exceed segment size
                                    assert(map_iter->first.count() < std::numeric_limits<segment_pos_t>::max());
                                    segment_pos_t byte_size = static_cast<segment_pos_t>(map_iter->first.count());
                                    auto mb = _owner.raw_writable_block(
                                            FarAddress(map_iter->first.pos()), 
                                            byte_size,
                                            (WritableBlockHint)map_iter->second._flag );
                                    mb.byte_copy(map_iter->second._shadow.get(), byte_size);
                                }
                                _garbage_collect.emplace_back(map_iter->second._shadow);
                                return true; //ask owner remove all references
                            });
                    
                    //notify background worker to dispose this tran
                    _owner.dispose_transaction(shared_from_this());
                }
                
                /**Allows discover state if transaction still exists (not in ghost state) */
                bool is_active() const
                {
                    return _tr_state == ts_active_c;
                }

                std::uint64_t next_history_id()
                {
                    return _gen_block_history.fetch_add(1, std::memory_order_relaxed);
                }

                EventSourcingSegmentManager& _owner;
                std::atomic_uint64_t _gen_block_history{0};


                using address_lookup_t = std::unordered_map<const std::uint8_t*, far_pos_t>;
                using transaction_end_listener_t = std::vector<std::unique_ptr<BeforeTransactionEnd>>;
                using shadow_buffer_array_t = std::vector<shadow_buffer_t>;

                transaction_state_t _tr_state = ts_active_c;
                linear_log_t _transaction_log;
                size_t _max_save_point_size = 0; //just for heuristic
                transaction_end_listener_t _end_listener;
                shadow_buffer_array_t _garbage_collect;
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
                virtual void commit()
                    override
                {
                    --_owner._ro_tran;
                    for (auto& h: _end_handlers)
                        h->on_commit();
                }

            private:
                
                std::vector< std::unique_ptr<BeforeTransactionEnd>> _end_handlers;
                EventSourcingSegmentManager& _owner;
            };

            using transaction_impl_ptr_t = std::shared_ptr<TransactionImpl>;

            /*just provide access to parent's writable-block*/
            MemoryChunk raw_writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                return SegmentManager::writable_block(pos, size, hint);
            }
            void dispose_transaction(transaction_impl_ptr_t tran)
            {
                {
                    std::unique_lock acc (_dispose_lock);
                    _ready_to_dispose.emplace_back(tran);
                    _cv_disposer.notify_one();
                }
                const wr_guard_t acc_capt(_opened_transactions_lock);
                
                for(auto i = _opened_transactions.begin(); i != _opened_transactions.end(); )
                {
                    if (tran->transaction_id() == i->second->transaction_id())
                    {
                        i = _opened_transactions.erase(i);
                    }
                    else
                        ++i;
                }
            }

            /**
            * \tparam F - action in form `bool (const& typename block_in_use_t::iterator)` to apply to iterator from 
            *               _captured. Method invoked under mutex, so it already thread safe.
            *               Action must return true - to remove entry from SavePoint and _captured, false to leave as is
            * 
            */
            template <class F>
            void apply_transaction_log(typename linear_log_t::iterator from, typename linear_log_t::iterator to, F action )
            {
                const std::unique_lock acc_captured(_captured_lock);
                const auto captured_end = _captured.end();
                for(; from != to; ++from)
                {
                    auto& map_iter = (*from); //resolve map's iterator
                    //to compare use property of map's iterator stability
                    if (captured_end != map_iter && action(map_iter))
                    { // may be safely wiped
                        _captured.erase(map_iter);
                        // Cannot erase iterator because deque doesn't grant stable position for 
                        // stored references, instead place _captured.end()
                        *from = _captured.end();
                    }
                }
            }

            /** Worker method to wipe from _captured not used references*/
            void release_captured_worker()
            {
                while(!_done_captured_worker.load())
                {
                    std::unique_lock<std::mutex> acc_job(_dispose_lock);
                    if(_ready_to_dispose.empty())
                    {
                        //release lock and wait queue is not empty
                        _cv_disposer.wait(acc_job, [this](){
                                return !_ready_to_dispose.empty() || _done_captured_worker.load();
                            });
                        if (_ready_to_dispose.empty())
                        {
                            acc_job.unlock();
                            continue;  //still empty just retry
                        }
                    }
                    auto wipe_tran = _ready_to_dispose.back();
                    _ready_to_dispose.pop_back();
                    //can unlock since no more operations with queue 
                    acc_job.unlock();                                                                          
                    wipe_tran->_garbage_collect.clear();
                    //dispose all save-points
                    wipe_tran->_transaction_log.clear();//nobody uses this tran anymore - so no locks
                }
            }

            using opened_transactions_t = std::unordered_map<std::thread::id, transaction_ptr_t> ;

            block_in_use_t _captured;
            mutable std::mutex _captured_lock;
            mutable shared_lock_t _opened_transactions_lock;
            opened_transactions_t _opened_transactions;
            std::atomic<Transaction::transaction_id_t> _transaction_uid_gen;

            /**Allows notify background worker to check for some job*/
            std::condition_variable _cv_disposer;
            std::atomic<bool> _done_captured_worker = false;
            std::atomic<size_t> _ro_tran = 0;
            mutable std::mutex _dispose_lock;
            std::deque<transaction_impl_ptr_t> _ready_to_dispose;
            /**background worker to release redundant records from _captured*/
            std::thread _captured_worker;
            std::atomic<ReadIsolation> _read_isolation = ReadIsolation::ReadCommitted;
        };
        

    } //ns::trie
}//ns::OP

#endif //_OP_TRIE_EVENTSOURCINGSEGMENTMANAGER__H_