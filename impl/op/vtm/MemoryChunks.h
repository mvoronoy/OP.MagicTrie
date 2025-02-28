#pragma once
#ifndef _OP_VTM_MEMORYCHUNKS__H_
#define _OP_VTM_MEMORYCHUNKS__H_

#include <cstdint>
#include <string.h>
#include <memory>

#include <op/vtm/typedefs.h>
#include <op/vtm/ShadowBuffer.h>

namespace OP::vtm
{
    struct MemoryChunkBase;

    struct BlockDisposer
    {
        virtual ~BlockDisposer() OP_NOEXCEPT = default;
        virtual void on_leave_scope(MemoryChunkBase& closing) OP_NOEXCEPT = 0;
    };

    struct MemoryChunkBase 
    {

        MemoryChunkBase() noexcept = default;

        MemoryChunkBase(
            segment_pos_t pos_offset,
            ShadowBuffer buffer,
            segment_pos_t count, 
            FarAddress address) OP_NOEXCEPT
            : _pos_offset(pos_offset)
            , _buffer(std::move(buffer))
            , _count(count)
            , _address(std::move(address))
        {
        }

        MemoryChunkBase(MemoryChunkBase&& right) OP_NOEXCEPT
            : _pos_offset(right._pos_offset)
            , _buffer(std::move(right._buffer))
            , _count(right._count)
            , _address(std::move(right._address))
            , _disposer(std::move(right._disposer))
        {
            right._count = 0;
            right._pos_offset = 0;
        }
            
        MemoryChunkBase& operator = (MemoryChunkBase&& right) OP_NOEXCEPT
        {
            _pos_offset = std::move(right._pos_offset);
            right._pos_offset = 0;
            _buffer = std::move(right._buffer);
            _count = std::move(right._count);
            right._count = 0;
            _address = std::move(right._address);
            _disposer = std::move(right._disposer);
            return *this;
        }

        MemoryChunkBase(const MemoryChunkBase&) = delete;
        MemoryChunkBase& operator = (const MemoryChunkBase&) = delete;

        ~MemoryChunkBase() OP_NOEXCEPT
        {
            if (_disposer)
                _disposer->on_leave_scope(*this);
        }

        FarAddress address() const noexcept
        {
            return _address;
        }
            
        using disposable_ptr_t = std::unique_ptr<BlockDisposer>;

        void emplace_disposable(disposable_ptr_t d)
        {
            _disposer = std::move(d);
        }

        std::uint8_t* pos() const
        {
            return _buffer.get() + _pos_offset;
        }
        
        segment_pos_t count() const
        {
            return _count;
        }

        segment_pos_t pos_offset() const
        {
            return _pos_offset;
        }

    protected:
        const ShadowBuffer& memory() const
        {
            return _buffer;
        }
            
    private:
        ShadowBuffer _buffer = {};
        segment_pos_t _count = 0;
        segment_pos_t _pos_offset = 0;
        disposable_ptr_t _disposer = {};
        FarAddress _address = {};
    };
    /**
    *   Pointer in virtual memory and it size
    */
    struct MemoryChunk : public MemoryChunkBase
    {

        MemoryChunk(ShadowBuffer buffer, segment_pos_t count, FarAddress address) :
            MemoryChunkBase(0, std::move(buffer), count, std::move(address))
        {
        }

        MemoryChunk(segment_pos_t pos_offset, ShadowBuffer buffer, segment_pos_t count, FarAddress address) :
            MemoryChunkBase(pos_offset, std::move(buffer), count, std::move(address))
        {
        }

        MemoryChunk(MemoryChunk&& right) OP_NOEXCEPT
            :MemoryChunkBase(std::move(right))
        {
        }

        //! For MSVC-2013 it is very important to keep copy-constructor after(!) move constructor
        MemoryChunk(const MemoryChunk&) = delete;
        MemoryChunk& operator = (const MemoryChunk&) = delete;
        MemoryChunk& operator = (MemoryChunk&& right) OP_NOEXCEPT
        {
            MemoryChunkBase::operator=(std::move(right));
            return *this;
        }
        
        MemoryChunk() = default;
        
        /**
        * @return new instance that is subset of this where beginning is shifted on `offset` bytes
        *   @param offset - how many bytes to offset from beginning, must be less than #count()
        */
        //MemoryChunk subset(segment_pos_t offset) const
        //{
        //    assert(offset < count());
        //    return MemoryChunk(pos_offset() + offset, memory(), count() - offset, address() + offset);
        //}

        /**
        *   @param idx - byte offset (not an item index)
        */
        template <class T>
        T* at(segment_pos_t idx) const
        {
            assert(idx < this->count());
            return reinterpret_cast<T*>(pos() + idx);
        }
        /**Copy buffer 
        * @param source source bytes
        * @param source_size number of bytes to copy
        * @param dest_offset bytes to offset in this block
        * @return number of bytes copied
        * @throws std::out_of_range exception if `source_size` exceeds this block size
        */
        segment_pos_t byte_copy(const void * source, segment_pos_t source_size, segment_pos_t dest_offset = 0)
        {
            if((this->count() - dest_offset) < source_size)
                throw std::out_of_range("source size is too big");
            memcpy(pos() + dest_offset, source, source_size);
            return source_size;
        }
            
    };


    /**Helper that provides writable access to virtual memory occupied by single instance or array of 
    * aligned instances of `T`
    * \tparam T - "standard layout types" in sense of `std::is_standard_layout_v<T> == true`
    */
    template <class T>
    struct WritableAccess : public MemoryChunk
    {
        static_assert(std::is_standard_layout_v<T>, "T must have standard layout, see http://www.stroustrup.com/C++11FAQ.html#PODs");

        WritableAccess(MemoryChunk&& right) noexcept
            : MemoryChunk(std::move(right))
        {
        }
        WritableAccess(WritableAccess<T>&& right) noexcept
            : MemoryChunk(std::move(right))
        {
        }

        WritableAccess& operator = (WritableAccess&& right) noexcept
        {
            MemoryChunk::operator=(std::move(right));
            return *this;
        }

        operator T* () 
        {
            return MemoryChunk::at<T>(0);
        }

        T* operator & () 
        {
            return MemoryChunk::at<T>(0);
        }

        T& operator *()
        {
            return *MemoryChunk::at<T>(0);
        }
        
        T* operator -> ()
        {
            return MemoryChunk::at<T>(0);
        }
        
        const T* operator -> () const
        {
            return MemoryChunk::at<T>(0);
        }
        /**
        * @return reference to array element reside by specified index
        * @throws std::out_of_range if index address invalid element
        */
        T& operator[](segment_pos_t index)
        {
            auto byte_offset = OP::utils::memory_requirement<T>::array_size(index);
            if (byte_offset >= this->count())
                throw std::out_of_range("index is out of range");
            return *MemoryChunk::at<T>(byte_offset);
        }

        const T& operator[](segment_pos_t index) const
        {
            auto byte_offset = OP::utils::memory_requirement<T>::array_size(index);
            if (byte_offset >= this->count())
                throw std::out_of_range("index is out of range");
            return *MemoryChunk::at<T>(byte_offset);
        }
        
        /** In-place construct type T from constructor arguments `Ts...` */
        template <class ... Ts>
        void make_new(Ts&&... args)
        {
            if ( this->count() < OP::utils::memory_requirement<T>::requirement )
                throw std::out_of_range("too small block to allocate T");
            new (pos()) T(std::forward<Ts>(args)...);
        }
    };

    /**Hint allows specify how writable block will be used*/
    enum class WritableBlockHint : std::uint8_t
    {
        block_no_hint_c = 0,
        block_for_read_c = 0x1,
        block_for_write_c = 0x2,
        force_optimistic_write_c = 0x4,
        allow_block_realloc = 0x8,
        /**Block already contains some information and will be used for r/w operations*/
        update_c = block_for_read_c | block_for_write_c,

        /**Block is used only for write purpose and doesn't contain useful information yet*/
        new_c = block_for_write_c
    };
    constexpr inline WritableBlockHint operator & (WritableBlockHint left, WritableBlockHint right) noexcept
    {
        return static_cast<WritableBlockHint>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }
    constexpr inline WritableBlockHint operator | (WritableBlockHint left, WritableBlockHint right) noexcept
    {
        return static_cast<WritableBlockHint>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    constexpr inline WritableBlockHint operator ~ (WritableBlockHint hint) noexcept
    {
        return static_cast<WritableBlockHint>( ~static_cast<std::uint8_t>(hint) );
    }
    /**
    *   Create writable accessor to some virtual memory
    * \tparam T some type that resides at accesed writable memory
    * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentToplogy)
    * \return accessor that provide writable access to the instance of `T`
    */
    template <class T, class SMProvider>
    WritableAccess<T> accessor(SMProvider& segment_manager_provider, FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
    {
        return WritableAccess<T>(
            std::move(
                resolve_segment_manager(segment_manager_provider)
                .writable_block(pos, OP::utils::memory_requirement<T>::requirement, hint)
            )
        );
    }
        
    /**
    *   Create read/write accessor to virtual memory occupied by array of `T` elements of length `number_elements`
    * \tparam T some type that resides at accesed readonly memory
    * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentTopology)
    * \param number_elements number of items in accessed array
    * \return accessor that provide readonly (const) access to array of `T`s
    */
    template <class T, class SMProvider>
    WritableAccess<T> array_accessor(
        SMProvider& segment_manager_provider, 
        FarAddress pos,
        segment_pos_t number_elements,
        WritableBlockHint hint = WritableBlockHint::update_c)
    {
        using A = WritableAccess<T>;
            
        const auto size = OP::utils::memory_requirement<T>::array_size( number_elements );
        return A(std::move(
            resolve_segment_manager(segment_manager_provider)
            .writable_block(pos, size, hint)
            )
        );
    }

    /**Hint allows specify how readonly blocks will be used*/
    enum class ReadonlyBlockHint : std::uint8_t
    {
        ro_no_hint_c = 0,
        /**Forces keep lock even after ReadonlyMemoryChunk is released. While the default behaviour releases lock */
        ro_keep_lock = 0x1
    };
    
    constexpr inline ReadonlyBlockHint operator & (ReadonlyBlockHint left, ReadonlyBlockHint right)
    {
        return static_cast<ReadonlyBlockHint>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    constexpr inline ReadonlyBlockHint operator | (ReadonlyBlockHint left, ReadonlyBlockHint right)
    {
        return static_cast<ReadonlyBlockHint>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    
    constexpr inline ReadonlyBlockHint operator ~ (ReadonlyBlockHint hint)
    {
        return static_cast<ReadonlyBlockHint>( ~static_cast<std::uint8_t>(hint) );
    }
        
    /**
    *   Specify read-only block in memory. Instances of this type are not copyable, only moveable.
    */
    struct ReadonlyMemoryChunk : MemoryChunkBase
    {

        ReadonlyMemoryChunk(
            segment_pos_t offset, 
            ShadowBuffer buffer,
            segment_pos_t count, 
            FarAddress address) :
            MemoryChunkBase(offset, std::move(buffer), count, std::move(address))
        {
        }

        ReadonlyMemoryChunk(
            ShadowBuffer buffer,
            segment_pos_t count, 
            FarAddress address) :
            MemoryChunkBase(0, std::move(buffer), count, address)
        {
        }

        ReadonlyMemoryChunk() = default;

        ReadonlyMemoryChunk(ReadonlyMemoryChunk&& right) noexcept
        {
            MemoryChunkBase::operator=(std::move(right));
        }

        ReadonlyMemoryChunk& operator = (ReadonlyMemoryChunk&& right) noexcept
        {
            MemoryChunkBase::operator=(std::move(right));
            return *this;
        }

        ReadonlyMemoryChunk(const ReadonlyMemoryChunk&) = delete;
        ReadonlyMemoryChunk& operator = (const ReadonlyMemoryChunk&) = delete;

        /**
        *   @param idx - byte offset (not an item index)
        */
        template <class T>
        const T* at(segment_pos_t idx) const
        {
            assert(idx < this->count());
            return reinterpret_cast<T*>(pos() + idx);
        }
    };

    /**Helper that provides readonly access to virtual memory occupied by single instance or array of 
    * aligned instances of `T`
    * \tparam T - "standard layout types" (see http://www.stroustrup.com/C++11FAQ.html#PODs)
    */
    template <class T>
    struct ReadonlyAccess : public ReadonlyMemoryChunk
    {
        static_assert(std::is_standard_layout_v<T>, "Type used with MemoryChunk must have 'standard layout'");
        ReadonlyAccess(ReadonlyMemoryChunk&& right) OP_NOEXCEPT
            : ReadonlyMemoryChunk(std::move(right))
        {
        }

        ReadonlyAccess(ReadonlyAccess<T>&& right) OP_NOEXCEPT
            : ReadonlyMemoryChunk(std::move(right))
        {
        }
        using ReadonlyMemoryChunk::ReadonlyMemoryChunk;
        using ReadonlyMemoryChunk::operator =;

        operator const T* () const
        {
            return ReadonlyMemoryChunk::at<T>(0);
        }
        
        const T& operator *() const
        {
            return *ReadonlyMemoryChunk::at<T>(0);
        }
        
        const T* operator -> () const
        {
            return ReadonlyMemoryChunk::at<T>(0);
        }
        /**
        * @return const reference to array element reside by specified index
        * @throws std::out_of_range if index address invalid element
        */
        const T& operator[](segment_pos_t index) const
        {
            auto byte_offset = OP::utils::memory_requirement<T>::array_size( index );
            if (byte_offset >= this->count())
                throw std::out_of_range("index is out of range");
            return *ReadonlyMemoryChunk::at<T>(byte_offset);
        }
    };
    /**
    *   Create readonly accessor to some virtual memory
    * \tparam T some type that resides at accessed readonly memory
    * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentToplogy)
    * \return accessor that provide readonly (const) access to instance of `T`
    */
    template <class T, class SMProvider>
    ReadonlyAccess<T> view(SMProvider& segment_manager_provider, FarAddress pos, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c)
    {
        return ReadonlyAccess<T>(
            std::move(
                resolve_segment_manager(segment_manager_provider)
                .readonly_block(pos, OP::utils::memory_requirement<T>::requirement, hint)
            ));
    }
        
    /**
    *   Create readonly accessor to virtual memory occupied by array of `T` elements of length `number_elements`
    * \tparam T some type that resides at accessed readonly memory
    * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentToplogy)
    * \param number_elements number of items in accessed array
    * \return accessor that provide readonly (const) access to array of `T`s
    */
    template <class T, class SMProvider>
    ReadonlyAccess<T> array_view(SMProvider& segment_manager_provider, FarAddress pos,
        segment_pos_t number_elements,
        ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c)
    {
        using A = ReadonlyAccess<T>;
        return A(
            std::move(
                resolve_segment_manager(segment_manager_provider)
                .readonly_block(pos, OP::utils::memory_requirement<T>::array_size( number_elements ), hint)
            ));
    }
        
} //ns::OP::vtm

#endif //_OP_VTM_MEMORYCHUNKS__H_
