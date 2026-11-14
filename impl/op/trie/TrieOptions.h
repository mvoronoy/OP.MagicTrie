#pragma once

#ifndef _OP_TRIE_TRIEOPTIONS__H
#define _OP_TRIE_TRIEOPTIONS__H

#include <op/vtm/MemoryChunks.h>
#include <op/vtm/Transactional.h>

#include <op/trie/TrieResidence.h>

namespace OP::trie
{
    namespace details
    {
        struct TrieServiceBase
        {
            virtual ~TrieServiceBase() = default;
        };
    }

    struct TrieResidenceService : details::TrieServiceBase
    {
        template <class TKeyString, class TPayloadManager>
        friend class Trie;

        //[[nodiscard]] virtual const TrieHeader& header() const = 0;

        [[nodiscard]] virtual trie_version_t version() const = 0;
        [[nodiscard]] virtual vtm::FarAddress residence(unsigned) const = 0;
        [[nodiscard]] virtual std::uint64_t count() const = 0;
        [[nodiscard]] virtual vtm::FarAddress root() const = 0;
        [[nodiscard]] virtual void store_residence(vtm::FarAddress, unsigned) = 0;
        [[nodiscard]] virtual std::uint64_t nodes_allocated() const = 0;

    private:

        virtual TrieResidenceService& increase_version() = 0;
        virtual TrieResidenceService& increase_count(std::int64_t amount = 1) = 0;
        ///**Number of nodes allocated*/
        virtual void increase_nodes_allocated(std::int64_t amount = 1) = 0;
        virtual void set_root(vtm::FarAddress) = 0;
    };

    enum class AllocHint: unsigned
    {
        /** Give a hint about allocation of fixed size regular items. */
        regular,
        /** Give a hint about allocation of fixed number of regular items. */
        array,
        /** heuristic is weak for this type (like text strings where length can vary in wide range). */
        irregular
    };

    struct TrieAllocatorService : details::TrieServiceBase
    {
        using FarAddress = vtm::FarAddress;
        using segment_pos_t = vtm::segment_pos_t;

        [[nodiscard]] virtual FarAddress allocate(segment_pos_t size, AllocHint hint) = 0;


        virtual void deallocate(FarAddress) noexcept = 0;

        virtual void deallocate(std::vector<FarAddress> to_remove)
        {
            for (const auto& addr : to_remove)
                deallocate(addr);
        }

        /** Utility for #allocate based on type information `T`, allocates number of bytes enough
        * to accommodate type `T`. Note memory is not initialized, constructor is not called. You still
        * need TrieMemoryService to access the allocated memory.
        * 
        * @param hint - Assumption: fixed c++ type - means that same type will be requested more and more
                AllocHint::regular 
        */
        template <class T>
        [[nodiscard]] FarAddress allocate_for(AllocHint hint = AllocHint::regular)
        {
            vtm::segment_pos_t expected_size =
                utils::memory_requirement<T>::requirement;
            return allocate(expected_size, hint);
        }
    };

    struct TrieTransactionService : details::TrieServiceBase
    {
        using transaction_ptr_t = vtm::transaction_ptr_t;
        using event_supplier_t = typename vtm::TransactionEvent::event_supplier_t;
        using transaction_id_t = typename vtm::TransactionEvent::transaction_id_t;

        /** Starts new transaction.
        *   \return instance that controls scope of transaction. Caller is fully responsible to manage
        *   lifecycle by commit/rollback operations. Try avoid long living instances.
        */
        [[nodiscard]] virtual transaction_ptr_t begin_transaction() = 0;
        [[nodiscard]] virtual event_supplier_t& event_supplier() = 0;
        [[nodiscard]] virtual std::optional<transaction_id_t> current_transaction_id() const = 0;
                
    };


    struct TrieMemoryService : details::TrieServiceBase
    {
        using buffer_t = vtm::ShadowBuffer;
        using FarAddress = vtm::FarAddress;
        using segment_pos_t = vtm::segment_pos_t;
        //using block_result_t = std::variant<buffer_t, vtm::ConcurrentLockException>;

        [[nodiscard]] virtual vtm::ReadonlyMemoryChunk readonly_block(FarAddress pos, segment_pos_t size) const noexcept = 0;
        [[nodiscard]] virtual vtm::MemoryChunk writable_block(FarAddress pos, segment_pos_t size, bool need_previous_content = true) = 0;


        template <class T>
        vtm::WritableAccess<T> access(FarAddress pos)
        {
            return vtm::WritableAccess<T>(
                writable_block(pos, OP::utils::memory_requirement<T>::requirement)
            );
        }

        template <class T>
        vtm::WritableAccess<T> access_element(FarAddress pos, segment_pos_t index)
        {
            return access<T>(
                pos + static_cast<segment_pos_t>(OP::utils::memory_requirement<T>::requirement * index));
        }

        /** @throws ConcurrentLockException exception if inner call to `readonly_block` failed */
        template <class T>
        vtm::ReadonlyAccess<T> view(FarAddress pos) const
        {
            return vtm::ReadonlyAccess<T>(
                readonly_block(pos, OP::utils::memory_requirement<T>::requirement)
            );
        }

        template <class T>
        vtm::ReadonlyAccess<T> view_element(FarAddress pos, segment_pos_t index) const
        {
            return view<T>(
                pos + static_cast<segment_pos_t>(OP::utils::memory_requirement<T>::requirement * index));
        }


        template <class T, class ...Args>
        [[maybe_unused]] T* make_new(FarAddress pos, Args&& ...args)
            noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            auto block = access<T>(pos);
            return ::new(block.pos()) T(std::forward<Args>(args)...);
        }
    };

    struct Logging : details::TrieServiceBase
    {
        enum Level
        {
            debug,
            info,
            warning,
            error,
            fatal
        };
        virtual void log(Level level, const char* message) noexcept = 0;
    };

    enum class OpenMode
    {
        create,
        open
    };

    struct EvalNodeSize
    {
        /**
        * @brief For the new created trie's node provide heuristic - how many keys to allocate initially at the specified level.
        *
        * The number MUST be power of 2 and not exceed 256: (2, 4, ...256).
        * Providing too small number may cause intensive growing and copying data operations, but too big size can waste a lot of memory.
        */
        virtual vtm::dim_t init_node_size(size_t level) const noexcept = 0;
    };

    /**Constant definition for trie*/
    struct TrieOptions : details::TrieServiceBase
    {
        [[nodiscard]] virtual const EvalNodeSize& init_node_size() const noexcept = 0;
        [[nodiscard]] virtual TrieResidenceService& residence_service() noexcept = 0;
        [[nodiscard]] virtual TrieAllocatorService& allocator_service() noexcept = 0;
        [[nodiscard]] virtual TrieMemoryService& memory_service() noexcept = 0;
        [[nodiscard]] virtual TrieTransactionService& transaction_service() noexcept = 0;
        [[nodiscard]] virtual Logging& logging() noexcept = 0;
    };

}//ns:OP::trie

#endif //_OP_TRIE_TRIEOPTIONS__H
