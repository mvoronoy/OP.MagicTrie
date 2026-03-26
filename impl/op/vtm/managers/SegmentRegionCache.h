#pragma once
#ifndef _OP_VTM_SEGMENTREGIONCACHE__H_
#define _OP_VTM_SEGMENTREGIONCACHE__H_

#include <mutex> 
#include <shared_mutex>
#include <cassert>
#include <optional>
#include <vector>
#include <op/vtm/managers/SegmentRegion.h>

namespace OP::vtm
{
    /**Simple thread-safe read-optimized storage of elements indexed in range [0...).*/
    struct SegmentRegionCache
    {
        using reference_t = SegmentRegion&;
        using region_entry_t = std::unique_ptr<SegmentRegion>;

        explicit SegmentRegionCache(size_t capacity)
            : _instance_uid(_instance_uid_generator.fetch_add(1))
        {
            _chunked_data.resize(capacity);
        }

        ~SegmentRegionCache()
        {
            get_last_accessed() = {};
        }

        void put(size_t pos, region_entry_t value)
        {
            std::unique_lock guard(_chunked_data_acc);
            ensure(pos);
            _chunked_data[pos] = std::move(value);
        }

        template <class Factory>
        reference_t get(size_t pos, Factory&& factory)
        {
            auto& last_accessed = get_last_accessed();
            if (last_accessed._last_region != nullptr
                && last_accessed._last_chunk_index == pos
                && last_accessed._this_uid == _instance_uid
                )
            {
                return *last_accessed._last_region;
            }

            if (std::shared_lock guard(_chunked_data_acc); pos < _chunked_data.size())
            { //control block to demarcate scope of guard
                auto& opt_data = _chunked_data.at(pos);
                if (opt_data)
                {
                    return update_local_cache(opt_data, pos);
                }
                // proceed with pessimistic scenario
            }

            //need doublecheck presence
            std::unique_lock guard(_chunked_data_acc);
            ensure(pos);
            //double check
            auto& opt_data = _chunked_data.at(pos);
            if (!opt_data)
                opt_data = factory(pos);
            return update_local_cache(opt_data, pos);
        }

        template <class FCallback>
        void for_each(FCallback f)
        {
            std::shared_lock guard(_chunked_data_acc);
            for (auto& ref : _chunked_data)
            {
                if (ref != nullptr)
                    f(*ref);
            }
        }

    private:
        const size_t _instance_uid;
        std::vector<region_entry_t> _chunked_data;
        std::shared_mutex _chunked_data_acc;

        struct LastAccessed
        {
            size_t _last_chunk_index = 0;
            size_t _this_uid = 0;
            SegmentRegion* _last_region = nullptr;
        };

        inline static LastAccessed& get_last_accessed() noexcept
        {
            static thread_local LastAccessed last_accessed = {};
            return last_accessed;
        }

        static inline std::atomic<size_t> _instance_uid_generator = 1;

        /**
        *   \pre _chunked_data_acc is acquired
        */
        void ensure(size_t pos)
        {
            if (pos >= _chunked_data.size())
                _chunked_data.resize(pos + 1);
        }

        /**
        *   \pre _chunked_data_acc is acquired
        */
        reference_t update_local_cache(region_entry_t& entry, size_t pos)
        {
            //following is safe since _last_accessed is thread-local
            auto& last_accessed = get_last_accessed();
            last_accessed._last_region = entry.get();
            last_accessed._last_chunk_index = pos;
            last_accessed._this_uid = _instance_uid;
            return *last_accessed._last_region;
        }
    };

}//ns: OP::vtm

#endif //_OP_VTM_SEGMENTREGIONCACHE__H_
