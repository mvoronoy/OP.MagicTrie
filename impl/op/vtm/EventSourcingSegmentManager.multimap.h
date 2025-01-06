#pragma once
#ifndef _OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_
#define _OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_

#include <thread>
#include <shared_mutex>
#include <map>
#include <queue>

#include <op/common/Exceptions.h>
#include <op/common/SpanContainer.h>
#include <op/common/Unsigned.h>

#include <op/flur/flur.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/ShadowBufferCache.h>

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
                _stop_garbage_collector.store(true);
                if (1 == 1) 
                {
                    std::unique_lock acc(_dispose_lock);
                    _cv_disposer.notify_one();
                }
                _garbage_collector.join();
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

            [[nodiscard]] ReadonlyMemoryChunk readonly_block(
                FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) override
            {
                assert(pos.segment() == (pos + size).segment());//block must not exceed segment size
                transaction_impl_ptr_t current_transaction = 
                    get_current_transaction().static_pointer_cast<TransactionImpl>();

                auto result = SegmentManager::readonly_block(pos, size, hint);
                if (_ro_tran)
                    return result;
                RWR search_range(pos, size);
                if( !current_transaction )
                    return readonly_block_no_retain(search_range, std::move(result));
                
                //apply all event sourced to `new_buffer`
                shadow_buffer_t new_buffer = _shadow_buffer_cache.get(result.count());
                memcpy(new_buffer.get(), result.at<std::uint8_t>(0), result.count()); //first copy origin from disk

                if((hint & ReadonlyBlockHint::ro_keep_lock) == ReadonlyBlockHint::ro_keep_lock)
                { //need to retain RO block

                    std::unique_lock guard_history_wr(_global_history_acc);
                    auto new_block_iter = add_global_history(
                        search_range,
                        BlockType::ro,
                        current_transaction->transaction_id(),
                        std::move(new_buffer),
                        static_cast<std::uint8_t>(hint)
                    );
                    
                    //guard that BlockType::init will be altered
                    RAIIBlockGuard block_leaking_guard(*new_block_iter);

                    current_transaction->store(new_block_iter);
                    guard_history_wr.unlock(); // no more changes of global history list are needed
                    auto buffer_weak = new_block_iter->_shadow.ghost();
                    ReadonlyMemoryChunk result(std::move(buffer_weak), size, pos);
                    std::shared_lock guard_history_ro(_global_history_acc); //shared ro access are enough to iterate
                    poulate_ro_block(search_range, new_block_iter, result, current_transaction->transaction_id());
                    block_leaking_guard.exchange(BlockType::ro); //ensure block will not be erased until tran end
                    return result;
                }
                else // no retains, just populate buffer with intersected blocks
                {
                    std::shared_lock guard_history_ro(_global_history_acc); //shared ro access are enough to iterate
                    ReadonlyMemoryChunk result(std::move(new_buffer), size, pos);
                    poulate_ro_block(search_range, _global_history.end(), result, current_transaction->transaction_id());
                    return result;
                }
            }


            [[nodiscard]] MemoryChunk writable_block(
                FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)  override
            {
                assert(pos.segment() == (pos + size).segment());//block must not exceed segment size
                if( _ro_tran > 0)
                {
                    throw Exception(er_ro_transaction_started);
                }
                transaction_impl_ptr_t current_transaction = 
                    get_current_transaction().static_pointer_cast<TransactionImpl>();
                if( !current_transaction ) //write is permitted in transaction scope only
                    throw Exception(er_transaction_not_started);
                
                auto result = SegmentManager::readonly_block(
                    pos, size, ReadonlyBlockHint::ro_no_hint_c); //it is not a mistake to use readonly(!)
                shadow_buffer_t new_buffer = _shadow_buffer_cache.get(result.count()); //try reuse existing buffer
                if( hint != WritableBlockHint::new_c)
                {
                    //first copy origin from disk
                    memcpy(
                        new_buffer.get(), result.at<std::uint8_t>(0), result.count()); 
                }
                RWR search_range(pos, size);

                // Add to global history WR block
                std::unique_lock guard_history_wr(_global_history_acc);
                auto new_block_iter = add_global_history(
                        search_range,
                        BlockType::wr,
                        current_transaction->transaction_id(),
                        std::move(new_buffer),
                        static_cast<std::uint8_t>(hint)
                    );
                //guard that BlockType::init will be altered to garbage
                RAIIBlockGuard block_leaking_guard(*new_block_iter);

                current_transaction->store(new_block_iter);
                guard_history_wr.unlock(); // no more changes of global history list are needed

                std::shared_lock guard_history_ro(_global_history_acc); //shared ro access are enough to iterate
                narrow_history_log(search_range, new_block_iter, [&](auto& all_iter){
                // find all previously used block that have any intersection with query
                // to check if wr- block is allowed

                    const auto& block = *all_iter;
                    if (block._type == BlockType::garbage) //only WR blocks contains changes
                        return true;

                    auto joined_zone = OP::zones::join_zones(search_range, block._range);
                    if (joined_zone.empty())
                        return true;
                    // existence of ro/wr block type from another transaction is a point to reject
                    if (block._used_in_transaction != current_transaction->transaction_id())
                    {//cannot capture because other transaction exists and retain
                        throw ConcurrentLockException("cannot capture WR-block while it is used in another transaction");
                    }
                    //same-tran ro blocks can have retain policy, just ignore
                    if(block._type != BlockType::wr)
                        return true;
                    //apply event sourced to `new_buffer`
                    /** there are 4 relative positions of intersection existing block and new one:
                    * 1. existing on left:\code
                    * [--existing--]
                    *       [--new--]
                    * \endcode
                    * 2. existing on right:\code
                    *      [--existing--]
                    * [--new--]
                    * \endcode
                    * 3. existing adsorbs new:\code
                    * [------existing------]
                    *       [--new--]
                    * \endcode
                    * 4. new adsorbs existing:\code
                    *   [-existing-]
                    * [------new------]
                    * \endcode
                    */
                    if (hint != WritableBlockHint::new_c)
                    {
                        auto offset_in_src = OP::utils::uint_diff_int(joined_zone.pos(), block._range.pos());
                        assert(offset_in_src >= 0);
                        auto offset_in_dest = OP::utils::uint_diff_int(joined_zone.pos(), pos.address);
                        assert(offset_in_dest >= 0);
                        memcpy(new_block_iter->_shadow.get() + offset_in_dest,
                               block._shadow.get() + offset_in_src,
                               joined_zone.count());
                    }
                    return true;
                });
                block_leaking_guard.exchange(BlockType::wr); //ensure block will not be erased until tran end

                shadow_buffer_t buffer_weak = new_block_iter->_shadow.ghost();
                return MemoryChunk(std::move(buffer_weak), size, pos);
            }

            [[nodiscard]] MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro) override
            {
                return this->writable_block(ro.address(), ro.count());
            }

        protected:
            EventSourcingSegmentManager(const char * file_name, bool create_new, bool readonly) 
                : SegmentManager(file_name, create_new, readonly)
                , _transaction_uid_gen(121)//just a magic number, actually it can be any number
                , _garbage_collector(&EventSourcingSegmentManager::collect_garbage, this)
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
            using RWR = OP::Range<far_pos_t, far_pos_t>;
            using shadow_buffer_t = ShadowBuffer;

            enum class BlockType : std::uint_fast8_t
            {
                /** block allocated but not usable yet*/
                init = 0,
                ro,
                wr,
                garbage
            };
            /**Properties of captured block in particular transaction*/
            struct BlockProfile
            {
                BlockProfile(
                    RWR range, 
                    BlockType type,
                    Transaction::transaction_id_t used_in_transaction,
                    std::uint64_t epoch,
                    shadow_buffer_t shadow,
                    std::uint8_t flag
                )
                    : _range(range)
                    , _type(type)
                    , _used_in_transaction(used_in_transaction)
                    , _epoch(epoch)
                    , _shadow(std::move(shadow))
                    , _flag(flag)
                {
                }

                /** position/size of block */
                const RWR _range;
                /** type of retain readonly/writable (and init state) */ 
                std::atomic<BlockType> _type = BlockType::init;
                /** what transaction retains the block */
                const Transaction::transaction_id_t _used_in_transaction;
                const std::uint64_t _epoch;
                /** shadow memory of changes */
                shadow_buffer_t _shadow;
                /**additional to _type optional flags either ReadonlyBlockHint or WritableBlockHint */
                const std::uint8_t _flag;  
            };

            /** Allows guard just created BlockProfile from memory leaks. If explicit #exchange is not
            * called block automatically marked to #BlockType::garbage at guard scope exit.
            */
            struct RAIIBlockGuard
            {
                explicit RAIIBlockGuard(BlockProfile& block)
                    : _block(block)
                    , _origin_type(_block._type.load())
                {
                }

                ~RAIIBlockGuard()
                {
                    if (_origin_type != BlockType::garbage)
                    {
                        _block._type = BlockType::garbage;
                    }
                }

                /** @return previous block type value. */
                [[maybe_unused]] BlockType exchange(BlockType new_type)
                {
                    auto result = _block._type.exchange(new_type);
                    _origin_type = BlockType::garbage; //prevent destroying at destructor
                    return result;
                }

                BlockProfile& _block;
                BlockType _origin_type;
            };

            using history_t = std::list<BlockProfile>;
            using history_iterator_t = typename history_t::iterator;

            history_t _global_history;
            std::multimap<far_pos_t, history_iterator_t> _index_ranges;
            std::atomic<std::uint64_t> _epoch{0};
            mutable std::shared_mutex _global_history_acc;

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
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    bool has_something_to_erase = false;
                    // No real commit for WR, since save point have to wait until entire transaction complete
                    // but we can remove all read locks unless ReadonlyBlockHint::ro_keep_lock used
                    for(auto i = begin(), iend = end(); i != iend; ++i)
                    {
                        auto& block = **i;
                        if(block._type == BlockType::ro //block for read
                             && !(block._flag & (std::uint8_t)ReadonlyBlockHint::ro_keep_lock) //keep lock in tran
                           )
                        {
                            block._type = BlockType::garbage; //mark block as garbage for future wipe
                            has_something_to_erase = true;
                        }
                    }
                    if(has_something_to_erase)
                        _framed_tran->_owner._cv_disposer.notify_one();
                    next_state(_tr_state); //disable changes in this
                }

                void rollback() override
                {
                    if(_tr_state >= TransactionState::sealed_noop)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    bool has_something_to_erase = false;
                    for (auto i = begin(), iend = end(); i != iend; ++i)
                    {
                        auto& block = **i;
                        /* on rollback remove all collected in this scope blocks*/
                        block._type = BlockType::garbage; //mark block as garbage for future wipe
                        has_something_to_erase = true;
                    }

                    if (has_something_to_erase)
                        _framed_tran->_owner._cv_disposer.notify_one();
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
                void store(history_iterator_t block)
                {
                    if(_tr_state != TransactionState::active)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    _transaction_log.emplace_back(std::move(block));
                }
                
                virtual void register_handle(std::unique_ptr<BeforeTransactionEnd> handler) override
                {
                    _end_listener.emplace_back(std::move(handler));
                }
                
                void rollback() override
                {
                    if(_tr_state >= TransactionState::sealed_noop)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    //invoke events on transaction end
                    for (auto& ev : _end_listener)
                    {
                        ev->on_rollback();
                    }

                    for(auto& block_iter: _transaction_log)
                    {
                        auto& block = *block_iter;
                        block._type = BlockType::garbage;
                    }

                    //notify background worker to dispose this tran
                    _owner.dispose_transaction(transaction_ptr_t{ TransactionShareMode{}, this });
                }

                void commit() override
                {
                    if(_tr_state >= TransactionState::sealed_rollback_only)
                        throw Exception(ErrorCode::er_transaction_ghost_state);
                    //invoke events on transaction end
                    for (auto& ev : _end_listener)
                    {
                        ev->on_commit();
                    }

                    for (auto& block_iter : _transaction_log)
                    {
                        auto& block = *block_iter;
                        if(block._type == BlockType::wr)
                        { //write-out changes
                            segment_pos_t byte_size = static_cast<segment_pos_t>(block._range.count());
                            auto mb = _owner.raw_writable_block(
                                FarAddress(block._range.pos()),
                                byte_size,
                                (WritableBlockHint)block._flag);
                            mb.byte_copy(block._shadow.get(), byte_size);
                        }
                        block._type = BlockType::garbage;
                    }

                    //notify background worker to dispose this tran
                    _owner.dispose_transaction(transaction_ptr_t{ TransactionShareMode{}, this });

                }
                
                /**Allows discover state if transaction still exists (not in ghost state) */
                bool is_active() const
                {
                    return _tr_state == TransactionState::active;
                }
                
                EventSourcingSegmentManager& _owner;

                using transaction_end_listener_t = std::vector<std::unique_ptr<BeforeTransactionEnd>>;

                TransactionState _tr_state = TransactionState::active;

                std::vector<history_iterator_t> _transaction_log;
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

            using transaction_impl_ptr_t = TransactionPtr<TransactionImpl>;

            /*just provide access to parent's writable-block*/
            MemoryChunk raw_writable_block(FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c)
            {
                return SegmentManager::writable_block(pos, size, hint);
            }

            /** Just take already existing block and check how it intersects with existing captured WR blocks */
            ReadonlyMemoryChunk readonly_block_no_retain(
                RWR search_range, ReadonlyMemoryChunk&& raw)
            {
                auto current_isolation = _read_isolation.load();
                //apply all event sourced to `new_buffer`
                
                shadow_buffer_t new_buffer = _shadow_buffer_cache.get(raw.count());//note: recycle of this buffer in future is impossible
                memcpy(new_buffer.get(), raw.at<std::uint8_t>(0), raw.count()); //copy origin persisted memory from disk

                std::shared_lock guard_history_ro(_global_history_acc);
                narrow_history_log(search_range, _global_history.end(), [&](auto& all_iter){
                // find all previously used block that have any intersection with query
                // to check if readonly block is allowed
                    const auto& block = *all_iter;
                    // iterate transaction log from oldest to newest and apply changes on 'raw' memory block                    
                    if (block._type != BlockType::wr)
                        return true;
                    auto joined_zone = OP::zones::join_zones(search_range, block._range);

                    if (joined_zone.empty())
                        return true;

                    //cannot capture because other WR- tran exists over this block
                    switch (current_isolation)
                    {
                        case ReadIsolation::Prevent:
                            //allow caller to retry later
                            throw ConcurrentLockException(
                                "cannot capture RO while it is used as WR in other transaction, or no transaction used");
                        case ReadIsolation::ReadUncommitted:
                        { //DIRTY-READ logic
                            auto offset_in_src = OP::utils::uint_diff_int(joined_zone.pos(), block._range.pos());
                            assert(offset_in_src >= 0);
                            auto offset_in_dest = OP::utils::uint_diff_int(joined_zone.pos(), search_range.pos());
                            assert(offset_in_dest >= 0);
                            memcpy(new_buffer.get() + offset_in_dest,
                                    block._shadow.get() + offset_in_src,
                                    joined_zone.count());
                            break;
                        }
                        case ReadIsolation::ReadCommitted:
                            [[fallthrough]];
                        default:
                            break; //jump to next of for
                    }
                    //do nothing, Ignore this WR block, proceed with origin RO memory. 
                    return true;
                });
                return ReadonlyMemoryChunk(std::move(new_buffer), raw.count(), raw.address());
            }

            void poulate_ro_block(const RWR& search_range, history_iterator_t end, 
                ReadonlyMemoryChunk& memory, Transaction::transaction_id_t current_tran)
            {
                auto current_isolation = _read_isolation.load();
                narrow_history_log(search_range, std::move(end), [&](auto& all_iter){
                // find all previously used block that have any intersection with query
                // to check if readonly block is allowed
                    const auto& block = *all_iter;
                    // iterate transaction log from oldest to newest and apply changes on result memory block                    
                    if (block._type != BlockType::wr)
                        return true;
                    auto joined_zone = OP::zones::join_zones(search_range, block._range);
                    if (joined_zone.empty())
                        return true;
                    if (block._used_in_transaction != current_tran)
                    {//another WR- tran exists over this block
                        switch (current_isolation)
                        {
                        case ReadIsolation::Prevent:
                        {
                            //block_leaking_guard ensures that: `new_block_iter->_type = garbage`
                            //exception, but caller may retry later
                            throw ConcurrentLockException(
                                "cannot capture RO while it is used as WR in other transaction, or no transaction used");
                        }
                        case ReadIsolation::ReadUncommitted:
                        { //DIRTY-READ logic, further code will copy this dirty chunk
                            break;
                        }
                        case ReadIsolation::ReadCommitted:
                            [[fallthrough]];
                        default:
                            //do nothing, Ignore this WR block, proceed with origin RO memory. 
                            return true; //jump to next of for
                        }
                    }
                    /** there are 4 relative positions of intersection existing block and new one:
                    * 1. existing on left:\code
                    * [--existing--]
                    *       [--new--]
                    * \endcode
                    * 2. existing on right:\code
                    *      [--existing--]
                    * [--new--]
                    * \endcode
                    * 3. existing adsorbs new:\code
                    * [------existing------]
                    *       [--new--]
                    * \endcode
                    * 4. new adsorbs existing:\code
                    *   [-existing-]
                    * [------new------]
                    * \endcode
                    */
                    auto offset_in_src = OP::utils::uint_diff_int(joined_zone.pos(), block._range.pos());
                    assert(offset_in_src >= 0);
                    auto offset_in_dest = OP::utils::uint_diff_int(joined_zone.pos(), search_range.pos());
                    assert(offset_in_dest >= 0);

                    memcpy(memory.pos() + offset_in_dest,
                        block._shadow.get() + offset_in_src,
                        joined_zone.count());
                    return true;
                });
            }

            void dispose_transaction(transaction_impl_ptr_t tran)
            {
                auto erased_tran_id = tran->transaction_id();
                {//block to place tran to garbage collector
                    std::unique_lock acc (_dispose_lock);
                    _ready_to_dispose.emplace_back(std::move(tran));
                    _cv_disposer.notify_one();
                }
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

            /** Worker method to wipe from _global_history not used references*/
            void collect_garbage()
            {
                while(!_stop_garbage_collector.load())
                {
                    //dispose closed transactions
                    std::unique_lock<std::mutex> acc_job(_dispose_lock);
                    if(!_ready_to_dispose.empty())
                    {
                        //take one to destroy
                        auto tran = std::move(_ready_to_dispose.back());
                        _ready_to_dispose.pop_back();
                        acc_job.unlock(); //allow contribute from another thread

                        //utilize all shadow buffers and wipe entry from global history
                        for (auto& block_iter : tran->_transaction_log)
                        {
                            auto& block = *block_iter;
                            _shadow_buffer_cache.utilize(std::move(block._shadow)); //allow shadow memory reuse
                            std::lock_guard g(_global_history_acc);//lock to remove item from global history
                            //erase left margin
                            for(auto b = _index_ranges.find(block._range.pos());
                                b != _index_ranges.end(); ++b)
                            {
                                if( b->second == block_iter )
                                {
                                    _index_ranges.erase(b);
                                    break;
                                }
                                    
                            }
                            //erase right margin
                            for(auto b = _index_ranges.find(block._range.right());
                                b != _index_ranges.end(); ++b)
                            {
                                if( b->second == block_iter )
                                {
                                    _index_ranges.erase(b);
                                    break;
                                }
                            }

                            _global_history.erase(block_iter);
                        }
                    }
                    else //put on wait until something appear or full GC stop
                        _cv_disposer.wait(acc_job, [this](){
                            return _stop_garbage_collector || !_ready_to_dispose.empty();
                        });
                }
            }

            /** \return position in _global_history where item has been added. 
            *   \pre `_global_history_acc` is unique locked
            */
            history_iterator_t add_global_history(
                const RWR& search_range, BlockType type, Transaction::transaction_id_t transaction_id,
                ShadowBuffer buffer, std::uint8_t flag)
            {
                _global_history.emplace_back(
                    search_range,
                    type,
                    transaction_id,
                    ++ _epoch,
                    std::move(buffer),
                    flag
                );
                
                auto new_block_iter = _global_history.end();
                --new_block_iter;

                _index_ranges.emplace(search_range.pos(), new_block_iter);
                _index_ranges.emplace(search_range.right(), new_block_iter); //add right margin as well

                return new_block_iter;
            }
            
            static bool cmp_history_iterator_by_epoch(const history_iterator_t& left, const history_iterator_t& right)
            {
                return left->_epoch > right->_epoch; //priority queue organized in reverse order
            }

            /** \brief Tries to improve (narrow) heuristic of scanning _global_history
            *   \pre `_global_history_acc` at least is share locked
            */
            template <class F>
            void narrow_history_log(
                const RWR& search_range, history_iterator_t end, F callback) 
            {
                if( _global_history.size() < 10 )
                {
                    for(auto i = _global_history.begin(); i != end; ++i)
                    { 
                        if( !callback(i) )
                            break;
                    }
                }
                else
                {
                    std::priority_queue<history_iterator_t, std::vector<history_iterator_t>, decltype(&cmp_history_iterator_by_epoch)>
                        history_queue(&cmp_history_iterator_by_epoch);
                    for(auto begin = _index_ranges.lower_bound(search_range.pos()), 
                            top = _index_ranges.lower_bound(search_range.right());
                        begin != top && begin != _index_ranges.end();
                        ++begin)
                    {
                        if(begin->second != end) //don't add just appended record
                            history_queue.push(begin->second);
                    }
                    for(; !history_queue.empty(); history_queue.pop())
                        callback(history_queue.top());
                }
            }

            ShadowBufferCache _shadow_buffer_cache = {};

            using opened_transactions_t = std::unordered_map<std::thread::id, transaction_ptr_t>;
            opened_transactions_t _opened_transactions;
            mutable std::shared_mutex _opened_transactions_lock;

            std::atomic<Transaction::transaction_id_t> _transaction_uid_gen;

            /**Allows notify background worker to check for some job*/
            std::condition_variable _cv_disposer;
            std::atomic<bool> _stop_garbage_collector = false;
            std::atomic<size_t> _ro_tran = 0;
            mutable std::mutex _dispose_lock;
            std::deque<transaction_impl_ptr_t> _ready_to_dispose;
            /**background worker to release redundant records from history */
            std::thread _garbage_collector;
            std::atomic<ReadIsolation> _read_isolation = ReadIsolation::ReadCommitted;
        };
        

    } //ns::trie
}//ns::OP

#endif //_OP_VTM_EVENTSOURCINGSEGMENTMANAGER__H_
