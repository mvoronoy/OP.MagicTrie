#ifndef _OP_TRIE_BITINDEXEDVECTOR__H_
#define _OP_TRIE_BITINDEXEDVECTOR__H_

#include <op/common/Bitset.h>
#include <op/common/StackAlloc.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>
#include <op/vtm/slots/HeapManager.h>

#include <op/trie/TrieOptions.h>
#include <op/trie/KeyValueContainer.h>

namespace OP::trie::containers
{

    /**
    * @brief A hybrid container combining a bitmask-based index with a contiguous ordered vector.
    * 
    * @tparam T The type of elements to store.
    * @tparam MaxEntries The maximum possible key value (must be a multiple of 4).
    * 
    * @details 
    * This container is designed for scenarios requiring **ordered storage** of generic values 
    * identified by small unsigned integer keys (e.g., uint8_t). 
    * 
    * ### Performance Rational: Bitmask vs. Sorted Array
    * Traditional ordered vectors (using `std::lower_bound`) require \f$O(\log N)\f$ comparisons 
    * and branching to find an insertion point. As $N$ grows, this results in multiple cache 
    * misses and pipeline stalls.
    * 
    * In contrast, this implementation leverages **Rank-Select** logic:
    * 1. **O(1) Search:** Uses a fixed-size bitset (`uint64_t` array) to represent presence.
    * 2. **Hardware Acceleration:** Uses `std::popcount` (mapping to CPU instructions like 
    *    `POPCNT` or `VPOPCNT`) to calculate the exact vector index in constant time relative 
    *    to the bitset size.
    * 3. **Cache Efficiency:** The bitmask usually fits in L1 cache, eliminating the pointer 
    *    chasing and memory jumps typical of binary searches.
    * 4. **Branchless Indexing:** The insertion position is calculated arithmetically, 
    *    reducing CPU branch mispredictions.
    */
    template <size_t MaxEntries, class Payload, class ParentInfo>
    class BitIndexedVector: public KeyValueContainer<Payload, ParentInfo>
    {
        
        using base_t = KeyValueContainer<Payload, ParentInfo>;
        using typename base_t::atom_t;
        using typename base_t::fast_atom_t;
        using typename base_t::dim_t;
        using typename base_t::fast_dim_t;
        using typename base_t::FarAddress;
        using typename base_t::payload_factory_t;
        using typename base_t::cforeach_callback_t;
        using typename base_t::foreach_callback_t;

        static inline constexpr fast_dim_t capacity_c = MaxEntries;
        static_assert(capacity_c % 4 == 0 && capacity_c < 256);

        using presence_item_t = std::uint64_t;
        constexpr static inline fast_dim_t bits_c = std::numeric_limits<presence_item_t>::digits; //=64

        struct DataVector
        {
            Payload _payload[capacity_c] = {};
        };

        constexpr static inline vtm::segment_pos_t data_offset_c = offsetof(DataVector, _payload);

    public:
        
        template <class ...Tx>
        BitIndexedVector(TrieOptions& options, const ParentInfo& node_info)
            : _options(options)
            , _parent_node(node_info)
        {
        }

        virtual fast_dim_t capacity() const override
        {
            return capacity_c;
        }

        /**
        * Create this data structure internals in dynamic memory using HeapManagerSlot slot.
        * @return far-address that point to allocated table wrapped by helper class PersistedSizedArray
        */
        FarAddress create() override
        {
            FarAddress result =
                _options.get().allocator_service().allocate_for<DataVector>();
            vmem().template make_new<DataVector>(result);
            return result;
        }

        FarAddress create_from(const base_t& from) override
        {
            assert(from.capacity() < capacity()); //this object must be bigger

            FarAddress result =
                _options.get().allocator_service().allocate_for<DataVector>();
            auto wr_data = vmem().template make_new<DataVector>(result);
            auto* payload = wr_data->_payload;

            size_t value_pos = 0;
            auto move_item_from = [&](fast_atom_t key, const Payload& value){
                payload[value_pos++] = value;
            };
            using local_callback_t = decltype(&move_item_from);
            from.cforeach(source_items_callback_adapter<local_callback_t>, &move_item_from);
            return result;
        }

        /** Destroy entire table block */
        void destroy(FarAddress tbl) override
        {
            _options.get().allocator_service().deallocate(tbl);
        }
        
        /**
        *   @return KvInsert::ok on success or KvInsert::need_grow if no more capacity
        */
        KvInsert insert(
            atom_t key, payload_factory_t payload_factory, void* user_data) override
        {
            //get current vector size as number of bits presenting
            auto size = parent_node().size();
        
            if (size >= capacity_c)//no capacity
                return KvInsert::need_grow;

            auto pos = parent_node().count_bits_before(key);

            auto wr_data = vmem().access<DataVector>(parent_node().reindex_table());
            auto* payload = wr_data->_payload;
            std::shift_right(&payload[pos], &payload[size+1]/*out-of-range*/, 1);
            payload_factory(payload[pos], user_data); //assign value
            return KvInsert::ok;
        }


        //bool contains(uint16_t key) const 
        //{
        //    const fast_dim_t word_idx = key / bits_c;
        //    const std::uint64_t bit_mask = 1ULL << (key % bits_c);

        //    Header h;
        //    _segment_manager.get().view(_residence + header_offset_c, h);
        //    return h._presence[word_idx] & bit_mask;
        //}
        
        bool erase(atom_t key) override
        {
            //get current vector size as number of bits presenting
            auto size = parent_node().size();
            // Update vector
            auto wr_data = vmem().access<DataVector>(
                parent_node().reindex_table());

            auto pos = parent_node().count_bits_before(key);
            auto* del_pos = &wr_data->_payload[pos];
            std::destroy_at(del_pos);
            std::shift_left(del_pos, &wr_data->_payload[size]/*out-of-range*/, 1);
            return true;
        }

        Payload& get(atom_t key) override
        {
            assert(parent_node().presence(key));
            auto pos = parent_node().count_bits_before(key);
            return *vmem().access_element<Payload>(
                parent_node().reindex_table() + data_offset_c, pos);
        }
        
        virtual vtm::ReadonlyAccess<Payload> cget(atom_t key) const override
        {
            assert(parent_node().presence(key));
            auto pos = parent_node().count_bits_before(key);
            return vmem().view_element<Payload>(
                parent_node().reindex_table() + data_offset_c, pos);
        }

        void cforeach(cforeach_callback_t callback, void* user_data) const override
        {
            auto ro_data = vmem().view<DataVector>(parent_node().reindex_table());
            const auto* payload = ro_data->_payload;
            size_t i = 0;
            for(auto key = parent_node().presence_first_set();
                !vtm::is_nil(key);
                key = parent_node().presence_next_set(static_cast<atom_t>(key)), ++i)
            {
                //make some heuristic optimization
                if (!callback(static_cast<atom_t>(key), payload[i], user_data))
                    break;
            }
        }

        void foreach(foreach_callback_t callback, void* user_data) override
        {
            auto wr_data = vmem().access<DataVector>(parent_node().reindex_table());
            auto* payload = wr_data->_payload;

            size_t i = 0;
            for(auto key = parent_node().presence_first_set();
                !vtm::is_nil(key);
                key = parent_node().presence_next_set(static_cast<atom_t>(key)), ++i)
            {
                //allow early exit
                if (!callback(static_cast<atom_t>(key), payload[i], user_data))
                    break;
            }

        }

    private:
        template <class FCallback>
        static bool source_items_callback_adapter(fast_atom_t key, const Payload& value, void* user_data)
        {
            (*reinterpret_cast<FCallback>(user_data))(key, value);
            return true;
        };

        const ParentInfo& parent_node() const noexcept
        {
            return _parent_node.get();
        }
        
        TrieMemoryService& vmem() const noexcept
        {
            return _options.get().memory_service();
        }

        inline fast_dim_t key2pos(atom_t key) noexcept
        {
            return parent_node().count_bits_before(key);
        }

        std::reference_wrapper<TrieOptions> _options;
        std::reference_wrapper<const ParentInfo> _parent_node;
    };

}//ns: OP::trie::containers


#endif //_OP_TRIE_BITINDEXEDVECTOR__H_

