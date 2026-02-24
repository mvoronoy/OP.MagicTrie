#pragma once
#ifndef _OP_COMMON_APPENDATOMLIST__H_
#define _OP_COMMON_APPENDATOMLIST__H_

#include <atomic>
#include <thread>
#include <cassert>

namespace OP::common
{

    /**
     * \brief Lock-free singly-linked list optimized for concurrent append operations.
     *
     * AppendAtomicList<T> is a lightweight concurrent container that allows efficient,
     * lock-free appending of elements to the end of the list. The implementation
     * is optimized under the assumption that elements are only appended at the tail.
     *
     * ### Concurrency Model
     *
     * - \ref append is thread-safe and supports multiple concurrent producers.
     * - \ref remove and \ref clear may execute concurrently with append.
     * - Only **one thread at a time** is allowed to call \ref remove or \ref clear.
     *
     * If multiple concurrent removals are required, external synchronization
     * (e.g. a mutex such as `remove_concurrent_access`) must be used by the caller.
     *
     * ### Design Assumptions
     *
     * The implementation simplifies synchronization by:
     * - Allowing insertion **only at the tail**.
     * - Maintaining an atomic pointer to the last insertion position.
     * - Temporarily permitting structural decoherence during append,
     *   which is resolved before the operation completes. The list may be transiently
     *   disconnected during append; however, forward traversal starting from head remains valid for already committed nodes.
     *
     * This design avoids global locking and reduces contention compared to
     * generic concurrent containers that support arbitrary insertion.
     *
     * ### Memory Management
     *
     * Nodes are dynamically allocated during append and destroyed during removal
     * or \ref clear. The container does not implement hazard pointers or epoch-based
     * reclamation; therefore:
     *
     * - Iterators must not outlive concurrent modifications.
     * - The caller is responsible for ensuring safe iteration in concurrent contexts.
     *
     * ### Thread Safety Summary
     *
     * | Operation | Concurrent append | Concurrent remove |
     * |-----------|-------------------|-------------------|
     * | append    | Yes               | Yes               |
     * | remove/clear | Yes               | No (single caller only) |
     * | iteration | Stable, may not correctly handle last item | Stable, unsafe without external synchronization |
     *
     * \tparam T Stored value type.
     */
    template <class T>
    class AppendAtomicList
    {
        struct Node;
        using atomic_ptr_t = std::atomic<Node*>;

        struct Place
        {
            atomic_ptr_t _next{ nullptr };
        };

        struct Node : Place
        {
            template <class...Args>
            Node(Args&& ...constructor_args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
                : _value(std::forward<Args>(constructor_args)...)
            {
            }

            T _value;
        };

        static_assert(
            std::is_standard_layout_v<Place>, 
            "Platform without this condition cannot implement #remove method");

        Place _head;
        std::atomic<atomic_ptr_t*> _last;

    public:
        struct IteratorImpl
        {
            using iterator_category = std::forward_iterator_tag;

            constexpr IteratorImpl() noexcept
                : _pos (nullptr)
            {
            }

            IteratorImpl& operator ++()
            {
                assert(_pos != nullptr);
                _pos = &_pos->load(std::memory_order_acquire)->_next;
                return *this;
            }

            T& operator* () noexcept
            {
                assert(_pos != nullptr);
                return _pos->load(std::memory_order_acquire)->_value;
            }

            T* operator-> ()
            {
                return &operator*();
            }
            
            //constexpr std::strong_ordering operator <=> (const IteratorImpl& other) const
            constexpr bool operator == (const IteratorImpl& other) const noexcept = default;

        private:
            
            atomic_ptr_t* _pos;

            friend AppendAtomicList;

            constexpr IteratorImpl(atomic_ptr_t* pos) noexcept
                :_pos(pos)
            {
            }

        };

        /**
         * \brief Forward iterator over AppendAtomicList.
         *
         * The iterator provides single-pass forward traversal.
         *
         * \warning Iteration is not safe against concurrent removal without
         *          external synchronization.
         */
        using iterator = IteratorImpl;


        AppendAtomicList()
            : _last(&_head._next)
        {
        }
        
        ~AppendAtomicList()
        {
            clear();
        }

        iterator begin() noexcept
        {
            return iterator(&_head._next);
        }

        constexpr iterator end() noexcept
        {
            return iterator(_last.load(std::memory_order_acquire));
        }

        /**
         * \brief Remove all elements from the list.
         *
         * Not thread-safe. Must not be called concurrently with append or remove.
         */
        void clear()
        {
            for (auto next = _head._next.load(std::memory_order_acquire); next;)
            {
                auto pos = next->_next.load(std::memory_order_acquire);
                delete next;
                next = pos;
            }
            _last.store(&_head._next, std::memory_order_release);
            _head._next = nullptr;
        }

        /**
         * \brief Append a new element at the end of the list.
         *
         * This operation is lock-free and supports concurrent execution
         * from multiple threads.
         *
         * Internally, the method:
         * - Allocates a new node.
         * - Atomically swaps the tail pointer.
         * - Restores structural consistency by linking the previous tail.
         *
         * During execution, the list may be temporarily structurally
         * inconsistent, but this does not prevent concurrent append operations.
         *
         * \param constructor_args Arguments forwarded to T constructor.
         * \return Iterator pointing to the inserted element.
         */
        template <class ...Args>
        [[maybe_unused]] iterator append(Args&& ...constructor_args)
        {
            Node* new_node = new Node(std::forward<Args>(constructor_args)...);
            auto prev = _last.load(std::memory_order_acquire);

            while (!_last.compare_exchange_weak(
                    prev, &new_node->_next, 
                    std::memory_order_acq_rel, std::memory_order_acquire))
            {
                //nothing 
            }
            //at this moment list is decoherent split, but allows append operations
            prev->store(new_node, std::memory_order_release); //restore
            return IteratorImpl(prev);
        }

        /**
         * \brief Remove the element pointed to by the iterator.
         *
         * This operation may run concurrently with append operations.
         *
         * \warning Only one thread may call remove() at a time.
         *          Concurrent removals require external synchronization.
         *
         * The method:
         * - Relinks the predecessor to the successor.
         * - Adjusts the tail pointer if necessary.
         * - Deletes the removed node.
         *
         * \param pos Iterator pointing to element to remove.
         * \return Iterator pointing to the next valid position.
         */
        iterator remove(iterator pos)
        {
            auto to_delete = pos._pos->load(std::memory_order_acquire);
            atomic_ptr_t* next_place = &to_delete->_next;
            auto next_next = next_place->load(std::memory_order_acquire);

            auto* field = reinterpret_cast<std::byte*>(pos._pos);
            assert(field);
            Place* prev_item = reinterpret_cast<Place*>(field - offsetof(Place, _next));
            //need strong to avoid spuriously fail, if value other then _last this just ignored
            _last.compare_exchange_strong(next_place, &prev_item->_next,
                std::memory_order_release,   // success: publish new_tail and prior writes
                std::memory_order_relaxed  // failure: cheap, no extra ordering
            );

            prev_item->_next.store(next_next, std::memory_order_release);
            //potentially it can be _last
            delete to_delete;
            return IteratorImpl(&prev_item->_next);
        }
    };

};
#endif //_OP_COMMON_APPENDATOMLIST__H_
