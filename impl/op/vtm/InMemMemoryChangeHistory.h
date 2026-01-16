#pragma once
#ifndef _OP_VTM_INMEMORYCHANGEHISTORY__H_
#define _OP_VTM_INMEMORYCHANGEHISTORY__H_

#include <shared_mutex>
#include <condition_variable>
#include <list>

#include <op/vtm/EventSourcingSegmentManager.h>

namespace OP::vtm
{
    
    struct InMemoryChangeHistory : MemoryChangeHistory
    {
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;

        InMemoryChangeHistory()
            : _garbage_collector(&InMemoryChangeHistory::collect_committed, this)
        {
        }
        
        ~InMemoryChangeHistory()
        {
            _stop_garbage_collection = true;
            _need_garbage_condition.notify_one();
            _garbage_collector.join();
            std::unique_lock lock(_global_history_acc);
            while (_global_history_begin)
            {
                auto temp = _global_history_begin->_next;
                delete _global_history_begin;
                _global_history_begin = temp;
            }
        }

        [[nodiscard]] ShadowBuffer allocate(
            const RWR& search_range, 
            transaction_id_t transaction_id, 
            MemoryRequestType hint, 
            const void* init_data) override
        {
            if(hint >= MemoryRequestType::wr)
            { //need to retain block
                std::unique_lock guard_history_wr(_global_history_acc);
                auto& new_block = add_global_history(
                    hint,
                    search_range,
                    BlockType::init,
                    transaction_id
                );
                //there are 2 scenarios of race there. 
                // 1) separate threads & separate transactions;
                //      to check (and behave accordingly to ReadIsolation rules) it is enough to check RWR data.
                // 2) separate threads & same transaction.
                //      when apply-history meets block in Init state - it should wait.


                //guard that BlockType::init will be altered
                RAIIBlockGuard block_leaking_guard(new_block);

                guard_history_wr.unlock(); // no more changes of global history list are needed
                ShadowBuffer result = new_block.buffer();
                
                if (hint != MemoryRequestType::wr_no_history)
                {
                    if (init_data)//copy origin from init
                        memcpy(result.get(), init_data, search_range.count());
                    std::shared_lock guard_history_ro(_global_history_acc); //shared ro access are enough to iterate
                    populate_ro_block(search_range, result, transaction_id, &new_block, 
                        // for writes need avoid races, so not looking at _isolation and keep always 'Prevent'
                        ReadIsolation::Prevent
                        );
                }
                block_leaking_guard.exchange(BlockType::wr); //block is ready for use
                return result;
            }
            else // no retains, just populate buffer with intersected blocks
            {
                ShadowBuffer new_buffer {new std::uint8_t[search_range.count()], search_range.count(), true};
                //copy origin from init
                if (init_data)
                    memcpy(new_buffer.get(), init_data, search_range.count());
                std::shared_lock guard_history_ro(_global_history_acc); //shared ro access are enough to iterate
                populate_ro_block(search_range, new_buffer, transaction_id, nullptr,
                    _isolation.load()
                );
                return new_buffer;
            }
        }

        void destroy(transaction_id_t tid, ShadowBuffer buffer) override
        {
            // restore back pointer to BlockProfile
            std::uint8_t* field_buffer = buffer.get();
            BlockProfile* block = reinterpret_cast<BlockProfile*>(
                field_buffer - offsetof(BlockProfile, _memory));
            assert(tid == block->_used_in_transaction);
            assert(block->_signature == BlockProfile::signature_c);
            //mark block for garbage collection
            auto old_state = block->_type.exchange(BlockType::garbage);
            assert(old_state == BlockType::wr);
        }

        [[maybe_unused]] ReadIsolation read_isolation(ReadIsolation new_level) override
        {
            return _isolation.exchange(new_level);
        }

        void on_new_transaction(transaction_id_t ) override
        {
            //do nothing
        }

        void on_commit(transaction_id_t id) override
        {
            complete_transaction(id);
        }

        void on_rollback(transaction_id_t id) override
        {
            std::shared_lock rlock(_global_history_acc);
            //for (auto i = begin(), iend = end(); i != iend; ++i)
            //{
            //    auto& block = **i;
            //    /* on rollback remove all collected in this scope blocks*/
            //    block._type = BlockType::garbage; //mark block as garbage for future wipe

            rlock.unlock();
            complete_transaction(id);
        }

    private:
        enum class BlockType : std::uint_fast8_t
        {
            /** block allocated but not usable yet*/
            init = 0,
            wr,
            garbage
        };



        /**Properties of captured block in particular transaction*/
        struct BlockProfile
        {
            static constexpr std::uint32_t signature_c = (((std::uint32_t('B') << 8) | std::uint32_t('p')) << 8) | std::uint32_t('0');

            BlockProfile(
                RWR range, 
                BlockType type,
                transaction_id_t used_in_transaction,
                std::uint64_t epoch
            )
                : _range(range)
                , _type(type)
                , _used_in_transaction(used_in_transaction)
                , _epoch(epoch)
            {
            }

            ShadowBuffer buffer()
            {
                return ShadowBuffer{ _memory, _range.count(), false };
            }

            /** position/size of block */
            const RWR _range;
            /** type of retain readonly/writable (and init state) */ 
            std::atomic<BlockType> _type = BlockType::init;
            /** what transaction retains the block */
            const transaction_id_t _used_in_transaction;
            const std::uint64_t _epoch;
            BlockProfile* _next = nullptr;
            const std::uint32_t _signature = signature_c;
            /** shadow memory of changes */
            alignas(ShadowBuffer) std::uint8_t _memory[1] = {};
        };


        /** Allows guard just created BlockProfile from memory leaks. If explicit #exchange is not
        * called block automatically marked to #BlockType::garbage at guard scope exit.
        */
        struct RAIIBlockGuard
        {
            explicit RAIIBlockGuard(BlockProfile& block) noexcept
                : _block(block)
                , _origin_type(_block._type.load())
            {
            }

            ~RAIIBlockGuard() noexcept
            {
                if (_origin_type != BlockType::garbage)
                {
                    _block._type = BlockType::garbage;
                }
            }

            /** @return previous block type value. */
            [[maybe_unused]] BlockType exchange(BlockType new_type) noexcept
            {
                auto result = _block._type.exchange(new_type);
                _origin_type = BlockType::garbage; //prevent destroying at destructor
                return result;
            }

            BlockProfile& _block;
            BlockType _origin_type;
        };

        std::atomic<ReadIsolation> _isolation = ReadIsolation::ReadCommitted;
        BlockProfile *_global_history_begin = nullptr, **_global_history_end = &_global_history_begin;
        std::atomic<std::uint64_t> _epoch{0};
        std::shared_mutex _global_history_acc;

        std::thread _garbage_collector;
        std::unordered_set<transaction_id_t> _completed_transactions;
        std::mutex _completed_transactions_acc;
        std::atomic<bool> _stop_garbage_collection = false;
        std::condition_variable _need_garbage_condition;

        /** \return position in _global_history where item has been added. 
        *   \pre `_global_history_acc` is unique locked
        */
        BlockProfile& add_global_history(
            MemoryRequestType hint,
            RWR search_range, 
            BlockType type, 
            transaction_id_t transaction_id)
        {
            segment_pos_t mem_block_size = sizeof(BlockProfile) + search_range.count();
            std::byte* buffer = new std::byte[mem_block_size];
            BlockProfile* new_block = new (buffer) BlockProfile(
                search_range, type, transaction_id,
                ++_epoch);
            *_global_history_end = new_block;
            _global_history_end = &new_block->_next;                
            return *new_block;
        }

        void populate_ro_block(
            const RWR& search_range,
            ShadowBuffer& new_buffer, 
            transaction_id_t current_tran,
            BlockProfile* current, 
            ReadIsolation current_isolation)
        {
            // find all previously used block that have any intersection with query
            // to check if readonly block is allowed
            for (auto i = _global_history_begin; i != nullptr && i != current; i = i->_next)
            { // iterate transaction log from oldest to newest and apply changes on result memory block

                auto& block = *i;
                if (block._type == BlockType::garbage)
                    continue;
                //Zone check goes first because it valid for all types of concurrency check
                auto joined_zone = OP::zones::join_zones(search_range, block._range);
                if (joined_zone.empty())  //no intersection => no race
                    continue;
                                    
                if (block._used_in_transaction != current_tran)
                {//another WR- tran exists over this block
                    switch (current_isolation)
                    {
                    case ReadIsolation::Prevent:
                    {
                        //block_leaking_guard ensures that: `new_block_iter->_type = garbage`
                        //exception, but caller may retry later
                        throw ConcurrentLockException();
                    }
                    case ReadIsolation::ReadUncommitted:
                    { //DIRTY-READ logic, further code will copy this dirty chunk
                        break;
                    }
                    case ReadIsolation::ReadCommitted:
                        [[fallthrough]];
                    default:
                        //do nothing, Ignore this WR block, proceed with origin RO memory. 
                        continue; //jump to next of for
                    }
                }
                else
                { //same tran, but another thread => wait until init complete
                    if (_wait_atomic_value(block._type, BlockType::wr) == BlockType::garbage)
                        continue;
                    //there BlockType::wr for sure.
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

                memcpy(
                    new_buffer.get() + offset_in_dest,
                    block._memory + offset_in_src,
                    joined_zone.count()
                );
            }
        }

        void complete_transaction(transaction_id_t transaction_id)
        {
            std::unique_lock lock(_completed_transactions_acc);
            _completed_transactions.emplace(transaction_id);
            _need_garbage_condition.notify_one();
        }

        /** background thread function to clean memory */
        void collect_committed() noexcept
        {
            while (!_stop_garbage_collection.load())
            {
                std::unique_lock lock(_completed_transactions_acc);
                if (_completed_transactions.empty())
                { //wait until some transaction complete
                    _need_garbage_condition.wait(lock,
                        [this]() {
                            return !_completed_transactions.empty()
                                || _stop_garbage_collection.load()
                                ;
                        });
                }
                if (_stop_garbage_collection.load())
                    return; //stop immediately
                std::unordered_set<transaction_id_t> id_clone{ _completed_transactions };
                _completed_transactions.clear();
                lock.unlock(); //allow collect other completed transactions
                std::unique_lock history_guard(_global_history_acc);
                BlockProfile* prev = nullptr;
                for (auto** i = &_global_history_begin; *i != nullptr; )
                {
                    if (id_clone.find((*i)->_used_in_transaction) != id_clone.end())
                    {
                        destroy_history_item(i); //i is updated automatically
                    }
                    else
                        i = &(*i)->_next;
                }
            }
            
        }
        /**
        * \pre  _global_history_acc - acquired
        */
        void destroy_history_item(BlockProfile** holder)
        {
            auto to_delete = *holder;
            if (_global_history_end == &(to_delete->_next)) //last item
            {
                _global_history_end = holder;
            }
            *holder = to_delete->_next;
            delete to_delete;
        }

        /** Wait until atomic `ref` get value `old` */
        template <class T>
        static inline T _wait_atomic_value(std::atomic<T>& ref, T expected) noexcept
        {
            T result = ref.load();
            while (result < expected)
            {
                std::this_thread::yield();
                result = ref.load();
            }
            return result;
        }

    };
}//ns:OP::vtm

#endif //_OP_VTM_INMEMORYCHANGEHISTORY__H_
