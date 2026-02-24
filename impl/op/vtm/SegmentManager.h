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

        struct SegmentEventListener
        {
            virtual void on_segment_allocated(segment_idx_t new_segment, SegmentManager& manager){}
            virtual void on_segment_opening(segment_idx_t new_segment, SegmentManager& manager){}
            /**
            *   Event is raised when segment is released (not deleted)
            */
            virtual void on_segment_releasing(segment_idx_t new_segment, SegmentManager& manager){}
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

            virtual segment_pos_t segment_size() const noexcept = 0;
            
            /** \brief Amount of bytes reserved in each segment for internal implementation purposes */
            virtual segment_pos_t header_size() const noexcept = 0;

            /** \brief Check if specified segment exists, create new segment if needed. */
            virtual void ensure_segment(segment_idx_t index) = 0;

            /** \brief number of segments allocated so far. */
            virtual segment_idx_t available_segments() = 0;
            
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
            *  \throws ConcurrentLockException if block is already locked for write by another transaction.
            */
            [[nodiscard]] virtual ReadonlyMemoryChunk readonly_block(
                FarAddress pos, segment_pos_t size, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c) = 0;

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
                
            virtual void subscribe_event_listener(SegmentEventListener* listener) = 0;

            /** Ensure underlying storage is synchronized */
            virtual void flush() = 0;

            /** \brief Get strong typed access to memory for read-only purposes.
            *   The method just wrap #readonly_block with typed access
            */
            template <class T>
            ReadonlyAccess<T> view(FarAddress pos, ReadonlyBlockHint hint = ReadonlyBlockHint::ro_no_hint_c)
            {
                return ReadonlyAccess<T>(
                            std::move(readonly_block(pos, OP::utils::memory_requirement<T>::requirement, hint))
                       );
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

        };

        /**Stub that return the same SegmentManager, but compatible with other owning classes */
        inline SegmentManager& resolve_segment_manager(SegmentManager& t) noexcept
        {
            return t;
        }
       
    
}//endof namespace OP::vtm

#endif //_OP_VTM_SEGMENTMANAGER__H_
