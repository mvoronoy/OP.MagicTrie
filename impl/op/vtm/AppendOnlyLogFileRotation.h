#pragma once
#ifndef _OP_VTM_APPENDONLYLOGFILEROTATION__H_
#define _OP_VTM_APPENDONLYLOGFILEROTATION__H_

#include <memory>
#include <cstdint>
#include <list>

#include <op/common/ThreadPool.h>
#include <op/common/ValueGuard.h>
#include <op/common/Exceptions.h>

#include <op/vtm/AppendOnlyLog.h>
#include <op/vtm/AppendOnlySkipList.h>

namespace OP::vtm
{
    struct FileRotationOptions
    {
        std::uint8_t _warm_size = 2;
        std::uint8_t _transactions_per_file = 5;
        std::uint32_t _segment_size = 2u * 1024*1024;
    };

    struct CreationPolicy
    {
        virtual ~CreationPolicy() = default;

        /** 
        * \return new file or nullptr if file cannot be created (like already exists).
        */ 
        virtual std::shared_ptr<AppendOnlyLog> make_new_file(std::uint64_t) = 0;
        
        virtual void utilize_file(std::shared_ptr<AppendOnlyLog> a0log) = 0;

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
        
        static std::string u2base32(std::uint64_t n)
        {
            constexpr size_t lm2map = 5;
            constexpr size_t char_size = 
                (std::numeric_limits<decltype(n)>::digits + lm2map - 1) / lm2map; 
            constexpr size_t bitmask = (1 << lm2map) - 1;
            std::string vary(char_size, '0');
            //auto last = --vary.end();
            for(auto i = char_size; i; --i, n >>= lm2map)
                vary[i - 1] = char_map[n & bitmask];
            return vary;
        }

        std::shared_ptr<AppendOnlyLog> make_new_file(
            std::uint64_t order) override
        {
            auto file = (_data_dir / (_file_prefix + u2base32(order))).replace_extension(_file_ext);

            if (std::filesystem::exists(file))
            {
                return nullptr;
            }
            //construct log
            return OP::vtm::AppendOnlyLog::create_new(
                _thread_pool, file, _options._segment_size
            );
        }

        void utilize_file(std::shared_ptr<AppendOnlyLog> a0log) override
        {
            auto file_path = a0log->file_name();
            a0log = nullptr;
            std::filesystem::remove(file_path); //exception means that some owns the a0log instance yet
        }

    private:
        OP::utils::ThreadPool& _thread_pool;
        FileRotationOptions _options;
        std::filesystem::path _data_dir;
        std::string _file_prefix;
        std::string _file_ext;
        
    };

    template <class TList>
    struct AppendLogFileRotation
    {

        enum class FileStatus : std::uint8_t
        {
            /** file empty, but ready to start append */
            warm    = 1,
            /** file has non processed log records */
            active  = 2,
            /** file cannot accept data anymore but may contain non processed log records */
            closed  = 3,
            /** file can be safely deleted or recycled */
            garbage = 4
        };

        using element_t = typename TList::element_t;
        using skip_list_t = TList;
        using skip_list_ptr = std::shared_ptr<TList>;
        using a0log_ptr = std::shared_ptr<AppendOnlyLog>;

        using w_guard_t = std::unique_lock<std::shared_mutex>;
        using r_guard_t = std::shared_lock<std::shared_mutex>;

        static std::shared_ptr<AppendLogFileRotation> create_new(
            OP::utils::ThreadPool& thread_pool, 
            std::unique_ptr<CreationPolicy> create_policy
        )
        {
            std::shared_ptr<AppendLogFileRotation> file_rotation{
                new AppendLogFileRotation(
                    thread_pool, 
                    std::move(create_policy))
            };
            //auto list_in_file = file_rotation->warm_file();
            //list_in_file._file_header->_status = FileStatus::active; //activate immediatly
            //file_rotation->_all_lists.emplace_back(list_in_file);
            file_rotation->_active_job =
                thread_pool.async([file_rotation]() { return file_rotation->warm_file(); });
            return file_rotation;
        }

        void on_new_transaction(std::uint64_t transaction_id)
        {
            w_guard_t rguard(_all_lists_acc);
            if (_all_lists.empty())
            {
                activate_next();
            }
            else
            {
                assert(_base_line_order <= transaction_id); //test obsolete transaction

                transaction_id -= _base_line_order;
                auto in_bunch_index = transaction_id % _options._transactions_per_file;
                auto bunch_index = transaction_id / _options._transactions_per_file;
                assert(bunch_index < _all_lists.size());
                if ((in_bunch_index == _options._transactions_per_file / 2)
                    && !_active_job.valid())
                {//need proactive job for next file allocation
                    _active_job =
                        _thread_pool.async([this]() { return warm_file(); });
                }
                else if (in_bunch_index == 0)
                {//switch to the new bunch by deactivating previous and activating new
                    activate_next();
                }
            }
        }

        auto append(std::uint64_t transaction_id, const element_t& item)
        {
        }

    protected:

        AppendLogFileRotation(
            OP::utils::ThreadPool& thread_pool, 
            std::unique_ptr<CreationPolicy> create_policy)
            : _thread_pool(thread_pool)
            , _create_policy(std::move(create_policy))
        {
        }

        skip_list_ptr get()
        {
            r_guard_t guard(_all_lists_acc);
            return _all_lists[_active_idx]._list;
        }

    private:
        /** allows a little improve scan on transaction-id inside single file */
        struct TransactionIdIndexer
        {
            /*[serialized in A0Log]*/ std::uint64_t _bloom_filter = 0; 
            /*[serialized in A0Log]*/ std::uint64_t _min = std::numeric_limits<std::uint64_t>::max(); 
            /*[serialized in A0Log]*/ std::uint64_t _max = 0; 
            
            /** For a Bloom filter dealing with sequentially growing std::uint64_t keys, the main concern 
            *   is ensuring the hash function distributes these non-random inputs uniformly across the bit array. 
            */
            static std::uint64_t hash(std::uint64_t item_to_index) noexcept
            {
                return item_to_index * 0x5fe14bf901200001ull; //(0x60ff8010405001) //good spreading of sequential bits for average case
            }

            inline void index(std::uint64_t item_to_index) noexcept
            {
                _bloom_filter |= hash(item_to_index);
                if(item_to_index < _min)
                    _min = item_to_index;
                if(_max < item_to_index)
                    _max = item_to_index;
            }

            inline OP::vtm::BucketNavigation check(std::uint64_t item) const noexcept
            {
                const auto hc = hash(item);
                return item >= _min 
                        && item <= _max 
                        && (_bloom_filter & hc) == hc //must match all bits
                            ? OP::vtm::BucketNavigation::not_sure
                            : OP::vtm::BucketNavigation::next
                    ;
            }
        };

        using transactions_list_t = AppendOnlySkipList<32, std::uint64_t, TransactionIdIndexer>;
        using transactions_list_ptr = std::shared_ptr<transactions_list_t>;

        struct FileHeader
        {
            constexpr static std::uint32_t signature_value_c = (((std::uint32_t{ 'R' } << 8 | '0') << 8 | 't') << 8) | 'L';
            std::uint32_t _sig = signature_value_c;
            FileStatus _status;
            FarAddress _skip_list;
            FarAddress _transactions_list;
            std::uint64_t _order;
            std::uint8_t _warm_size;

            FileHeader(std::uint64_t order, std::uint8_t warm_size) noexcept
                : _status{FileStatus::warm}
                , _skip_list{}
                , _order{order}
                , _warm_size{warm_size}
            {}
        };

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

        ListInFile warm_file()
        {
            std::shared_ptr<AppendOnlyLog> a0l;
            std::uint64_t fidx{};
            do
            {
                fidx = ++_file_order;
                a0l = _create_policy->make_new_file(fidx);
            } while (!a0l);

            auto [_, file_header] = a0l->construct<FileHeader>(fidx, _options._warm_size);

            auto [tran_addr, tran_list] = transactions_list_t::create_new(a0l);
            file_header->_transactions_list = tran_addr;
            
            auto [list_addr, skip_list] = skip_list_t::create_new(a0l);
            file_header->_skip_list = list_addr;
            return ListInFile(skip_list, tran_list, file_header);
        }

        /**
        * \pre write lock on `_all_lists_acc`
        */
        void activate_next()
        {
            if (!_all_lists.empty())
            {
                auto& prev = _all_lists[_active_idx];
                assert(prev._file_header->_status == FileStatus::active);
                prev._file_header->_status = FileStatus::closed;
            }
            auto new_slot = _active_job.get();
            _active_idx = _all_lists.size();
            new_slot._file_header->_status = FileStatus::active;
            _all_lists.emplace_back(std::move(new_slot));
        }

        template <class FCallback>
        void scan_files(FCallback& action)
        {                                                                                          
            namespace fs = std::filesystem;
            if (fs::exists(root) && fs::is_directory(root))
            {

                for (auto const& dir_entry : std::filesystem::directory_iterator{_data_dir})
                {
                    if (dir_entry.is_regular_file() 
                        && entry.path().extension() == _file_ext
                        && OP::utils::subview(entry.path().filename().string(), 0, _file_prefix.size()) == _file_prefix
                        )
                    {
                        paths.emplace_back(entry.path().filename());
                    }
                }
            }

            return paths;
        } 

        OP::utils::ThreadPool& _thread_pool;
        FileRotationOptions _options;
        std::unique_ptr<CreationPolicy> _create_policy;

        std::deque<ListInFile> _all_lists;
        std::shared_mutex _all_lists_acc;

        std::atomic<std::uint64_t> _file_order = 0;
        std::uint64_t _base_line_order = 0;
        
        /** Batch index (aka `transaction_id / _options._transactions_per_file`) to job for warm-up file */
        size_t _active_idx = 0;
        
        std::future<ListInFile> _active_job;
    };

} //ns:OP::vtm
#endif //_OP_VTM_APPENDONLYLOGFILEROTATION__H_
