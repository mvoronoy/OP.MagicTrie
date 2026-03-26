#ifndef _OP_TRIE_BITINDEXEDVECTOR__H_
#define _OP_TRIE_BITINDEXEDVECTOR__H_

#include <op/common/Bitset.h>
#include <op/common/StackAlloc.h>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/SegmentTopology.h>
#include <op/vtm/PersistedReference.h>
#include <op/vtm/slots/HeapManager.h>

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
        using typename base_t::foreach_callback_t;
        
        using presence_item_t = std::uint64_t;
        constexpr static inline fast_dim_t bits_c = std::numeric_limits<presence_item_t>::digits; //=64
        /** number of items (uint64_t) in presence array */
        constexpr static inline fast_dim_t presence_capacity_c = 256 / bits_c; // 4 == (256 / 64)

        static_assert(MaxEntries % 4 == 0 && MaxEntries < 256);

        struct Header
        {
            presence_item_t _presence[presence_capacity_c] = {};
        };

        struct DataVector
        {
            Payload _payload[MaxEntries] = {};
        };

        struct FullData
        {
            Header _header = {};
            DataVector _data = {};
        };

        constexpr static inline vtm::segment_pos_t header_offset_c = offsetof(FullData, _header);
        constexpr static inline vtm::segment_pos_t data_offset_c = offsetof(FullData, _data);


    public:
        
        template <class ...Tx>
        BitIndexedVector(vtm::SegmentTopology<Tx...>& topology, FarAddress residence = {})
            : _segment_manager(topology.segment_manager())
            , _heap_manager(topology.template slot<vtm::HeapManagerSlot>())
            , _residence(residence)
        {
        }

        virtual fast_dim_t capacity() const override
        {
            return MaxEntries;
        }

        /**
        * Create this data structure internals in dynamic memory using HeapManagerSlot slot.
        * @return far-address that point to allocated table wrapped by helper class PersistedSizedArray
        */
        FarAddress create() override
        {
            assert(_residence.is_nil());
            std::tie(_residence, std::ignore) = _heap_manager.get().make_new<FullData>();
            return _residence;
        }

        /** Destroy entire table block */
        void destroy(FarAddress htbl) override
        {
            _heap_manager.get().deallocate(htbl);
            _residence = {}; //just for case
        }
        
        /**
        *   @return insert position or #end() if no more capacity
        */
        KvInsert insert(
            atom_t key, payload_factory_t payload_factory, void* user_data) override
        {
            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            auto wr_head = _segment_manager.get().accessor<Header>(_residence + header_offset_c);

            if (wr_head->_presence[word_idx] & bit_mask)
                return KvInsert::already_exists;

            //get current vector size as number of bits presenting
            size_t size = OP::rawbits::count_bits(wr_head->_presence);
        
            if (size == MaxEntries)//no capacity
                return KvInsert::need_grow;

            auto pos = key2pos(*wr_head, key); 

            wr_head->_presence[word_idx] |= bit_mask;

            auto wr_data = _segment_manager.get().accessor<DataVector>(_residence + data_offset_c);

            auto* ins_pos = &wr_data->_payload[pos];
            std::shift_right(ins_pos, &wr_data->_payload[size+1]/*out-of-range*/, 1);
            payload_factory(*ins_pos, user_data); //assign value
            return KvInsert::ok;
        }


        bool contains(uint16_t key) const 
        {
            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            Header h;
            _segment_manager.get().view(_residence + header_offset_c, h);
            return h._presence[word_idx] & bit_mask;
        }
        
        bool erase(atom_t key) override
        {
            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            auto wr_head = _segment_manager.get().accessor<Header>(_residence + header_offset_c);

            if (!(wr_head->_presence[word_idx] & bit_mask))
                return false; // Key not found

            //get current vector size as number of bits presenting
            size_t size = OP::rawbits::count_bits(wr_head->_presence);
            // Update bitset
            wr_head->_presence[word_idx] &= ~bit_mask;
            // Update vector
            auto wr_data = _segment_manager.get().accessor<DataVector>(_residence + data_offset_c);

            auto pos = key2pos(*wr_head, key); 
            auto* del_pos = &wr_data->_payload[pos];
            std::destroy_at(del_pos);
            std::shift_left(del_pos, &wr_data->_payload[size]/*out-of-range*/, 1);
            return true;
        }

        Payload* get(atom_t key) override
        {
            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            Header h;
            _segment_manager.get().view(_residence + header_offset_c, h);
            if( h._presence[word_idx] & bit_mask )
            {
                auto pos = key2pos(h, key);
                vtm::PersistedArray<Payload> ref_data(_residence + data_offset_c);
                return &ref_data.ref_element(_segment_manager.get(), pos);
            }
            
            return nullptr;
        }
        
        std::optional<Payload> cget(atom_t key) const override
        {
            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            Header h;
            _segment_manager.get().view(_residence + header_offset_c, h);
            if( h._presence[word_idx] & bit_mask )
            {
                auto pos = key2pos(h, key);
                vtm::ConstantPersistedArray<Payload> cref_data(_residence + data_offset_c);
                MemoryAlignedStorage<Payload> value;
                cref_data.ref_element(_segment_manager.get(), pos, *value);
                return *value;
            }
            
            return {};
        }

        void foreach(foreach_callback_t callback, void* user_data) override
        {
            Header h;
            _segment_manager.get().view(_residence + header_offset_c, h);
            auto wr_data = _segment_manager.get().accessor<DataVector>(_residence + data_offset_c);

            size_t i = 0;
            for(auto key = OP::rawbits::first_set(h._presence);
                key != OP::rawbits::nil_c;
                key = OP::rawbits::next_set(h._presence, key), ++i)
            {
                //make some heuristic optimization
                if (!callback(key, wr_data->_payload[i], user_data))
                    break;
            }
        }

        /**
        *   @param from [in/out] origin hash table that will be changed during grow. When table exceeds 128, 
        *   this became nil since no table should be used above 128
        * @return tuple of new dimension and functor that can be used as ruler how key is converted to new indexes 
        */
        bool grow_from(base_t& from) override
        {
            assert(from.capacity() < capacity()); //this object must be bigger

            auto wr_head = _segment_manager.get().accessor<Header>(_residence + header_offset_c);
            // Update vector
            auto wr_data = _segment_manager.get().accessor<DataVector>(_residence + data_offset_c);
            size_t value_pos = 0;

            auto move_item_from = [&](fast_atom_t key, Payload& value){
                OP::rawbits::set(wr_head->_presence, key);
                wr_data->_payload[value_pos++] = std::move(value);
            };
            using local_callback_t = decltype(&move_item_from);
            foreach_callback_t source_items_callback_adapter = +[](fast_atom_t key, Payload& value, void* user_data) -> bool{
                (*reinterpret_cast<local_callback_t>(user_data))(key, value);
                return true; 
            };
            from.foreach(source_items_callback_adapter, &move_item_from);
            return true;
        }

    private:

        static inline size_t key2pos(const Header& head, atom_t key) noexcept
        {
            constexpr auto header_offset = offsetof(FullData, _header);
            constexpr auto data_offset = offsetof(FullData, _data);

            const fast_dim_t word_idx = key / bits_c;
            const std::uint64_t bit_mask = 1ULL << (key % bits_c);

            // calculate rank: how many bits are set before this one?
            size_t pos = 0;
            for (size_t i = 0; i < word_idx; ++i) 
            {
                pos += std::popcount(head._presence[i]);
            }
            pos += std::popcount(head._presence[word_idx] & (bit_mask - 1));

            return pos;
        }

        std::reference_wrapper<vtm::SegmentManager> _segment_manager;
        std::reference_wrapper<vtm::HeapManagerSlot> _heap_manager;
        FarAddress _residence;
    };

}//ns: OP::trie::containers


#endif //_OP_TRIE_BITINDEXEDVECTOR__H_

