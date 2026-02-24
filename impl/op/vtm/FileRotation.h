#pragma once
#ifndef _OP_VTM_APPENDONLYLOGFILEROTATION__H_
#define _OP_VTM_APPENDONLYLOGFILEROTATION__H_

#include <memory>
#include <cstdint>
#include <list>

#include <op/common/Utils.h>
#include <op/common/Bitset.h>
#include <op/common/ThreadPool.h>
#include <op/common/ValueGuard.h>
#include <op/common/Exceptions.h>

#include <op/vtm/AppendOnlyLog.h>
#include <op/vtm/AppendOnlySkipList.h>
#include <op/vtm/EventSourcingSegmentManager.h>
#include <op/vtm/ShadowBufferCache.h>


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


} //ns:OP::vtm
#endif //_OP_VTM_APPENDONLYLOGFILEROTATION__H_
