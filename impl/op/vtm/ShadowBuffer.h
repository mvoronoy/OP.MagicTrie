#pragma once

#ifndef _OP_VTM_SHADOWBUFFER__H_
#define _OP_VTM_SHADOWBUFFER__H_

#include <cstdint>
#include <memory>
#include <utility>

namespace OP::vtm
{
    namespace details
    {
        template <class T, class U>
        constexpr inline T exchange(T& obj, U&& new_value) noexcept
        {
#ifdef OP_CPP20_FEATURES
            return std::exchange(obj, std::forward<U>(new_value));
#else
            T old_value = std::move(obj);
            obj = std::forward<U>(new_value);
            return old_value;
#endif
        }

    }
    /** Simple byte buffer. It is similar to std::unique_ptr, but allows to create
    * non-owning 'ghost'. Used as element for #ShadowBufferCache container to allow quick reuse memory
    * using size as a key.  
    */
    struct ShadowBuffer
    {
        constexpr ShadowBuffer(std::uint8_t* buffer, size_t size, bool owns) noexcept
            : _buffer(buffer)
            , _owner(owns)
            , _size(size)
        {
        }

        constexpr ShadowBuffer() noexcept
            : _buffer(nullptr)
            , _owner(false)
            , _size(0)
        {
        }

        constexpr ShadowBuffer(ShadowBuffer&& other) noexcept
            : _buffer(details::exchange(other._buffer, nullptr))
            , _owner(details::exchange(other._owner, false))
            , _size(details::exchange(other._size, 0))
        {
        }

        constexpr ShadowBuffer(const ShadowBuffer& other) = delete;
        ShadowBuffer& operator = (const ShadowBuffer&) = delete;

        ShadowBuffer& operator = (ShadowBuffer&& other) noexcept
        {
            destroy();

            _buffer = details::exchange(other._buffer, nullptr);
            _owner = details::exchange(other._owner, false);
            _size = details::exchange(other._size, 0);

            return *this;
        }

        ~ShadowBuffer()
        {
            destroy();
        }

        constexpr operator bool() const noexcept
        {
            return _buffer;
        }

        constexpr bool operator !() const noexcept
        {
            return !_buffer;
        }

        constexpr bool operator ==(std::nullptr_t ) const noexcept
        {
            return !_buffer;
        }

        constexpr bool operator !=(std::nullptr_t npt) const noexcept
        {
            return _buffer != npt;
        }

        constexpr size_t size() const noexcept
        {
            return _size;
        }

        std::uint8_t* get() const noexcept
        {
            assert(_buffer);
            return _buffer;
        }

        ShadowBuffer ghost() const noexcept
        {
            return ShadowBuffer{_buffer, _size, false};
        }

        constexpr bool is_owner() const noexcept
        {
            return _owner;
        }

    private:

        void destroy()
        {
            if(_owner && _buffer)
            {
                _owner = false;
                delete []_buffer;
                _buffer = nullptr;
            }
        }

        std::uint8_t* _buffer;
        bool _owner;
        size_t _size;
    };
        
    
} //ns::OP::vtm

#endif //_OP_VTM_SHADOWBUFFER__H_
