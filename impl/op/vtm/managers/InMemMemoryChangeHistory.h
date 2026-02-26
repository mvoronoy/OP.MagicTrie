#pragma once
#ifndef _OP_VTM_INMEMORYCHANGEHISTORY__H_
#define _OP_VTM_INMEMORYCHANGEHISTORY__H_

#include <shared_mutex>
#include <condition_variable>
#include <list>

#include <op/common/ThreadPool.h>
#include <op/common/Bitset.h>
#include <op/common/atomic_utils.h>

#include <op/vtm/managers/EventSourcingSegmentManager.h>
#include <op/vtm/managers/BucketIndexedList.h>

namespace OP::vtm
{
    struct Recovery
    {
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;

        virtual std::uint32_t serialize_size() const = 0;
        virtual void serialize(void* buffer) const = 0;
        virtual void deserialize(const void* buffer) = 0;

        virtual void begin_transaction(transaction_id_t) = 0;
        virtual void store_origin(transaction_id_t, FarAddress origin, const void* init_data, size_t buffer_size) = 0;

        virtual void commit_begin(transaction_id_t) = 0;
        virtual void commit_end(transaction_id_t) = 0;

        virtual void recovery(EventSourcingSegmentManager&) = 0;
    };
    
    struct InMemoryChangeHistory : MemoryChangeHistory
    {
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;
        using query_region_result_t = typename MemoryChangeHistory::query_region_result_t;
        using query_region_error_t = typename MemoryChangeHistory::ConcurrentAccessError;

        explicit InMemoryChangeHistory(OP::utils::ThreadPool& thread_pool)
            : _thread_pool(thread_pool)
        {
        }
        
        ~InMemoryChangeHistory()
        {
            std::unique_lock guard(_garbage_collection_future_acc);
            if(_garbage_collection_future.valid())
            {
                _garbage_collection_future.get();
            }
            guard.release();

            _global_history.clear();
        }

        OP::utils::ThreadPool& thread_pool() noexcept
        {
            return _thread_pool;
        }

        [[nodiscard]] query_region_result_t buffer_of_region(
            const RWR& search_range, 
            transaction_id_t transaction_id, 
            MemoryRequestType hint, 
            const void* init_data) override
        {
            if(hint >= MemoryRequestType::wr)
            { //need to retain block immediately, forming linear history
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

                if (hint == MemoryRequestType::wr)
                {
                    ShadowBuffer new_block_buffer = new_block.buffer();
                    if (init_data)//copy origin from init
                        memcpy(new_block_buffer.get(), init_data, search_range.count());
                    query_region_result_t result = populate_ro_block(search_range,
                        std::move(new_block_buffer),
                        transaction_id,
                        &new_block,
                        // for writes need avoid races, so not looking at _isolation and keep always 'Prevent'
                        ReadIsolation::Prevent);
                    if(std::holds_alternative<query_region_error_t>(result))
                    {
                        return result; //concurrent lock exception
                    }
                    // expose block to all waiters
                    block_leaking_guard.exchange(BlockType::wr); //block is ready for use
                    return result;
                }
                else //strongly MemoryRequestType::wr_no_history
                {
                    query_region_result_t result = check_no_locks(
                        new_block, new_block.buffer(), transaction_id);
                    if (std::holds_alternative<query_region_error_t>(result))
                        return result; //concurrent lock exception
                    // expose block to all waiters
                    block_leaking_guard.exchange(BlockType::wr); //block is ready for use
                    return result;
                }
            }
            else // no retains, just populate buffer with intersected blocks
            {
                ShadowBuffer new_buffer {new std::uint8_t[search_range.count()], search_range.count(), true};
                //copy origin from init
                if (init_data)
                    memcpy(new_buffer.get(), init_data, search_range.count());
                return populate_ro_block(
                    search_range,
                    std::move(new_buffer),
                    transaction_id, nullptr,
                    _isolation.load());
            }
        }

        /** cheap way to mark block as garbage */
        void destroy(transaction_id_t tid, ShadowBuffer buffer) override
        {
            if (buffer.is_owner()) //memory allocated for RO and no entry in global_history
                return; //there ShadowBuffer will automatically destroy memory
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
            OP::utils::Waitable<BlockType> _type = { BlockType::init, std::memory_order_release, std::memory_order_acquire };
            /** what transaction retains the block */
            const transaction_id_t _used_in_transaction;
            const std::uint64_t _epoch;
            const std::uint32_t _signature = signature_c;
            /** shadow memory of changes */
            alignas(ShadowBuffer) std::uint8_t _memory[1] = {};
        };

        struct BlockByRWRIndexer
        {
            std::atomic<std::uint64_t>
                _bloom_filter = 0,
                _min_left = std::numeric_limits<transaction_id_t>::max(),
                _max_right = 0;

            static inline constexpr std::uint64_t bloom_calc(const RWR& range) noexcept
            {
                auto bit_width = OP::common::log2(range.count()) + 1;
                return ((1ull << bit_width) - 1)
                    << OP::common::log2(range.pos());
            }

            void index(const BlockProfile& block) noexcept
            {
                _bloom_filter.fetch_or(bloom_calc(block._range));
                //c++26?: _min.fetch_min(r._value);
                OP::utils::cas_extremum<std::less>(_min_left, block._range.pos());
                //c++26?: _max.fetch_max(r._value);
                OP::utils::cas_extremum<std::greater>(_max_right, block._range.right());
            }

            bool check(const BlockProfile& block) const noexcept
            {
                return check(block._range);
            }

            bool check(const RWR& query) const noexcept
            {
                auto bloom = bloom_calc(query);

                return (bloom & _bloom_filter.load(std::memory_order_acquire)) != 0
                    && query.pos() < _max_right.load(std::memory_order_acquire)
                    && query.right() > _min_left.load(std::memory_order_acquire)
                    ;
            }
        };

        struct BlockByTransactionIdIndexer
        {
            std::atomic<transaction_id_t>
                _bloom_filter = 0, 
                _min = std::numeric_limits<transaction_id_t>::max(), 
                _max = 0;
            
            constexpr static transaction_id_t hash(transaction_id_t v) noexcept
            {
                return 0x5fe14bf901200001ull * v;
            }

            void index(const BlockProfile& block) noexcept
            {
                _bloom_filter.fetch_or(hash(block._used_in_transaction));
                //c++26?: _min.fetch_min(r._value);
                OP::utils::cas_extremum<std::less>(_min, block._used_in_transaction);
                //c++26?: _max.fetch_max(r._value);
                OP::utils::cas_extremum<std::greater>(_max, block._used_in_transaction);
            }

            bool check(const BlockProfile& block) const noexcept
            {
                return check(block._used_in_transaction);
            }

            bool check(transaction_id_t query) const noexcept
            {
                const auto hash_v = hash(query);

                return (hash_v & _bloom_filter.load(std::memory_order_acquire)) == hash_v
                    && query >= _min.load(std::memory_order_acquire)
                    && query <= _max.load(std::memory_order_acquire)
                    ;
            }

        };
        /** Aggregates several BlockProfile to speed up lookup by RWR and/or transaction-id */
        using indexed_history_list_t =
            BucketIndexedList<BlockProfile, BlockByRWRIndexer, BlockByTransactionIdIndexer>;

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
                    _block._type.store(BlockType::garbage);//notifies if some thread(s) wait
                }
            }

            /** @return previous block type value. */
            [[maybe_unused]] BlockType exchange(BlockType new_type) noexcept
            {
                auto result = _block._type.exchange(new_type); //notifies if some thread(s) wait
                _origin_type = BlockType::garbage; //prevent destroying at destructor
                return result;
            }

            BlockProfile& _block;
            BlockType _origin_type;
        };

        std::atomic<ReadIsolation> _isolation = ReadIsolation::ReadCommitted;

        std::atomic<std::uint64_t> _epoch{0};

        OP::utils::ThreadPool& _thread_pool;
        std::unordered_set<transaction_id_t> _completed_transactions;
        std::mutex _completed_transactions_acc;

        std::future<void> _garbage_collection_future;
        std::mutex _garbage_collection_future_acc;

        indexed_history_list_t _global_history;

        /** \return position in _global_history where item has been added. 
        *   \pre `_global_history_acc` is unique locked
        */
        BlockProfile& add_global_history(
            MemoryRequestType hint,
            const RWR& search_range, 
            BlockType type, 
            transaction_id_t transaction_id)
        {
            segment_pos_t mem_block_size = sizeof(BlockProfile) + search_range.count();
            std::byte* buffer = new std::byte[mem_block_size];
            
            std::unique_ptr<BlockProfile> new_block (new (buffer) BlockProfile(
                search_range, type, transaction_id,
                ++_epoch));

            BlockProfile* result = new_block.get();
            _global_history.append(std::move(new_block));
            return *result;
        }

        template <class F>
        void indexed_for_each(const RWR& query, F&& callback)
        {
            _global_history.indexed_for_each(query, callback);
        }


        template <class F>
        void for_each(F&& callback)
        {
            _global_history.for_each(callback);
        }

        
        /** \return true on success and false if ConcurrentException must be raised */
        query_region_result_t populate_ro_block(
            const RWR& search_range,
            ShadowBuffer&& new_buffer, 
            transaction_id_t current_tran,
            BlockProfile* current, 
            ReadIsolation current_isolation) noexcept
        {
            query_region_result_t result = std::move(new_buffer); //optimistic scenario
            // find all previously used block that have any intersection with query
            // to check if readonly block is allowed
            // iterate transaction log from oldest to newest and apply changes on result memory block
            indexed_for_each(search_range, [&](BlockProfile& block)->bool{ 
                if (&block == current) //reach the end
                    return false;

                //Zone check goes first because it valid for all types of concurrency check
                auto joined_zone = OP::zones::join_zones(search_range, block._range);
                if (joined_zone.empty())  //no intersection => no race
                    return true;
                if (block._type.load() == BlockType::garbage)
                    return true;

                if (block._used_in_transaction != current_tran)
                {//another WR- tran exists over this block
                    switch (current_isolation)
                    {
                    case ReadIsolation::Prevent:
                    {
                        //block_leaking_guard ensures that: `new_block_iter->_type = garbage`
                        //exception, but caller may retry later
                        result = query_region_error_t{//throw ConcurrentLockException();
                            search_range, current_tran,
                            block._range, block._used_in_transaction };
                        return false;//stop iteration
                    }
                    case ReadIsolation::ReadUncommitted:
                    { //DIRTY-READ logic, further code will copy this dirty chunk
                        break;
                    }
                    case ReadIsolation::ReadCommitted:
                        [[fallthrough]];
                    default:
                        //do nothing, Ignore this WR block, proceed with origin RO memory. 
                        return true; //jump to the next of iteration
                    }
                }
                else
                { //same tran, but another thread => wait until init complete
                    if (block._type.wait_condition<std::less>(BlockType::wr) == BlockType::garbage)
                        return true;
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
                    std::get<ShadowBuffer>(result).get() + offset_in_dest,
                    block._memory + offset_in_src,
                    joined_zone.count()
                );
                return true; //iterate next
            });
            return result;
        }

        query_region_result_t check_no_locks(
            BlockProfile& current,
            ShadowBuffer&& new_buffer,
            transaction_id_t current_tran) noexcept
        {
            query_region_result_t result = std::move(new_buffer); //optimistic scenario
            indexed_for_each(current._range, [&](BlockProfile& block)->bool {
                if (&block == &current) //reach the end
                    return false;
                if (current_tran == block._used_in_transaction) //not interesting of same transaction blocks, skip it
                    return true;
                if (block._type.load() == BlockType::garbage)
                    return true;
                auto joined_zone = OP::zones::join_zones(current._range, block._range);
                if (joined_zone.empty())  //no intersection => no race
                    return true;
                result = query_region_error_t{//throw ConcurrentLockException();
                    current._range, current_tran,
                    block._range, block._used_in_transaction };
                return false;//stop iteration
                });
            return result;
        }

        void complete_transaction(transaction_id_t transaction_id)
        {
            // soft remove of associated blocks, cleaning logic is delegated to garbage collection process
            _global_history.soft_remove_if_all(transaction_id, [transaction_id](BlockProfile& block) {
                    if(block._used_in_transaction == transaction_id)
                    {
                        //mark block as garbage to exclude from history review
                        block._type.store(BlockType::garbage); //notifies if some thread(s) wait
                        return true; //soft remove
                    }
                    return false;
            });
            if (_global_history.empty_buckets_count()) //nudge garbage collection
            {
                initiate_garbage_collection();
            }
        }

        void initiate_garbage_collection()
        {
            std::unique_lock guard(_garbage_collection_future_acc);
            
            _garbage_collection_future = thread_pool().async([this](decltype(_garbage_collection_future) && previous) {
                    if(previous.valid())
                        previous.get(); //wait until previous gc cycle done
                    collect_committed();
                }, std::move(_garbage_collection_future)
            );
        }

        /** background thread function to clean memory */
        void collect_committed() noexcept
        {
            _global_history.clean();
        }

    };
}//ns:OP::vtm

#endif //_OP_VTM_INMEMORYCHANGEHISTORY__H_
