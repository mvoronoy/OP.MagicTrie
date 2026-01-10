#pragma once

#ifndef _OP_VTM_SHADOWBUFFER__H_
#define _OP_VTM_SHADOWBUFFER__H_

#include <cstdint>
#include <memory>

namespace OP::vtm
{
    /** Simple byte buffer allocated on heap. It similar to std::unique_ptr, but allows to create
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
            : _buffer(other._buffer)
            , _owner(other._owner)
            , _size(other._size)
        {
            other._buffer = nullptr;
            other._owner = false;
        }

        constexpr ShadowBuffer(const ShadowBuffer& other) = delete;
        
        ShadowBuffer& operator = (const ShadowBuffer&) = delete;

        ShadowBuffer& operator = (ShadowBuffer&& other) noexcept
        {
            destroy();

            _buffer = other._buffer;
            _owner = other._owner;
            _size = other._size;

            other._buffer = nullptr;
            other._owner = false;

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
/*
namespace std
{
    template<>
    struct std::hash<S>
    {
        std::size_t operator()(const S& s) const noexcept
        {
            std::size_t h1 = std::hash<std::string>{}(s.first_name);
            std::size_t h2 = std::hash<std::string>{}(s.last_name);
            return h1 ^ (h2 << 1); // or use boost::hash_combine
        }
    };
}//ns:std
*/
#endif //_OP_VTM_SHADOWBUFFER__H_
