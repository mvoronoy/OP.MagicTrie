#pragma once
#ifndef _OP_VTM_HISTORYRECOVERY__H_
#define _OP_VTM_HISTORYRECOVERY__H_

#include <unordered_map>

#include <op/vtm/InMemMemoryChangeHistory.h>
#include <op/vtm/FileRotation.h>

namespace OP::vtm
{
    struct A0lRecovery : Recovery
    {
        constexpr static size_t transactions_per_file_c = 8;
        using transaction_id_t = typename Recovery::transaction_id_t;
        using RWR = typename MemoryChangeHistory::RWR;

        A0lRecovery(std::unique_ptr<CreationPolicy> create_policy, OP::utils::ThreadPool& thread_pool)
            : _create_policy(std::move(create_policy))
            , _thread_pool(thread_pool)
            , _next_file(_thread_pool.async(&A0lRecovery::warm_up_file, this))
        {
        }

        virtual std::uint32_t serialize_size() const override
        {
            return OP::utils::aligned_sizeof<PersistState>(SegmentDef::align_c);
        }

        virtual void serialize(void* buffer) const override
        {
            memcpy(buffer, &_state, sizeof(PersistState));
        }

        virtual void deserialize(const void* buffer) override
        {
            memcpy(_state, buffer, sizeof(PersistState));
            if(_state._signature != PersistState::signature_c)
                throw Exception(vtm::ErrorCodes::er_invalid_signature, "recovery-log");
        }

        virtual void begin_transaction(transaction_id_t tid) override
        {
            auto [warm_file, r_guard] = get_log(tid); //r-guard is enough since A0List has own locking
            auto [data_lst, tran_lst] = warm_file;
            tran_lst->emplace(TransactionProgress::opened, tid);
        }

        virtual void store_origin(transaction_id_t tid, FarAddress origin, const void* init_data, size_t buffer_size) override
        {
            auto [warm_file, r_guard] = get_log(tid); //r-guard is enough since A0List has own locking
            auto [data_lst, tran_lst] = warm_file;
            auto& logging = data_lst.append_log();
            //make memory alloc for buffer
            auto [address_of_backup, buffer] = logging.allocate(buffer_size);
            mempcy(buffer, init_data, buffer_size);
            data_lst.emplace(tid, address_of_backup, origin, buffer_size);
        }

        virtual void commit_begin(transaction_id_t tid) override
        {
            auto [warm_file, r_guard] = get_log(tid); //r-guard is enough since A0List has own locking
            auto [data_lst, tran_lst] = warm_file;
            tran_lst->indexed_for_each(tid, [&](TransactionProgress& transaction) {
                if (transaction._transaction != tid) //Bloom filter may be false positive
                    return true; //continue iteration
                // from this moment transaction treated as subject for compensate process on recovery
                transaction._status = TransactionProgress::commiting; 
                return false; //stop iteration
                });
        }

        virtual void commit_end(transaction_id_t) override
        {
            auto [warm_file, r_guard] = get_log(tid); //r-guard is enough since A0List has own locking
            auto [_, tran_lst] = warm_file;
            size_t already_closed = 0;
            tran_lst->for_each(tid, [&](TransactionProgress& transaction) {
                if (transaction._transaction == tid)
                {
                    assert(transaction._status == TransactionProgress::commiting);
                    // from this moment transaction is safely closed
                    transaction._status = TransactionProgress::closed;
                    ++already_closed;
                } else if(transaction._status == TransactionProgress::closed) //just count how many are already closed
                    ++already_closed;

                return true; //continue iteration to count `already_closed`
            });
            if (already_closed == transactions_per_file_c)
            { //evict file for deletion
                auto log_file = tran_lst->append_log();//the copy
                r_guard.unlock();
                std::unique_lock guard(_open_logs_acc);
                _open_logs.erase(bucket_index(tid));
                guard.unlock();
                _create_policy->utilize_file(log_file);
            }
        }

        virtual void recovery(EventSourcingSegmentManager& segment_manager) override
        {
            // precondition #deserialize has been called
            for (auto& [id, log_file] : _create_policy->all())
            {
                FileHeader *header = log_file->at<FileHeader>(FarAddress{ 0 });
                if (header->_signature != FileHeader::signature_c)
                    throw Exception(vtm::ErrorCodes::er_invalid_signature, log_file->file_name().str());
                auto backup_list = backup_list_t::open(log_file, header->_backups_list);
                auto tran_list = transaction_list_t::open(log_file, header->_transaction_list);
                size_t closed = 0;
                tran_list->for_each([&](const TransactionProgress& progress) {
                    recover_transaction(segment_manager, progress, backup_list);
                });
                //delete file
                _create_policy->utilize_file(log_file);
            }

        }

    private:
        
        struct PersistState
        {
            constexpr static std::uint32_t signature_c = (((((('R' << 8) | 'e') << 8) | 'c') << 8) | 'L') ;
            constexpr static transaction_id_t no_tran = ~transaction_id_t{};

            const std::uint32_t _signature = signature_c;
            std::uint64_t _file_order = 0;
            transaction_id_t _min_transaction = no_tran;
            transaction_id_t _max_transaction = no_tran;
        };

        struct FileHeader
        {
            constexpr static std::uint32_t signature_c = (((((('R' << 8) | 'e') << 8) | 'c') << 8) | 'H') ;
            const std::uint32_t _signature = signature_c;
            FarAddress _backups_list{};
            FarAddress _transaction_list{};
        };

        /** chunk of SegmentManager to restore if transaction was interrupted during backup
        * Persisted in file managed by `_current_log`
        */
        struct BackupRecord
        {
            BackupRecord(
                transaction_id_t transaction,
                FarAddress address_of_backup,
                FarAddress origin_addr,
                std::uint64_t buffer_size) noexcept
                : _transaction(transaction)
                , _address_of_backup(address_of_backup)
                , _origin_addr(origin_addr)
                , _buffer_size(buffer_size)
            {
            }

            transaction_id_t _transaction;
            /**address_of_backup */
            FarAddress _address_of_backup;
            /** address in source SegmentManager */
            FarAddress _origin_addr;
            std::uint32_t _buffer_size; //segment size cannot exceed uint32
        };


        struct TransactionProgress
        {
            enum
            {
                opened = 0,
                commiting,
                closed
            };
            std::uint8_t _status = opened;
            transaction_id_t _transaction;
        };

        /** allows a little improve scan on transaction-id inside single file
        *   (supports as as well BlockProfile indexing)
        */
        struct TransactionIdIndexer
        {
            /*[serialized in A0Log]*/ std::uint64_t _bloom_filter = 0;
            /*[serialized in A0Log]*/ transaction_id_t _min = std::numeric_limits<std::uint64_t>::max();
            /*[serialized in A0Log]*/ transaction_id_t _max = 0;

            /** For a Bloom filter dealing with sequentially growing std::uint64_t keys, the main concern
            *   is ensuring the hash function distributes these non-random inputs uniformly across the bit array.
            */
            static std::uint64_t hash(transaction_id_t item_to_index) noexcept
            {
                //good spreading of sequential bits for average case when transaction_id grows monotony
                return bloom_filter_code(item_to_index);// * 0x5fe14bf901200001ull; //(0x60ff8010405001)
            }

            /**  calculate bloom-filter index and min-max index for transaction id */
            inline void _index(transaction_id_t item_to_index) noexcept
            {
                _bloom_filter |= hash(item_to_index);
                if (item_to_index < _min)
                    _min = item_to_index;
                if (_max < item_to_index)
                    _max = item_to_index;
            }

            template <class T>
            inline void index(const T& item_to_index) noexcept
            {
                _index(item_to_index._transaction);
            }

            inline OP::vtm::BucketNavigation check(transaction_id_t item) const noexcept
            {
                const auto hc = hash(item);
                return item >= _min
                    && item <= _max
                    && (_bloom_filter & hc) == hc //must match all bits
                    ? OP::vtm::BucketNavigation::not_sure
                    : OP::vtm::BucketNavigation::next
                    ;
            }

            template <class T>
            inline OP::vtm::BucketNavigation check(const T& item) const noexcept
            {
                return check(item._transaction);
            }

        };

        using backup_list_t = AppendOnlySkipList<64, BackupRecord, TransactionIdIndexer>;
        using backup_list_ptr = std::shared_ptr<backup_list_t>;
        using transaction_list_t = AppendOnlySkipList<8, TransactionProgress, TransactionIdIndexer>;
        using transaction_list_ptr = std::shared_ptr<transaction_list_t>;
        using warm_file_t = std::pair<backup_list_ptr, transaction_list_ptr>;
        using r_guard_t = std::shared_lock<std::shared_mutex>;

        warm_file_t warm_up_file()
        {
            auto file = _create_policy->make_new_file(_state._file_order++);
            auto [_, header] = file.construct<FileHeader>();
            auto [backup_addr, backup_list] = backup_list_t::create_new(file);
            auto [tran_addr, tran_list] = transaction_list_ptr::create_new(file);
            header->_backups_list = backup_addr;
            header->_transaction_list = tran_addr;
            return warm_file_t{ list, tran_list };
        }

        inline transaction_id_t bucket_index(transaction_id_t tid) const noexcept
        {
            return tid / transactions_per_file_c;
        }

        /** 
        * \return current pair of AppendOnlySkipList for transaction states and backup-records AND acquired RO-lock over `_open_logs_acc` 
        */
        std::pair<warm_file_t&, r_guard_t> get_log(transaction_id_t tid)
        {
            const auto bucket_id = bucket_index(tid);
            typename decltype(_open_logs)::iterator found_pos;
            r_guard_t rguard(_open_logs_acc);
            if (found_pos = _open_logs.find(bucket_id); found != _open_logs.end())
                return { found->second, std::move(rguard) };
            rguard.unlock();
            //acquire wr lock and make double check
            std::unique_lock w_guard(_open_logs_acc);
            if (found_pos = _open_logs.find(bucket_id); found == _open_logs.end())
            {//sure that another thread didn't add this bucket_id
                // there wr-lock grants unique access to `_next_file` as well as `_open_logs`
                auto new_file = (_next_file.valid()) ? _next_file.get() : warm_up_file(); 
                found_pos = _open_logs.try_emplace(found, bucket_id, std::move(new_file));
            }
            //check if new file should be warm-up (at 75%)
            if( ((tid % transactions_per_file_c) >= / (3*transactions_per_file_c / 4))
                && !_next_file.valid() //no back task yet)
            {
                _next_file = _thread_pool.async(&A0lRecovery::warm_up_file, this);
            }

            w_guard.unlock(); 
            return { found_pos->second, r_guard_t rguard(_open_logs_acc) }; //re-aquire as ro lock
        }

        void recover_transaction(SegmentManager& segment_manager, const TransactionProgress& progress, backup_list_t& backup_list)
        {
            // searching only for non-committed transactions
            if (progress._status != TransactionProgress::commiting)
                return;//not interesting, continue iteration
            auto log_file = backup_list.append_log();
            //need recovery - iterate all transaction blocks and restore origin value in segment_manager
            backup_list.indexed_for_each(progress._transaction, [&](const BackupRecord& backup) {
                if (backup._transaction != progress._transaction) // Bloom is not precise, false positive
                    return true; // iterate next
                auto backup_buffer = log_file->at<std::byte>(backup._address_of_backup);
                MemoryChunk mem_chunk = segment_manager.writable_block(
                    backup._origin_addr, backup._buffer_size);
                //
                mem_chunk.byte_copy(backup_buffer, backup._buffer_size);
                return true; //continue backup iteration to apply other blocks
            });

        }

        PersistState _state;
        std::unique_ptr<CreationPolicy> _create_policy;
        OP::utils::ThreadPool& _thread_pool;
        std::future<backup_list_ptr> _next_file;
        std::unordered_map<transaction_id_t, warm_file_t> _open_logs;
        std::shared_mutex _open_logs_acc;
    };

}//ns:OP::vtm

#endif //_OP_VTM_HISTORYRECOVERY__H_
