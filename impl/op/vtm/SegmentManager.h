#pragma once

#ifndef _OP_VTM_SEGMENTMANAGER__H_
#define _OP_VTM_SEGMENTMANAGER__H_

#include <type_traits>
#include <cstdint>
#include <memory>
#include <string>

#include <op/common/Utils.h>
#include <op/common/Exceptions.h>
#include <op/common/Range.h>

#include <op/vtm/typedefs.h>
#include <op/vtm/Transactional.h>
#include <op/vtm/MemoryChunks.h>
#include <op/vtm/vtm_error.h>

namespace OP::vtm
{        
    struct SegmentManager;
     
    class SegmentEventSupplier
    {
    public:

        /** Event codes of segments lifecycle */
        enum Code
        {
            /** Notify that new segment has been allocated, parameters defined by #UserSpaceEvent type. */
            allocated = 0,
            /** Notify existing segment is opening, parameters defined by #UserSpaceEvent type.*/
            opening,
            /** Notify releasing existing segment (don't mix with deleting). Parameter is segment_idx_t - index of segment */
            releasing,
            _event_code_count_
        };

        /** Event arguments about user space available for specified segment */ 
        struct UserSpaceEvent
        {
            segment_idx_t _id;
            //FarAddress _begin;
            /*Size of available user space, so all memory in range: `[_begin, _begin + _size)` is valid */
            //segment_pos_t _size;
        };


        using event_manager_t = OP::events::EventSupplier<
            Assoc<allocated, UserSpaceEvent>,
            Assoc<opening, UserSpaceEvent>,
            Assoc<releasing, segment_idx_t>
        >;
        
        using unsubscriber_t = event_manager_t::unsubscriber_t;

        template<Code code, class FHandler>
        unsubscriber_t bind(FHandler&& handler) 
        {
            return _supplier.on<code>(std::move(handler));
        }

        template<Code code, class TPayload>
        void send_event(TPayload payload)
        {
            _supplier.send<code>(payload);
        }

    private:
        event_manager_t _supplier;
    };

    /** Provide optimization hints for method #SegmentManager::ensure_segment */
    enum class ensure_hint_t
    {
        /** no hint, just check segment exists*/
        no,
        /** in short time there is high probability for the next segment, corelated with boost::interprocess::advice_willneed */
        next_soon
    };


    
    /**
    * \brief Abstraction that represents transactional access to memory blocks placed inside big page called segment.
    *
    */
    struct SegmentManager 
    {
        using transaction_ptr_t = OP::vtm::transaction_ptr_t;

        SegmentManager() noexcept = default;

        virtual ~SegmentManager() = default;

        /** Provide access to events of SegmentManager */
        SegmentEventSupplier& event_supplier() noexcept
        {
            return _event_supplier;
        }

        virtual segment_pos_t segment_size() const noexcept = 0;
        
        /** @brief Space available in specific segment for user purposes. 
        *   @return pair of address of first byte of user space, and size of available memory after first byte.
        */
        virtual std::pair<FarAddress, segment_pos_t> user_space(segment_idx_t) const noexcept = 0;

        /** \brief Check if specified segment exists, create new segment if needed. */
        virtual void ensure_segment(segment_idx_t index, ensure_hint_t hint = ensure_hint_t::no) = 0;

        /** \brief number of segments allocated so far. */
        virtual segment_idx_t available_segments() = 0;

        /**
        *   Override for complex scenarios of address interpretations.
        *   @return index of segment (that can be used as argument for #user_space, #ensure_segment).
        */
        virtual segment_idx_t segment_of(FarAddress pos) const noexcept
        {
            return pos.segment();
        }
        
        /**
        *   Override for complex scenarios of address interpretations.
        *   @return offset of memory in particular segment.
        */
        virtual segment_idx_t offset_of(FarAddress pos) const noexcept
        {
            return pos.offset();
        }
        
        /** Starts new transaction. 
        *   \return instance that controls scope of transaction. Caller is fully responsible to manage 
        *   lifecycle by commit/rollback operations. Try avoid long living instances.
        */
        [[nodiscard]] virtual transaction_ptr_t begin_transaction() = 0;
        
        /**
        *  \brief Get memory for read-only purposes.
        *  \return raw block of memory for reading. Review use #view method for strongly typed access to the same memory.
        *
        *  \param hint - give implementation optional hint how block will be used. Default behavior is to release lock 
        *       after ReadonlyMemoryChunk destroyed.
        *  \throws ConcurrentLockException if block conflicts with locked one by another transaction.
        */
        [[nodiscard]] virtual ReadonlyMemoryChunk readonly_block(
            FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) = 0;

        /**
        *  \throws ConcurrentLockException if block conflicts with locked one by another transaction.
        *  \throws OP::Exception if read exceeds segment size
        */
        virtual void read(
            FarAddress pos, std::uint8_t* buffer, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) = 0;

        /**
        *  \brief Get memory for write purposes.
        * \throws ConcurrentLockException if block is already locked by another transaction.
        */
        [[nodiscard]] virtual MemoryChunk writable_block(
            FarAddress pos, segment_pos_t size, WritableBlockHint hint = WritableBlockHint::update_c) = 0;

        /**
        * Implementation based integrity checking of this instance
        */
        virtual void _check_integrity(bool verbose) = 0;

        /**
        * \brief change read-only block to writable block.
        *
        * Implementation may not support this kind of upgrade, that is why default implementation provided and just call #writable_block method.
        *    
        * \throws ConcurrentLockException if block is already locked for concurrent write or concurrent read (by the other transaction).
        */
        [[nodiscard]] virtual MemoryChunk upgrade_to_writable_block(ReadonlyMemoryChunk& ro) = 0;
            
        /** Ensure underlying storage is synchronized */
        virtual void flush() = 0;

        /** \brief Get strong typed access to memory for read-only purposes.
        *   The method just wrap #readonly_block with typed access
        */
        template <class T>
        [[nodiscard]] ReadonlyAccess<T> view(FarAddress pos, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c)
        {
            return ReadonlyAccess<T>(
                        std::move(readonly_block(pos, OP::utils::memory_requirement<T>::requirement, hint))
                   );
        }
        
        /** \brief Get strong typed access to memory for read-only purposes.
        *   The method just wrap #readonly_block with typed access
        */
        template <class T>
        void view(FarAddress pos, T& dest, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c)
        {
            static_assert(std::is_standard_layout_v<T>, "Template argument T must have standard layout to plain read");
            read(pos, reinterpret_cast<std::uint8_t*>(&dest), sizeof(T), hint);
        }
        
        /** \brief Get strong typed access to memory for write purposes.
        *   The method just wrap #writable_block with typed access.
        */
        template <class T>
        WritableAccess<T> accessor(FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
        {
            return WritableAccess<T>(
                    writable_block(pos, OP::utils::memory_requirement<T>::requirement, hint)
            );
        }

        /** Shorthand for \code
        *    writable_block(pos, sizeof(T)).at<T>(0)
        *   \endcode
        */
        template <class T>
        inline T* wr_at(FarAddress pos, WritableBlockHint hint = WritableBlockHint::update_c)
        {
            return this->writable_block(
                    pos, OP::utils::memory_requirement<T>::requirement, hint)
                .template at<T>(0);
        }

            template <typename T> struct member {};
            // traits for member type
            template<typename Class, typename Type>
            struct member<Type Class::*> {
                using mem_type = Type;
                using class_type = Class;
            };

    private:
        SegmentEventSupplier _event_supplier;    
    };

    /**Stub that return the same SegmentManager, but compatible with other owning classes */
    inline SegmentManager& resolve_segment_manager(SegmentManager& t) noexcept
    {
        return t;
    }
   
    
}//endof namespace OP::vtm

#endif //_OP_VTM_SEGMENTMANAGER__H_
