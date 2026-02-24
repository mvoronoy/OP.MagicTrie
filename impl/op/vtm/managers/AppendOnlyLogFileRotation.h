#pragma once
#ifndef _OP_VTM_APPENDONLYLOGFILEROTATION__H_
#define _OP_VTM_APPENDONLYLOGFILEROTATION__H_

#include <memory>
#include <cstdint>
#include <list>

#include <op/common/Utils.h>
#include <op/common/Bitset.h>
#include <op/common/ThreadPool.h>
#include <op/common/Exceptions.h>

#include <op/vtm/AppendOnlyLog.h>
#include <op/vtm/AppendOnlySkipList.h>
#include <op/vtm/managers/EventSourcingSegmentManager.h>
#include <op/vtm/ShadowBufferCache.h>

#include <op/flur/flur.h>

namespace OP::vtm
{
    struct FileRotationOptions
    {
        std::uint8_t _warm_size = 2;
        std::uint8_t _transactions_per_file = 5;
        std::uint32_t _segment_size = 2u * 1024*1024;
        
        [[maybe_unused]] constexpr FileRotationOptions& transactions_per_file(std::uint8_t v) noexcept
        {
            _transactions_per_file = v;
            return *this;
        }
    };

    struct CreationPolicy
    {
        virtual ~CreationPolicy() = default;

        /** 
        * \return new file or nullptr if file cannot be created (like already exists).
        */ 
        virtual std::shared_ptr<AppendOnlyLog> make_new_file(std::uint64_t) = 0;
        
        virtual std::shared_ptr<AppendOnlyLog> reopen_file(std::uint64_t) = 0;
        
        virtual void utilize_file(std::shared_ptr<AppendOnlyLog> a0log) = 0;

        virtual std::map<std::uint64_t, std::shared_ptr<AppendOnlyLog>> all() = 0;
    };

    struct FileCreationPolicy : public CreationPolicy
    {
        FileCreationPolicy(
            OP::utils::ThreadPool& thread_pool, 
            FileRotationOptions options, 
            std::filesystem::path data_dir, 
            std::string file_prefix,
            std::string file_ext)
            : _thread_pool(thread_pool)
            , _options(std::move(options))
            , _data_dir(std::move(data_dir))
            , _file_prefix(std::move(file_prefix))
            , _file_ext(std::move(file_ext))
        {}

        constexpr static std::array<char, 32> char_map{
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
            'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
            'u', 'v'
        };

        constexpr static size_t lm2map_c = 5; //ln2(char_map.size())
        constexpr static size_t number_char_size_c = (std::numeric_limits<std::uint64_t>::digits + lm2map_c - 1) / lm2map_c;
        
        static std::string u2base32(std::uint64_t n)
        {
            constexpr size_t bitmask = (1 << lm2map_c) - 1;
            std::string vary(number_char_size_c, '0');
            //auto last = --vary.end();
            for(auto i = number_char_size_c; i; --i, n >>= lm2map_c)
                vary[i - 1] = char_map[n & bitmask];
            return vary;
        }

        template <class StringLike>
        static std::optional<std::uint64_t> base32to_u(const StringLike& str) noexcept
        {
            // binary search not the best by performance but simple in use
            // str->int in this case is not time critical
            std::uint64_t n = 0;
            for(auto c: str)
            {
                auto pos = std::lower_bound(char_map.begin(), char_map.end(), c);
                if(pos == char_map.end() || *pos != c)
                    return {};
                n <<= lm2map_c;
                auto delta = std::distance(char_map.begin(), pos);
                n |= delta;
            }
            return n;
        }

        std::shared_ptr<AppendOnlyLog> make_new_file(
            std::uint64_t order) override
        {
            auto file = compose_file_name(order);

            if (std::filesystem::exists(file))
            {
                return nullptr;
            }
            //construct log
            return OP::vtm::AppendOnlyLog::create_new(
                _thread_pool, file, _options._segment_size
            );
        }

        std::shared_ptr<AppendOnlyLog> reopen_file(std::uint64_t order) override
        {
            auto file = compose_file_name(order);

            if (std::filesystem::exists(file))
            {
                return nullptr;
            }
            //construct log
            return OP::vtm::AppendOnlyLog::open(
                _thread_pool, file
            );
        }

        void utilize_file(std::shared_ptr<AppendOnlyLog> a0log) override
        {
            auto file_path = a0log->file_name();
            std::error_code ec;
            if (!std::filesystem::remove(file_path, ec))
            {//some owns the a0log instance yet
            }
        }

        virtual std::map<std::uint64_t, std::shared_ptr<AppendOnlyLog>> all() override
        {
            std::map<std::uint64_t, std::shared_ptr<AppendOnlyLog>> result;
            // Iterate over directory entries
            for (const auto& entry : std::filesystem::directory_iterator(_data_dir)) 
            {
                // Check if the entry is a regular file
                if (!std::filesystem::is_regular_file(entry))
                    continue;
                auto pure_name = entry.path().filename().stem().string();
                // Check if the file extension matches the desired extension
                if (entry.path().extension() == _file_ext 
                    && OP::utils::subview<std::string_view>(pure_name, 0, _file_prefix.size()) == _file_prefix
                    && pure_name.size() == (_file_prefix.size() + number_char_size_c)
                )
                { // matches to pattern
                    //extract id from file-name
                    auto optn = base32to_u(OP::utils::subview<std::string_view>(pure_name, _file_prefix.size()));
                    if(!optn) //bad file name
                        continue;
                    result.emplace(*optn, 
                        OP::vtm::AppendOnlyLog::open(_thread_pool, entry.path()));
                }
            }
            return result;
        }

    private:

        OP::utils::ThreadPool& _thread_pool;
        FileRotationOptions _options;
        std::filesystem::path _data_dir;
        std::string _file_prefix;
        std::string _file_ext;

        std::filesystem::path compose_file_name(std::uint64_t order) const
        {
            return (_data_dir / (_file_prefix + u2base32(order))).replace_extension(_file_ext);
        }
    };

    struct AppendLogFileRotationChangeHistory : MemoryChangeHistory
    {
        using transaction_id_t = typename MemoryChangeHistory::transaction_id_t;
        using RWR = typename MemoryChangeHistory::RWR;
        using pos_t = typename RWR::pos_t;

        enum class FileStatus : std::uint8_t
        {
            /** file empty, but ready to start append */
            warm    = 1,
            /** file has not committed log records */
            active  = 2,
            /** file cannot accept data anymore but may contain non processed log records */
            closed  = 3,
            /** file can be safely deleted or recycled */
            garbage = 4
        };

        static std::unique_ptr<MemoryChangeHistory> create_new(
            OP::utils::ThreadPool& thread_pool, 
            std::unique_ptr<CreationPolicy> create_policy
        )
        {
            std::unique_ptr<AppendLogFileRotationChangeHistory> file_rotation{
                new AppendLogFileRotationChangeHistory(
                    thread_pool, 
                    std::move(create_policy))
            };
            //don't need to guard - since no shared thread are possible there
            file_rotation->_promote_new_file_job =
                thread_pool.async([zhis = file_rotation.get()]() { return zhis->warm_file(); });
            return file_rotation;
        }

        virtual ~AppendLogFileRotationChangeHistory()
        {
            if (std::lock_guard g{ _garbage_collector_job_acc }; _garbage_collector_job.valid())
                _garbage_collector_job.get();
            if (std::lock_guard g{ _promote_new_file_job_acc }; _promote_new_file_job.valid())
                _promote_new_file_job.get();
        }

        OP::utils::ThreadPool& thread_pool() noexcept
        {
            return _thread_pool;
        }

        void on_new_transaction(transaction_id_t transaction_id) override
        {
            w_guard_t rguard(_all_lists_acc);
            if (_all_lists.empty())
            {
                activate_next_file(transaction_id);
            }
            else
            {
                auto& last = _all_lists.back();
                assert(transaction_id > last._transactions_list->last()._transaction_id); //logic assumes that transaction_id grows
                auto in_bunch_index = last._transactions_list->size();
                std::unique_lock guard(_promote_new_file_job_acc);
                if ((in_bunch_index == _options._warm_size)
                    && !_promote_new_file_job.valid())
                {//need proactive job for next file allocation
                    _promote_new_file_job =
                        _thread_pool.async([this]() { return warm_file(); });
                }
                guard.unlock();

                if (in_bunch_index == _options._transactions_per_file)
                {//switch to the new bunch by deactivating previous and activating new
                    activate_next_file(transaction_id);
                }
                else
                { //just extend current transaction list
                    last._transactions_list->emplace(TransactionStore{ transaction_id });
                    //++last._file_header->_active_transactions;
                    last._file_header->_transaction_id_bloom |= bloom_filter_code(transaction_id);
                }
            }
        }

        [[maybe_unused]] ReadIsolation read_isolation(ReadIsolation new_level) override
        {
            return _isolation.exchange(new_level);
        }

        [[nodiscard]] std::optional<ShadowBuffer> buffer_of_region(
            const RWR& search_range, 
            transaction_id_t transaction_id, 
            MemoryRequestType hint, 
            const void* init_data) override
        {
            skip_list_ptr current = get(); //no lock, even if async request incomes it will be ordered/serialized by `get`

            //make buffer
            if (hint < MemoryRequestType::wr) // no need persist memory, just heap
            {
                ShadowBuffer result(new std::uint8_t[search_range.count()], search_range.count(), true);
                if(!populate_ro_buffer(search_range, result, transaction_id, init_data, FarAddress{}, _isolation.load()))
                {
                    return {}; //concurrent lock exception
                }
                return result;
            }
            // create persisted block for write operations
            auto [mem_addr, buffer] = current->append_log()->allocate(search_range.count());
            ShadowBuffer result(buffer, search_range.count(), false); // no need to free, since memory managed by mapped file
            if (hint != MemoryRequestType::wr_no_history)
            {
                if(!populate_ro_buffer(search_range, result, transaction_id,
                    init_data,
                    mem_addr,
                    // for writes need avoid races, so not looking at _isolation and keep always 'Prevent'
                    ReadIsolation::Prevent))
                {
                    return {}; //concurrent lock exception
                }
            }
            current->emplace(BlockProfile{ search_range, transaction_id, mem_addr });
            return result;
        }

        virtual void destroy(transaction_id_t tid, ShadowBuffer buffer) override
        {
            //do nothing
        }

        virtual void on_commit(transaction_id_t tid) override
        {
            complete_transaction(tid);
        }

        virtual void on_rollback(transaction_id_t tid) override
        {
            complete_transaction(tid);
        }

    private:
        struct FileHeader
        {
            constexpr static std::uint32_t signature_value_c = (((std::uint32_t{ 'R' } << 8 | '0') << 8 | 't') << 8) | 'L';
            constexpr static std::uint64_t no_file_c = ~std::uint64_t(0);
            const std::uint32_t _sig = signature_value_c;
            FileStatus _status;
            FarAddress _skip_list;
            FarAddress _transactions_list;
            std::uint64_t _order, _next_file = no_file_c;
            std::uint64_t _transaction_id_bloom = 0; //optimization
            std::uint32_t _active_transactions;

            FileHeader(std::uint64_t order, std::uint32_t max_tran) noexcept
                : _status{ FileStatus::warm }
                , _skip_list{}
                , _order{ order }
                , _active_transactions{ max_tran }
            {
            }
        };


        /** Meta information about shadow buffer */
        struct BlockProfile
        {
            /** position/size of mapped block */
            const RWR _range;
            /** what transaction retains the block */
            const transaction_id_t _used_in_transaction;
            FarAddress _memory_block;
        };

        /** Placeholder of transaction id inside single file */
        struct TransactionStore
        {
            transaction_id_t _transaction_id;
            bool _active = true;
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

            inline void index(const BlockProfile& item_to_index) noexcept
            {
                _index(item_to_index._used_in_transaction);
            }

            inline void index(const TransactionStore& item_to_index) noexcept
            {
                _index(item_to_index._transaction_id);
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

            inline OP::vtm::BucketNavigation check(const BlockProfile& item) const noexcept
            {
                return check(item._used_in_transaction);
            }

            inline OP::vtm::BucketNavigation check(const TransactionStore& item) const noexcept
            {
                return check(item._transaction_id);
            }
        };

        /** Support indexing for skip-list by RWR */
        struct RangeIndexer
        {
            /*[serialized in A0Log]*/ std::uint64_t _bloom_filter = 0;
            static inline constexpr std::uint64_t bloom_calc(const RWR& range) noexcept
            {
                auto bit_width = OP::common::log2(range.count()) + 1;
                return ((1ull << bit_width) - 1)
                    << OP::common::log2(range.pos());
            }
            /**  calculate bloom-filter index of RWR inside BlockProfile */
            inline void index(const BlockProfile& profile) noexcept
            {
                _bloom_filter |= bloom_calc(profile._range);
            }

            /** check if current bucket intersects with specified RWR */
            inline OP::vtm::BucketNavigation check(const BlockProfile& profile) const noexcept
            {
                const auto bc = bloom_calc(profile._range);
                return (_bloom_filter & bc) //can match any bit
                    ? OP::vtm::BucketNavigation::not_sure
                    : OP::vtm::BucketNavigation::next
                    ;
            }
            /* Make check polymorph to query by RWR */
            inline OP::vtm::BucketNavigation check(const RWR& range) const noexcept
            {
                const auto bc = bloom_calc(range);
                return (_bloom_filter & bc) //can match any bit
                    ? OP::vtm::BucketNavigation::not_sure
                    : OP::vtm::BucketNavigation::next
                    ;
            }
        };

        using skip_list_t = AppendOnlySkipList<32, BlockProfile, TransactionIdIndexer, RangeIndexer>;
        using skip_list_ptr = std::shared_ptr<skip_list_t>;
        using a0log_ptr = std::shared_ptr<AppendOnlyLog>;

        using transactions_list_t = AppendOnlySkipList<8, TransactionStore, TransactionIdIndexer>;
        using transactions_list_ptr = std::shared_ptr<transactions_list_t>;
        /** Temporal storage that associates together:
         - mapped file,
         - AppendOnlySkipList for data
         - AppendOnlySkipList transactions used inside this file
         */
        struct ListInFile
        {
            ListInFile(
                skip_list_ptr payload_list,
                transactions_list_ptr transactions_list,
                FileHeader* file_header)
                : _payload_list(payload_list)
                , _transactions_list(transactions_list)
                , _file_header(file_header)
            {

            }
            skip_list_ptr _payload_list;
            transactions_list_ptr _transactions_list;

            FileHeader* _file_header;
        };

        OP::utils::ThreadPool& _thread_pool;
        std::atomic<ReadIsolation> _isolation = ReadIsolation::ReadCommitted;
        FileRotationOptions _options;
        std::unique_ptr<CreationPolicy> _create_policy;

        std::list<ListInFile> _all_lists;
        std::shared_mutex _all_lists_acc;

        std::atomic<std::uint64_t> _file_order = 0;
        std::uint64_t _base_line_order = 0;

        /** Batch index (aka `transaction_id / _options._transactions_per_file`) to job for warm-up file */
        size_t _active_idx = 0;

        std::future<ListInFile> _promote_new_file_job;
        std::mutex _promote_new_file_job_acc;

        std::future<void> _garbage_collector_job;
        std::mutex _garbage_collector_job_acc;

        using w_guard_t = std::unique_lock<std::shared_mutex>;
        using r_guard_t = std::shared_lock<std::shared_mutex>;

        protected:

            AppendLogFileRotationChangeHistory(
                OP::utils::ThreadPool& thread_pool,
                std::unique_ptr<CreationPolicy> create_policy)
                : _thread_pool(thread_pool)
                , _create_policy(std::move(create_policy))
            {
            }

            skip_list_ptr get()
            {
                r_guard_t guard(_all_lists_acc);
                return _all_lists.back()._payload_list;
            }

        ListInFile warm_file()
        {
            std::shared_ptr<AppendOnlyLog> a0l;
            std::uint64_t fidx{};
            do
            {
                fidx = ++_file_order;
                a0l = _create_policy->make_new_file(fidx);
            } while (!a0l);

            auto [_, file_header] = a0l->construct<FileHeader>(fidx, _options._transactions_per_file);

            auto [tran_addr, tran_list] = transactions_list_t::create_new(a0l);
            file_header->_transactions_list = tran_addr;

            auto [list_addr, skip_list] = skip_list_t::create_new(a0l);
            file_header->_skip_list = list_addr;
            return ListInFile(skip_list, tran_list, file_header);
        }

        /**
        * \pre write lock on `_all_lists_acc`
        */
        void activate_next_file(transaction_id_t transaction_id)
        {
            std::unique_lock guard(_promote_new_file_job_acc);
            auto new_slot = _promote_new_file_job.get();
            guard.unlock();

            new_slot._file_header->_status = FileStatus::active;
            new_slot._file_header->_transaction_id_bloom = bloom_filter_code(transaction_id);
            if (!_all_lists.empty())
            {
                auto& prev = _all_lists.back();
                //assert(prev._file_header->_status == FileStatus::active);
                prev._file_header->_status = FileStatus::closed;
                prev._file_header->_next_file = new_slot._file_header->_order;
            }
            new_slot._transactions_list->emplace(TransactionStore{ transaction_id, true }); //append new transaction id immediately
            _all_lists.emplace_back(std::move(new_slot));
        }

        /** iterate memory blocks opened so far that have intersection with `search_range`. For current transaction
        * copy data, for alien transaction follow rules specified by `isolation`.
        */
        bool populate_ro_buffer(
            const RWR& search_range, ShadowBuffer& buffer, transaction_id_t transaction_id,
            const void* init_data, 
            const FarAddress mem_addr,
            ReadIsolation isolation
        )
        {
            if( init_data)
                memcpy(buffer.get(), init_data, search_range.count());
            std::unordered_set<transaction_id_t> ready_to_garbage_transactions;
            std::shared_lock r_guard(_all_lists_acc);//ro control
            for (auto open_file_iter = _all_lists.begin(); open_file_iter != _all_lists.end(); )
            {
                const auto& open_file = *open_file_iter++;
                
                if (FileStatus::warm == open_file._file_header->_status) //achieve non populated file - it is always last in the deque
                    break;

                //clone non-active transaction-id to prevent failed detection them as conflicting
                open_file._transactions_list->for_each([&](const auto& tran_store) {
                    if (!tran_store._active)
                        ready_to_garbage_transactions.emplace(tran_store._transaction_id);
                    });

                if (!OP::utils::any_of<FileStatus::active, FileStatus::closed>(open_file._file_header->_status))
                { //scan other file, as this slot has no active transactions
                    continue; //analyze next file
                }

                //as soon as shared_ptr keeps instance alive, file cannot be deleted, r_guard can be released after it
                auto skplst = open_file._payload_list;

                bool concurrent_lock_error = false;
                open_file._payload_list->indexed_for_each(search_range, [&](const BlockProfile& profile) -> bool {
                    auto joined_zone = OP::zones::join_zones(search_range, profile._range);
                    if (joined_zone.empty())  //no intersection => no race
                        return true; //go next step
                    if (transaction_id == profile._used_in_transaction || isolation == ReadIsolation::ReadUncommitted)
                    {
                        auto offset_in_src = OP::utils::uint_diff_int(joined_zone.pos(), profile._range.pos());
                        auto offset_in_dest = OP::utils::uint_diff_int(joined_zone.pos(), search_range.pos());
                        const std::uint8_t* memory = skplst->append_log()->at<std::uint8_t>(profile._memory_block);
                        memcpy(
                            buffer.get() + offset_in_dest,
                            memory + offset_in_src,
                            joined_zone.count()
                        );
                    }
                    else if (isolation == ReadIsolation::Prevent)
                    {   // block intersects with other transaction, and isolation level doesn't allow conflict

                        //need check if this transaction is ever active
                        if (auto found = ready_to_garbage_transactions.find(profile._used_in_transaction);
                            found == ready_to_garbage_transactions.end())
                        { // not in cache of garbage, hence transaction is active
                            concurrent_lock_error = true;
                            return false; //stop iteration
                        }
                    }
                    // ignore all other isolations
                    return true; //continue iteration
                    });
                if (concurrent_lock_error)
                    return false;
            }
            return true; //all good
        }

        void complete_transaction(transaction_id_t transaction_id)
        {
            //make copy of file headers to quickly unlock mutex
            std::vector<ListInFile> control_copy;
            w_guard_t guard(_all_lists_acc);
            control_copy = evict<false/*just copy*/>(
                [transaction_id](const auto& file_def) {
                    //copy only on active or closed files
                    return OP::utils::any_of<FileStatus::active, FileStatus::closed>(file_def._file_header->_status)
                        //also allow bloom-filter to check if transaction present in this file
                        && (transaction_id == (file_def._file_header->_transaction_id_bloom & transaction_id))
                        ;
                });
            guard.unlock();

            bool need_start_gc = false;
            for(auto& file_def: control_copy)
            {
                //iterate all transaction lists in single file and mark as disabled
                file_def._transactions_list->indexed_for_each(transaction_id, [&]( TransactionStore& value) {
                    if (value._transaction_id != transaction_id) //Bloom filter unfortunately not precise
                        return true; //continue scan
                    if (!value._active) //already closed, go next file
                        return false;//stop this file scan
                    
                    assert(file_def._file_header->_active_transactions > 0);
                    value._active = false;
                    --file_def._file_header->_active_transactions;
                    return false; //stop iteration
                    });
                if (file_def._file_header->_active_transactions == 0)
                { // no more open transactions in this file, can be safely garbage collected
                    file_def._file_header->_status = FileStatus::garbage;
                    need_start_gc = true;
                }
            }
            if (need_start_gc)
                start_gc();
        }


        void start_gc()
        {
            std::unique_lock guard(_garbage_collector_job_acc);
            _garbage_collector_job = _thread_pool.async(
                [this, previous = std::move(_garbage_collector_job)]() mutable -> void {
                    if (previous.valid()) //wait till previous complete
                        previous.get();
                    w_guard_t lst_guard(_all_lists_acc);
                    auto control_copy = evict<true/*move out items*/>([](const auto& file_def) {
                        return FileStatus::garbage == file_def._file_header->_status;
                        });
                    lst_guard.unlock();
                    //now can destroy file by file
                    for (auto& file_def : control_copy) {
                        std::shared_ptr<AppendOnlyLog> a0log = file_def._payload_list->append_log();
                        file_def._payload_list.reset();
                        file_def._transactions_list.reset();
                        _create_policy->utilize_file(a0log);
                    }
                }
            );
        }

        /** make copy or move out items from `_all_lists` basing on predicate 
        * \pre _all_lists_acc - must be write locked
        */ 
        template <bool remove_evicted, class Predicate>
        std::vector<ListInFile> evict(Predicate predicate)
        {
            std::vector<ListInFile> result;
            
            for (auto i = _all_lists.begin(); i != _all_lists.end(); )
            {//move with erase
                if (predicate(*i))
                {
                    if constexpr (remove_evicted)
                    {
                        result.emplace_back(std::move(*i));
                        i = _all_lists.erase(i);
                    }
                    else
                    {
                        result.emplace_back(*i);
                        ++i;
                    }
                }
                else 
                    ++i;
            }
            return result;
        }

    };

} //ns:OP::vtm
#endif //_OP_VTM_APPENDONLYLOGFILEROTATION__H_
