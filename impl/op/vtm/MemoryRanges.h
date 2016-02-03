#ifndef _OP_TRIE_MEMORYRANGES__H_
#define _OP_TRIE_MEMORYRANGES__H_
#include <op/common/Range.h>
#include <op/vtm/SegmentHelper.h>
#include <op/common/Utils.h>
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
            MemoryRange(MemoryRange&& right)
                :MemoryRangeBase(std::move(right))
            {
            }
            //! For MSVC-2013 it is very important to keep copy-constructor after(!) move constructor
            MemoryRange(const MemoryRange&) = delete;

            MemoryRange() = default;

            
            MemoryRange& operator = (const MemoryRange&) = delete;
            /**
            * @return new instance that is subset of this where beginning is shifted on `offset` bytes
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
                    throw std::out_of_range("source size too big");
                memcpy(pos() + dest_offset, source, source_size);
                return source_size;
            }
            //template <class T, size_t N>
            //segment_pos_t copy(const T (&arr)[N])
            //{
            //    return this->copy(arr, sizeof(arr));
            //}
            //
            //template <class T, size_t N>
            //segment_pos_t aligned_copy(const T arr[N])
            //{
            //    static OP_CONSTEXPR(const) byte_size_c = memory_requirement<T>::requirement * N;
            //    if((this->count() - dest_offset) < byte_size_c)
            //        throw std::out_of_range("source size too big");
            //    auto p = pos() + dest_offset;
            //    for (auto t : arr)
            //    {
            //        *p = t;
            //        p += memory_requirement<T>::requirement;
            //    }
            //    return byte_size_c;
            //}
        };
        namespace details
        {
            template <class T>
            inline OP_CONSTEXPR(const) typename std::enable_if< !std::is_scalar<T>::value, segment_pos_t >::type array_view_size_helper() OP_NOEXCEPT
            {
                return memory_requirement<T>::requirement;
            }
            template <class T>
            inline OP_CONSTEXPR(const) typename std::enable_if< std::is_scalar<T>::value, segment_pos_t >::type array_view_size_helper() OP_NOEXCEPT
            {
                return sizeof(T);
            }

        } //ns:details               

        /**Helper that provides writable access to virtual memory occupied by single instance or array of 
        * alligned instances of `T`
        * \tparam T - "standard layout types" (see http://www.stroustrup.com/C++11FAQ.html#PODs)
        */
        template <class T>
        struct WritableAccess : public MemoryRange
        {
            WritableAccess(MemoryRange&& right) OP_NOEXCEPT
                : MemoryRange(std::move(right))
            {
            }
            WritableAccess(WritableAccess<T>&& right) OP_NOEXCEPT
                :MemoryRange(std::move(right))
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
            const T* operator -> () const
            {
                return MemoryRange::at<T>(0);
            }
            /**
            * @return reference to array element reside by specified index
            * @throws std::out_of_range if index address invalid element
            */
            T& operator[](segment_pos_t index)
            {
                auto byte_offset = details::array_view_size_helper<T>() * index;
                if (byte_offset >= this->count())
                    throw std::out_of_range("index is out of range");
                return *MemoryRange::at<T>(byte_offset);
            }
            const T& operator[](segment_pos_t index) const
            {
                auto byte_offset = details::array_view_size_helper<T>() * index;
                if (byte_offset >= this->count())
                    throw std::out_of_range("index is out of range");
                return *MemoryRange::at<T>(byte_offset);
            }
            template <class ... Ts>
            void make_new(Ts&&... args)
            {
                if ( this->count() < details::array_view_size_helper<T>() )
                    throw std::out_of_range("too small block to allocate T");
                new (pos()) T(std::forward<Ts>(args)...);
            }
            void make_array(segment_pos_t array_size)
            {
                //items have to be alligned
                OP_CONSTEXPR(static const) auto inc = details::array_view_size_helper<T>();
                if (this->count() < (inc*array_size) )
                    throw std::out_of_range("too small block to allocate T[N]");
                for (auto p = pos(); array_size; p += inc, --array_size)
                {
                    new (p) T;
                }
                
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
        /**
        *   Create writable accessor to some virtual memory
        * \tparam T some type that resides at accesed writable memory
        * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentToplogy)
        * \return accessor that provide writable access to the instance of `T`
        */
        template <class T, class SMProvider>
        WritableAccess<T> accessor(SMProvider& segment_manager_provider, FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
        {
            return WritableAccess<T>(std::move(resolve_segment_manager(segment_manager_provider).
                writable_block(pos, memory_requirement<T>::requirement, hint)));
        }
        
        /**
        *   Create read/write accessor to virtual memory ocupied by array of `T` elements of length `number_elements`
        * \tparam T some type that resides at accesed readonly memory
        * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentTopology)
        * \param number_elements number of items in accessed array
        * \return accessor that provide readonly (const) access to array of `T`s
        */
        template <class T, class SMProvider>
        WritableAccess<T> array_accessor(SMProvider& segment_manager_provider, FarAddress pos,
            segment_pos_t number_elements,
            WritableBlockHint hint = WritableBlockHint::update_c)
        {
            using A = WritableAccess<T>;
            auto size = details::array_view_size_helper<T>() * number_elements;
            return A(std::move(resolve_segment_manager(segment_manager_provider).
                writable_block(pos, details::array_view_size_helper<T>() * number_elements, hint)));
        }
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
        /**Helper that provides readonly access to virtual memory occupied by single instance or array of 
        * alligned instances of `T`
        * \tparam T - "standard layout types" (see http://www.stroustrup.com/C++11FAQ.html#PODs)
        */
        template <class T>
        struct ReadonlyAccess : public ReadonlyMemoryRange
        {
            ReadonlyAccess(ReadonlyMemoryRange&& right) OP_NOEXCEPT
                : ReadonlyMemoryRange(std::move(right))
            {
            }
            ReadonlyAccess(ReadonlyAccess<T>&& right) OP_NOEXCEPT
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
            /**
            * @return const reference to array element reside by specified index
            * @throws std::out_of_range if index address invalid element
            */
            const T& operator[](segment_pos_t index) const
            {
                auto byte_offset = details::array_view_size_helper<T>() * index;
                if (byte_offset >= this->count())
                    throw std::out_of_range("index is out of range");
                return *ReadonlyMemoryRange::at<T>(byte_offset);
            }
        };
        /**
        *   Create readonly accessor to some virtual memory
        * \tparam T some type that resides at accesed readonly memory
        * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentToplogy)
        * \return accessor that provide readonly (const) access to instance of `T`
        */
        template <class T, class SMProvider>
        ReadonlyAccess<T> view(SMProvider& segment_manager_provider, FarAddress pos, ReadonlyBlockHint::type hint = ReadonlyBlockHint::ro_no_hint_c)
        {
            return ReadonlyAccess<T>(std::move(resolve_segment_manager(segment_manager_provider).
                readonly_block(pos, memory_requirement<T>::requirement, hint)));
        }
        
        /**
        *   Create readonly accessor to virtual memory ocupied by array of `T` elements of length `number_elements`
        * \tparam T some type that resides at accesed readonly memory
        * \tparam SMProvider some type that either castable to SegmentManager or resolved by resolve_segment_manager() (like a SegmentToplogy)
        * \param number_elements number of items in accessed array
        * \return accessor that provide readonly (const) access to array of `T`s
        */
        template <class T, class SMProvider>
        ReadonlyAccess<T> array_view(SMProvider& segment_manager_provider, FarAddress pos,
            segment_pos_t number_elements,
            ReadonlyBlockHint::type hint = ReadonlyBlockHint::ro_no_hint_c)
        {
            using A = ReadonlyAccess<T>;
            return A(std::move(resolve_segment_manager(segment_manager_provider).
                readonly_block(pos, details::array_view_size_helper<T>() * number_elements, hint)));
        }
        
        
    } //ns::trie
} //ns::OP
#endif //_OP_TRIE_MEMORYRANGES__H_

