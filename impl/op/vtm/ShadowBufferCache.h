#pragma once
#ifndef _OP_VTM_SHADOWBUFFERCACHE__H_
#define _OP_VTM_SHADOWBUFFERCACHE__H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

#include <op/vtm/ShadowBuffer.h>

namespace OP::vtm
{
    /** Cache to reuse ShadowBuffers to reduce number of heap allocations */
    struct ShadowBufferCache
    {
        ShadowBufferCache() 
            : _cache{ 31, BufferSizeHash{}, ShadowBufferCache::buffer_size_eq }
        {
        }

        /** Taking buffer size either take it from cache or allocate new */
        ShadowBuffer get(segment_pos_t size)
        {
            ShadowBuffer search_by{ nullptr, size, false };//just for search purpose
            std::unique_lock guard(_cache_acc);
            auto found = _cache.extract(search_by); //work with node instead of entry
            if( found.empty() )
            {
                guard.unlock();
                return ShadowBuffer{new std::uint8_t[size], size, true};
            }
            //
            ShadowBuffer result{ std::move(found.value()) };
            return result;
        }

        void utilize(ShadowBuffer deleting)
        {
            if( !deleting.is_owner() )
                return;
            std::unique_lock guard(_cache_acc);
            _cache.emplace(std::move(deleting));
        }

    private:

        static bool buffer_size_eq(const ShadowBuffer& left, const ShadowBuffer& right) noexcept
        {
            return left.size() == right.size();
        }

        struct BufferSizeHash
        {
            std::hash<size_t> _h;
            size_t operator()(const ShadowBuffer& buffer)const noexcept
            {
                return _h(buffer.size());
            }
        };

        using cache_t = std::unordered_multiset<
            ShadowBuffer, BufferSizeHash, bool(*)(const ShadowBuffer&, const ShadowBuffer&)>;
        cache_t _cache;
        mutable std::shared_mutex _cache_acc;
    };
} //ns::OP::vtm

#endif //_OP_VTM_SHADOWBUFFERCACHE__H_
