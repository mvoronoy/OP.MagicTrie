#pragma once

#ifndef _OP_VTM_SIZERADIXROUTINGINDEX__H_
#define _OP_VTM_SIZERADIXROUTINGINDEX__H_

#include <op/common/Utils.h>

#include <op/vtm/AppendOnlyMemoryPool.h>
#include <op/vtm/SegmentManager.h>


namespace OP::vtm
{
    namespace concepts
    {
        // Concept: Callable that takes a single argument of type `Payload`
        template <typename F, typename Arg>
        concept LambdaAcceptingPayload = std::invocable<F, Arg>;
    }//ns:concepts
    
    /**
    *
    * A static mixed-radix hierarchical index for free memory blocks. Unlike conventional balanced trees
    * or hash tables, the index has a fixed topology determined solely by the supported allocation size
    * range. Each node represents exactly one allocation size and stores the head of the corresponding
    * free list. Lookup is performed by decomposing the requested allocation size into mixed-radix digits
    * using successive quotient/remainder operations. This provides deterministic O(depth) navigation while
    * significantly reducing the metadata required compared with a flat table indexed by block size.
    * The hierarchy is implicit rather than explicitly stored. Node identities are reconstructed directly
    * from the requested allocation size using mixed-radix decomposition, eliminating the need to allocate
    * tree nodes or maintain parent/child pointers.
    * @tparam Payload must expose field `far_t _next`.
    */
    template <class Payload, OP::vtm::segment_pos_t max_number = (1ul << 16)>
    struct SizeRadixRoutingIndex
    {
        static_assert(std::is_standard_layout_v<Payload>,
            "template argument `Payload` must have standard layout");

        using segment_pos_t = OP::vtm::segment_pos_t;
        using far_t = OP::vtm::FarAddress;
        constexpr static inline segment_pos_t max_number_c = max_number;
        constexpr static inline segment_pos_t min_bucket_size_c = 4;

        enum class AddStatus { ok, too_big };

        struct ClaimResult
        {
            far_t _block_address{};
            std::uint8_t* _block = nullptr;
            segment_pos_t _block_size = 0;

            constexpr bool valid() const noexcept
            {
                return !_block_address.is_nil();
            }

            constexpr bool operator!() const noexcept
            {
                return _block_address.is_nil();
            }

        };

        SizeRadixRoutingIndex(OP::vtm::SegmentManager& manager, OP::vtm::AppendOnlyMemoryPool& pool, far_t root) noexcept
            : _sm(manager)
            , _pool(pool)
            , _root_page(root)
        {
        }

        SizeRadixRoutingIndex(OP::vtm::SegmentManager& manager, OP::vtm::AppendOnlyMemoryPool& pool)
            : SizeRadixRoutingIndex(manager, pool, create_page(manager, pool, 0))
        {
        }

        FarAddress root_address() const
        {
            return _root_page;
        }

        [[nodiscard]] AddStatus add_block(far_t addr, segment_pos_t size)
        {
            //assert(size >= OP::utils::memory_requirement<Payload>::requirement);
            if (size > max_number_c)
                return AddStatus::too_big;
            find_insert_point(size, [&](SkipPointer& insert_pos) {
                append_list(insert_pos, addr);
                });
            return AddStatus::ok;
        }

        /** When there are several block if the same size, this method more efficient. */
        template <class AddressEnumerable>
        [[nodiscard]] AddStatus add_multiple_blocks(segment_pos_t size, AddressEnumerable&& enum_addresses)
        {
            //assert(size >= OP::utils::memory_requirement<Payload>::requirement);
            if (size > max_number_c)
                return AddStatus::too_big;
            find_insert_point(size, [&](SkipPointer& insert_pos) {
                auto seq = enum_addresses.compound();
                for (seq.start(); seq.in_range(); seq.next())
                {
                    append_list(insert_pos, seq.current()); //add address to skip point
                }
                }
            );
            return AddStatus::ok;
        }


        [[nodiscard]] ClaimResult claim(segment_pos_t size, bool size_must_match)
        {
            //assert(size >= OP::utils::memory_requirement<Payload>::requirement);
            segment_pos_t level_capacity = max_number_c;

            auto size_factorization = size - 1;
            segment_pos_t level = 0;
            std::array<
                Fallback,
                //worst case scenario - height of the tree log[4+1](Max+1) = log2(max+1)/log2(Smallest_Page_size+1)
                (std::bit_width(max_number_c + 1) + std::bit_width(min_bucket_size_c) - 1) / (std::bit_width(min_bucket_size_c) - 1)
            > fallback_stack;
            segment_pos_t fallback_stack_idx = 0;

            for (auto page_addr = _root_page; !page_addr.is_nil();)
            {
                const auto level_slots_number = page_size(level);
                auto slot_size = (level_capacity + level_slots_number - 1) / level_slots_number;
                level_capacity = slot_size;

                auto key = size_factorization / slot_size;

                assert(key < level_slots_number);
                size_factorization %= slot_size;
                ++level;

                std::mutex* bucket_acc = mutex_for_bunch(page_addr);

                SkipPointer* page = skip_pointer_array(page_addr, level_slots_number);
                if (!size_must_match) //fallback enabled
                {
                    assert(fallback_stack_idx < fallback_stack.size());
                    segment_pos_t prev_abs_size = 0;
                    if (fallback_stack_idx)
                        prev_abs_size = fallback_stack[fallback_stack_idx - 1].in_parent_size();//._parent_size;
                    fallback_stack[fallback_stack_idx++] = Fallback{
                        ._parent_bucket = page, ._parent_acc = bucket_acc, ._position = key,
                        ._parent_size = prev_abs_size,
                        ._block_size = slot_size
                    };
                }
                std::unique_lock guard(*bucket_acc);

                if (size_factorization == 0) //entry found
                {
                    far_t result = page[key]._payload; //may be nil
                    if (!result.is_nil())
                    {//no need additional lock, just remove top item from the list
                        std::uint8_t* buffer{};
                        std::tie(page[key]._payload, buffer) = pick_top_payload(result);
                        return { result, buffer, size }; //exact match of size
                    }
                    break; //no precise match
                }
                page_addr = page[key]._children;
            }
            if (!size_must_match && fallback_stack_idx) //need lookup over bigger blocks
            {
                return find_bigger_block(fallback_stack, fallback_stack_idx);
            }
            return {};
        }

        template <class FCallback>
            requires concepts::LambdaAcceptingPayload<FCallback, const Payload&>
        void for_each(FCallback callback) const
        {
            dump_recursive_step(callback, _root_page, 0);
        }

    private:
        struct SkipPointer
        {
            far_t _payload{};
            far_t _children{}; //pointer to SkipBucket
        };

        struct Fallback
        {
            SkipPointer* _parent_bucket = nullptr;
            std::mutex* _parent_acc = nullptr;
            segment_pos_t _position = OP::vtm::eos_c; //offset in parent's bucket
            segment_pos_t _parent_size = 0;
            segment_pos_t _block_size = 0;
            constexpr segment_pos_t in_parent_size() const noexcept
            {
                assert(_position < 32);
                return _parent_size + _position * _block_size;
            }
        };

        OP::vtm::SegmentManager& _sm;
        OP::vtm::AppendOnlyMemoryPool& _pool;
        far_t _root_page;
        std::array<std::mutex, 13> _skip_page_acc{}; //prime number

        template <class TArr>
        ClaimResult find_bigger_block(TArr& fallback_stack, segment_pos_t fallback_stack_idx)
        {
            segment_pos_t parent_size = 0;
            //--fallback_stack_idx; //skip ground level
            for (segment_pos_t level = fallback_stack_idx; level;)
            {
                Fallback& current = fallback_stack[--level]; //go up and right
                const auto level_slots_number = page_size(level);
                std::unique_lock guard(*current._parent_acc);
                for (++current._position; current._position < level_slots_number; ++current._position)
                {
                    auto& slot = current._parent_bucket[current._position];
                    far_t result = slot._payload; //may be nil
                    if (!result.is_nil())
                    { //have block of suitable size
                        std::uint8_t* buffer{};
                        std::tie(slot._payload, buffer) = pick_top_payload(result);
                        return { result, buffer,
                            1 + //need +1 since add_block uses (size-1)
                            current.in_parent_size()
                        };
                    }
                    //enter deep, if child exists
                    if (!slot._children.is_nil()) //go deep
                    {//push entire child bucket to research

                        const auto child_bucket_size = page_size(++level);
                        auto* page = skip_pointer_array(slot._children, child_bucket_size);
                        fallback_stack[level] = {
                            ._parent_bucket = page,
                            ._parent_acc = mutex_for_bunch(slot._children),
                            ._position = ~0u, //will scan from beginning
                            ._parent_size = current.in_parent_size(),
                            ._block_size =
                                        (current._block_size + child_bucket_size - 1) / child_bucket_size
                        };
                        ++level;
                        break; //break inner loop to allow process this fallback 
                    }
                }
            }
            return ClaimResult{};
        }
        
        template <class FCallback>
        void find_insert_point(segment_pos_t size, FCallback&& when_found)
        {
            segment_pos_t level_capacity = max_number_c;

            auto size_factorization = size - 1;
            segment_pos_t level = 0;
            for (auto page_addr = _root_page; size_factorization;)
            {
                const auto level_slots_number = page_size(level);
                const auto slot_size = (level_capacity + level_slots_number - 1) / level_slots_number;
                level_capacity = slot_size;

                const auto key = size_factorization / slot_size;
                assert(key < level_slots_number);

                size_factorization %= slot_size;
                ++level;

                SkipPointer* page = skip_pointer_array(page_addr, level_slots_number);
                std::unique_lock guard(*mutex_for_bunch(page_addr));

                if (size_factorization == 0) //entry found
                {
                    when_found(page[key]);
                    break;
                }
                else
                { // continue descending
                    if (page[key]._children.is_nil())
                    {  //need promote new child 
                        page[key]._children = create_page(_sm, _pool, level);
                    }
                    page_addr = page[key]._children;
                }
            }
        }

        /**
        * @return pair: [next address of payload list], [memory buffer allocated]
        */
        std::pair<far_t, std::uint8_t*> pick_top_payload(far_t payload_addr)
        {
            auto payload = _sm.accessor<Payload>(payload_addr);
            auto result = std::make_pair(payload->_next, reinterpret_cast<std::uint8_t*>(&payload));
            std::destroy_at(&payload); //it is not payload anymore, convert it to raw byte buffer
            return result;
        }

        std::mutex* mutex_for_bunch(far_t bunch_address)
        {
            std::hash<far_t> addr_hash;
            const auto lock_id = addr_hash(bunch_address); //only to pick a mutex
            return &_skip_page_acc[lock_id % _skip_page_acc.size()];
        }
        /**
        * @pre parent's bucket must be locked.
        */
        void append_list(SkipPointer& pointer, far_t addr)
        {
            //no need for additional lock, since memory accessible only from locked bucket
            auto wr_block = _sm.accessor<Payload>(addr);
            wr_block.make_new();
            wr_block->_next = pointer._payload;
            pointer._payload = addr; //support list of free blocks of the same size
        }

        SkipPointer* skip_pointer_array(far_t page_addr, segment_pos_t bucket_size)
        {
            auto wr_block = _sm.writable_block(page_addr, OP::utils::memory_requirement<SkipPointer>::array_size(bucket_size));
            return wr_block.at<SkipPointer>(0);
        }

        constexpr static segment_pos_t page_size(segment_pos_t level) noexcept
        {
            return level == 0 ? 16 : (level == 1 ? 8 : 4);
        }

        /**
        * Implemented as `static` to allow work before constructor
        */
        static far_t create_page(OP::vtm::SegmentManager& manager, OP::vtm::AppendOnlyMemoryPool& pool, segment_pos_t level)
        {
            auto bucket_size = OP::utils::memory_requirement<SkipPointer>::array_size(page_size(level));
            auto addr = pool.allocate(bucket_size);
            auto raw_chunk = manager.writable_block(addr, bucket_size);
            ::new(raw_chunk.pos()) SkipPointer[page_size(level)];
            return addr;
        }

        template <class FCallback>
        void dump_recursive_step(FCallback& callback, far_t bucket_addr, segment_pos_t level, segment_pos_t level_capacity = max_number_c) const
        {
            const auto n = page_size(level);
            const auto bucket_size = OP::utils::memory_requirement<SkipPointer>::array_size(n);
            auto block = _sm.readonly_block(bucket_addr, bucket_size);
            const SkipPointer* bucket = block.at<SkipPointer>(0);

            const auto slot_size = (level_capacity + n - 1) / n;

            for (OP::vtm::segment_pos_t i = 0; i < n; ++i)
            {

                //std::cout << std::setfill(' ') << std::setw(level * 4) << i << "[w=" << std::hex << slot_size << ", ";
                if (!bucket[i]._payload.is_nil())
                {
                    callback(*_sm.view<Payload>(bucket[i]._payload));
                }
                //std::cout << "]\n";
                if (!bucket[i]._children.is_nil())
                {
                    dump_recursive_step(callback, bucket[i]._children, level + 1, slot_size);
                }
            }
        }

    };



}//ns:OP::vtm
#endif //_OP_VTM_SIZERADIXROUTINGINDEX__H_
