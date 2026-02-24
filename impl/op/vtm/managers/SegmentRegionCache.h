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

        explicit SegmentRegionCache(size_t capacity)
        {
            _data.resize(capacity);
        }

        void put(size_t pos, SegmentRegion&& value)
        {
            std::unique_lock guard(_lock);
            ensure(pos);
            _data[pos].emplace(std::move(value));
        }

        template <class Factory>
        reference_t get(size_t pos, Factory factory)
        {
            if(std::shared_lock guard(_lock); pos <_data.size())
            { //control block to demarcate scope of guard
                auto& opt_data = _data.at(pos);
                if (opt_data)
                {
                    return *opt_data;
                }
            }
            //need doublecheck presence
            std::unique_lock guard(_lock);
            ensure(pos);
            //double check
            auto& opt_data = _data.at(pos);
            if (!opt_data)
                opt_data.emplace(factory(pos));
            return *opt_data;
        }

        template <class FCallback>
        void for_each(FCallback f)
        {
            std::shared_lock guard(_lock);
            for(container_t& ref: _data)
            {
                if(ref.has_value())
                    f(*ref);
            }

        }

    private:
        
        using container_t = std::optional<SegmentRegion>;

        std::vector<container_t> _data;
        std::shared_mutex _lock;

        void ensure(size_t pos)
        {
            if (pos >= _data.size())
                _data.resize(pos + 1);
        }
    };

}//ns: OP::vtm

#endif //_OP_VTM_SEGMENTREGIONCACHE__H_
