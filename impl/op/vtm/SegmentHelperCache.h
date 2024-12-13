#pragma once
#ifndef _OP_VTM_SEGMENTHELPERCACHE__H_
#define _OP_VTM_SEGMENTHELPERCACHE__H_

#include <mutex> 
#include <shared_mutex>
#include <cassert>
#include <optional>
#include <vector>

namespace OP::vtm
{
    /**Simple thread-safe read-optimized storage of elements indexed in range [0...).*/
    template <class T, class K = size_t>
    struct SegmentHelperCache
    {
        using reference_t = T&;
        using index_t = K;

        explicit SegmentHelperCache(index_t capacity)
        {
            _data.resize(capacity);
        }

        void put(index_t pos, T&& value)
        {
            std::unique_lock guard(_lock);
            ensure(pos);
            _data[pos].emplace(std::move(value));
        }

        /**
        *@throws std::out_of_range
        */
        reference_t get(index_t pos) const
        {
            std::shared_lock guard(_lock);
            return *_data.at(pos); //raises std::out_of_range 
        }

        template <class Factory>
        reference_t get(index_t pos, Factory factory)
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

    private:
        
        using container_t = std::optional<T>;

        std::vector<container_t> _data;
        mutable std::shared_mutex _lock;

        void ensure(index_t pos)
        {
            if (pos >= _data.size())
                _data.resize(pos + 1);
        }
    };

}//ns: OP::vtm

#endif //_OP_VTM_SEGMENTHELPERCACHE__H_
