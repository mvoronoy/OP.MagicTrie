#ifndef _OP_TRIE_MEMORYRANGES__H_
#define _OP_TRIE_MEMORYRANGES__H_
#include <op/trie/Range.h>
#include <op/trie/SegmentHelper.h>
#include <cstdint>
#include <memory>
namespace OP
{
    namespace trie
    {
        struct MemoryRangeBase;

        struct BlockDisposer
        {
            virtual ~BlockDisposer() OP_NOEXCEPT = default;
            virtual void on_leave_scope(MemoryRangeBase& closing) OP_NOEXCEPT = 0;
        };
        struct MemoryRangeBase : public OP::Range<std::uint8_t *, segment_pos_t>
        {
            typedef OP::Range<std::uint8_t *> base_t;
            MemoryRangeBase() {}
            MemoryRangeBase(std::uint8_t * pos, segment_pos_t count, FarAddress && address, details::segment_helper_p && segment) OP_NOEXCEPT
                : base_t(pos, count)
                , _address(std::move(address))
                , _segment(std::move(segment))
            {
            }
            MemoryRangeBase(segment_pos_t count, FarAddress && address, details::segment_helper_p && segment) OP_NOEXCEPT
                : base_t(segment->at<std::uint8_t>(address.offset), count),
                _address(std::move(address)),
                _segment(std::move(segment))
            {
            }
            MemoryRangeBase(MemoryRangeBase&& right)  OP_NOEXCEPT
                : base_t(right.pos(), right.count())
                , _address(std::move(right._address))
                , _segment(std::move(right._segment))
                , _disposer(std::move(right._disposer))
            {

            }
            MemoryRangeBase& operator = (MemoryRangeBase&& right)  OP_NOEXCEPT
            {
                _address = std::move(right._address);
                _segment = std::move(right._segment);
                _disposer = std::move(right._disposer);
                base_t::operator=(std::move(right));
                return *this;
            }

            MemoryRangeBase(const MemoryRangeBase&) = delete;
            MemoryRangeBase& operator = (const MemoryRangeBase&) = delete;

            ~MemoryRangeBase() OP_NOEXCEPT
            {
                if (_disposer)
                    _disposer->on_leave_scope(*this);
            }

            const FarAddress& address() const
            {
                return _address;
            }
            details::segment_helper_p segment() const
            {
                return _segment;
            }

            typedef std::unique_ptr<BlockDisposer> disposable_ptr_t;
            void emplace_disposable(disposable_ptr_t d)
            {
                _disposer = std::move(d);
            }
        private:
            disposable_ptr_t _disposer;
            FarAddress _address;
            details::segment_helper_p _segment;
        };
        /**
        *   Pointer in virtual memory and it size
        */
        struct MemoryRange : public MemoryRangeBase
        {
            MemoryRange(segment_pos_t count, FarAddress && address, details::segment_helper_p && segment) :
                MemoryRangeBase(count, std::move(address), std::move(segment)) {}

            MemoryRange(std::uint8_t * pos, segment_pos_t count, FarAddress && address, details::segment_helper_p && segment) :
                MemoryRangeBase(pos, count, std::move(address), std::move(segment))
            {
            }
            MemoryRange() = default;
            MemoryRange(MemoryRange&& right)
                :MemoryRangeBase(std::move(right))
            {
            }

            MemoryRange(const MemoryRange&) = delete;
            MemoryRange& operator = (const MemoryRange&) = delete;
            /**
            * @return new instance that is subset of this where beginning is shifeted on `offset` bytes
            *   @param offset - how many bytes to offset from beggining, must be less than #count()
            */
            MemoryRange subset(segment_pos_t offset) const
            {
                assert(offset < count());
                return MemoryRange(this->pos() + offset, count() - offset, address() + offset, this->segment());
            }
            /**
            *   @param idx - byte offset (not an item index)
            */
            template <class T>
            T* at(segment_pos_t idx)
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
            segment_pos_t copy(const void * source, segment_pos_t source_size, segment_pos_t dest_offset = 0)
            {
                if((this->count() - dest_offset) < source_size)
                    throw std::out_of_range("source size too big");
                memcpy(pos() + dest_offset, source, source_size);
                return source_size;
            }
            template <class T, size_t N>
            segment_pos_t copy(const T arr[N])
            {
                this->copy(arr, sizeof())
            }
        };
        template <class T>
        struct WritableAccess
        {
            WritableAccess(MemoryRange&& right) OP_NOEXCEPT
                : MemoryRange(std::move(right))
            {
            }
            operator T* () 
            {
                return MemoryRange::at<T>(0);
            }
            T& operator *()
            {
                return *MemoryRange::at<T>(0);
            }
            T* operator -> ()
            {
                return MemoryRange::at<T>(0);
            }
            T& operator[](segment_pos_t index)
            {
                auto byte_offset = memory_requirement<T>::requirement * index;
                if (byte_offset >= this->count())
                    throw std::out_of_range("index out of range");
                return *MemoryRange::at<T>(byte_offset);
            }
        };
        
        /**
        *   Specify read-only block in memory. Instances of this type are not copyable, only moveable.
        */
        struct ReadonlyMemoryRange : MemoryRangeBase
        {
            ReadonlyMemoryRange(segment_pos_t count, FarAddress && address, details::segment_helper_p && segment) :
                MemoryRangeBase(count, std::move(address), std::move(segment)) {}
            ReadonlyMemoryRange(std::uint8_t * pos, segment_pos_t count, FarAddress && address, details::segment_helper_p && segment) :
                MemoryRangeBase(pos, count, std::move(address), std::move(segment))
            {
            }
            ReadonlyMemoryRange() = default;
            ReadonlyMemoryRange(ReadonlyMemoryRange&& right)  OP_NOEXCEPT
            {
                MemoryRangeBase::operator=(std::move(right));
            }
            ReadonlyMemoryRange& operator = (ReadonlyMemoryRange&& right)  OP_NOEXCEPT
            {
                MemoryRangeBase::operator=(std::move(right));
                return *this;
            }

            ReadonlyMemoryRange(const ReadonlyMemoryRange&) = delete;
            ReadonlyMemoryRange& operator = (const ReadonlyMemoryRange&) = delete;

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
        template <class T>
        struct ReadonlyAccess : public ReadonlyMemoryRange
        {
            ReadonlyAccess(ReadonlyMemoryRange&& right) OP_NOEXCEPT
                : ReadonlyMemoryRange(std::move(right))
            {
            }
            operator const T* () const
            {
                return ReadonlyMemoryRange::at<T>(0);
            }
            const T& operator *() const
            {
                return *ReadonlyMemoryRange::at<T>(0);
            }
            const T* operator -> () const
            {
                return ReadonlyMemoryRange::at<T>(0);
            }
            const T& operator[](segment_pos_t index) const
            {
                auto byte_offset = memory_requirement<T>::requirement * index;
                if (byte_offset >= this->count())
                    throw std::out_of_range("index out of range");
                return *ReadonlyMemoryRange::at<T>(byte_offset);
            }
        };
        /**Hint allows specify how writable block will be used*/
        enum class WritableBlockHint : std::uint8_t
        {
            block_no_hint_c = 0,
            block_for_read_c = 0x1,
            block_for_write_c = 0x2,
            force_optimistic_write_c = 0x4,
            /**Block contains some information and will be used for r/w operations*/
            update_c = block_for_read_c | block_for_write_c,

            /**Block is used only for write purpose and doesn't contain usefull information yet*/
            new_c = block_for_write_c
        };
        /**Hint allows specify how readonly blocks will be used*/
        struct ReadonlyBlockHint
        {
            enum type : std::uint8_t
            {
                ro_no_hint_c = 0,
                /**Forces keep lock even after ReadonlyMemoryRange is released. The default behaviour releases lock */
                ro_keep_lock = 0x1
            };
        };
    } //ns::trie
} //ns::OP
#endif //_OP_TRIE_MEMORYRANGES__H_

