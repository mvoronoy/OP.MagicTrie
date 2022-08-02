#ifndef _OP_TRIE_HASHTABLE__H_
#define _OP_TRIE_HASHTABLE__H_

#include <op/common/typedefs.h>
#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>
#include <op/trie/Containers.h>
#include <op/trie/KeyValueContainer.h>

namespace OP::trie::containers
{

    namespace details
    {
       
        /**
        *  Define how many steps may make hash-table algorithm with open address
        *  table before give-up to locate correct item
        */
        constexpr inline dim_t max_hash_neighbors(dim_t dim) noexcept
        {
            assert(((dim-1) & dim) == 0 && dim >=8 && dim <= 256);
            switch (dim)
            {
            case 8:
            case 128:
                return 2;
            case 16:
            case 32:
                return 3;
            case 64:
                return 4;
            default:
                assert(false);
                //no break (!)
            case 256:
                return 1;
            }
        }
        /** Define how hash table grow on reaching size limit */
        constexpr inline dim_t grow_size(dim_t capacity) noexcept
        {
            //capacity must be pow of 2
            assert(((capacity-1) & capacity) == 0 && capacity >=8 );
            if (capacity <= 128) 
                return capacity * 2;
            else
            {
                assert(capacity < 256); //must never grow to 256
                return 256;
            }
        };
        
        /**function forms bit-mask to evaluate hash. It is assumed that param capcity is ^2 */
        constexpr inline dim_t bitmask(dim_t capacity) noexcept
        {
            assert(((capacity - 1) & capacity) == 0 && capacity >= 8 && capacity <= 256);
            return capacity - 1 /*on condition capacity is power of 2*/;
        }
        
        constexpr inline atom_t hash(atom_t k, dim_t table_size) noexcept
        {
            //table_size must be pow of 2 and in the range [8..256]
            assert(((table_size-1) & table_size) == 0 && table_size >=8 && table_size <= 256);
            return static_cast<atom_t>(
                 static_cast<dim_t>(k) & bitmask(table_size));
        }


        /**Set bits of the specified flag*/
        template <class HashTable>
        void set_flag(HashTable* ht, fast_dim_t index, atom_t flag)
        {
            auto &v = ht[index];
            v.flag = v.flag | flag;
        }
        
        /** Clear bits of the specified flag*/
        template <class HashTable>
        void reset_flag(HashTable* ht, fast_dim_t index, atom_t flag)
        {
            auto &v = ht[index];
            v.flag = v.flag & ~(flag);
        }
        
        /**Clear bits of the specified flag*/
        template <class HashTable>
        bool has_flag(HashTable* ht, fast_dim_t index, atom_t flag) 
        {
            auto &v = ht[index];
            return (v.flag & flag) != 0;
        }
        
        
        /**\tparam Accessor - either ReadonlyAccess<HashTableData::content_t> or WritableAccess<HashTableData::content_t> */
        template <class HashTable>
        atom_t get_flag(const HashTable* ht, fast_dim_t index) 
        {
            auto &v = ht[index];
            return v.flag;
        }
        
        template <class HashTable>
        const auto& get_value(const HashTable* ht, fast_dim_t index) 
        {
            const auto &v = ht[index];
            assert(v.flag & fpresence_c);
            return v;
        }
        
    }//ns:details


    template <class Payload, class ParentInfo>
    struct PersistedHashTable : KeyValueContainer<Payload, ParentInfo>
    {

        using this_t = PersistedHashTable<Payload, ParentInfo>;
        using base_t = KeyValueContainer<Payload, ParentInfo>;

        /** Define conetent of the table, including origin key and payload*/
        struct Content
        {
            atom_t key;
            bool presence = false;
            Payload payload = {};
        };

        using persisted_table_t = PersistedArray<Content>;
        using const_persisted_table_t = ConstantPersistedArray<Content>;
        using payload_factory_t = typename base_t::FPayloadFactory;

        /**
        * \tparam some specialization of SegmentTopology with mandatory slot `HeapManagerSlot`
        */
        template <class TSegmentTopology>
        PersistedHashTable(TSegmentTopology& topology, const ParentInfo& node_info, dim_t capacity)
            : _segment_manager(topology.segment_manager())
            , _heap_manager(topology.OP_TEMPL_METH(slot)<HeapManagerSlot>())
            , _node_info(node_info)
            , _capacity(capacity)
        {
            assert(((capacity -1) & capacity) == 0 && capacity >=8 && capacity <= 128);
        }
        
        /**
        * Create HashTableData<Payload> in dynamic memory using HeapManagerSlot slot
        * @return far-address that point to allocated table wrapped by helper class PersistedSizedArray
        *           
        */
        FarAddress create() override
        {
            
            auto byte_size = persisted_table_t::memory_requirement(_capacity);

            persisted_table_t result { _heap_manager.allocate(byte_size) };

            auto* container = result.ref(_segment_manager, _capacity);
            for(dim_t i = 0; i < _capacity; ++i)
                container[i]={};
            return result.address;
        }

        /** Destroy entire table block */
        void destroy(FarAddress htbl) override
        {
            _heap_manager.deallocate(htbl);
        }

        /**
        *   @return insert position or #end() if no more capacity
        */
        std::pair<dim_t, bool> insert(
                atom_t key, const payload_factory_t& payload_factory) override
        {
            using namespace details;
            persisted_table_t ref_data(_node_info.reindex_table());
            //auto& head = ref_data.size_ref(_segment_manager);
            auto* hash_data = ref_data.ref(_segment_manager, _capacity);
            return insert_impl(hash_data, key, payload_factory);
        }

        /**
        * @tparam OnMoveCallback - functor that is called if internal space of hash-table is optimized (shrinked). Argumnets (from, to)
        * @return true if key was erased
        */
        bool erase(atom_t key) override
        {
            using namespace details;
            dim_t hash = details::hash(key, _capacity);
            persisted_table_t ref_data(_node_info.reindex_table());
            auto* hash_data = ref_data.ref(_segment_manager, _capacity);

            for (auto i = 0; i < max_hash_neighbors(_capacity); ++i)
            {
                if (!hash_data[hash].presence)
                { //nothing at this pos
                    return false;
                }
                if ( hash_data[hash].key == key)
                {//make erase
                    //may be rest of neighbors sequence may be shifted by 1, so scan in backward
                    restore_on_erase(hash_data, hash);
                    return true;
                }
                ++hash %= _capacity; //keep in boundary
            }
            //looks was try to erase non-existing entry
            return false;
        }

        atom_t hash(atom_t key) const override
        {
            return details::hash(key, _capacity);
        }

        virtual atom_t reindex(atom_t key) const override 
        {
            auto reindexed = find(key);
            assert(reindexed != dim_nil_c);
            return static_cast<atom_t>(reindexed);
        }

        /**Find index of key entry or #end() if nothing found*/
        dim_t find(atom_t key) const override
        {
            dim_t result = dim_nil_c;
            persisted_table_t ref_data(_node_info.reindex_table());
            find_impl(ref_data, key, [&](auto idx, Content& ){
                result = idx;
            });
            return result;    
        }

        Payload* get(atom_t key) const override
        {
            Payload* ptr = nullptr;
            persisted_table_t ref_data(_node_info.reindex_table());
            find_impl(ref_data, key, [&](auto idx, Content& data){
                ptr = &data.payload;
            });
            
            return ptr;
        }
        
        std::optional<Payload> cget(atom_t key) const override
        {
            std::optional<Payload> result;
            const_persisted_table_t ref_data(_node_info.reindex_table());
            find_impl(ref_data, key, [&](auto idx, const Content& data){
                result.emplace(data.payload);
            });
            return result;
        }

        /**
        *   @param from [in/out] origin hash table that will be changed during grow. When table exceeds 128, 
        *   this became nil since no table should be used above 128
        * @return tuple of new dimension and functor that can be used as ruler how key is converted to new indexes 
        */
        bool grow_from(KeyValueContainer& from, FarAddress& result) override
        {
            assert(_node_info.capacity() < _capacity); //this object must be bigger
            result = create();

            using namespace details;
            persisted_table_t to_ref(result);
            auto* to_data = to_ref.ref(_segment_manager, _capacity);

            auto i = _node_info.presence_first_set();
            
            //iterate only over occupied slots
            for(atom_t key = static_cast<atom_t>(i);
                i != dim_nil_c;
                i = _node_info.presence_next_set(key), 
                key = static_cast<atom_t>(i))
            {
                auto *v = from.get(key);
                assert(v); //must exists since presence() == true
                auto res = insert_impl(to_data, key, 
                    [&](Payload& dest)->void{
                        dest = *v;//don't use std::move there since method can re-try
                    }
                );
                if(!res.second) //grow must be always succeeded since this buffer 2 times bigger
                {
                    destroy(result);
                    result = {};
                    return false;
                }
            }
            return true;
        }
  
    private:
        template <class F>
        std::pair<dim_t, bool> insert_impl(Content* hash_data,
                atom_t key, F&& payload_factory) 
        {
            using namespace details;

            atom_t hash = details::hash(key, _capacity);

            for (auto i = 0; i < max_hash_neighbors(_capacity); ++i)
            {
                auto &v = hash_data[hash];
                if (!v.presence)
                { //nothing at this pos
                    v.key = key;
                    if constexpr(std::is_base_of_v<payload_factory_t, std::decay_t<F> >)
                    {
                        payload_factory.inplace_construct(v.payload);
                    }
                    else
                    {
                        payload_factory(v.payload);
                    }
                    v.presence = true;
                    return std::make_pair(static_cast<dim_t>(hash), true);
                }
                if ( v.key == key )
                    return std::make_pair(static_cast<dim_t>(hash), false); //already exists
                ++hash %= _capacity; //try next slot, but keep in boundary
            }
            //has no enough place
            return std::make_pair(dim_nil_c, false); //no capacity
        }
        
        template <class TableRef, class FCollect >
        bool find_impl(TableRef& ref_data, atom_t key, FCollect collect) const 
        {
            using namespace details;
            dim_t hash = details::hash(key, _capacity);
            auto hash_data = ref_data.ref(_segment_manager, _capacity);

            for (unsigned i = 0; i < max_hash_neighbors(_capacity); ++i)
            {
                auto& v = hash_data[hash];
                if (!v.presence)
                { //nothing at this pos
                    break;
                }
                if (v.key == key)
                {
                    collect(hash, v);
                    return true;
                }
                ++hash %= _capacity; //keep in boundary
            }
            return false;
        }

        /** Optimize space before some item is removed
        * @tparam OnMoveCallback - functor that is called if internal space of hash-table is optimized (shrinked). Argumnets (from, to)
        * @return - during optimization this method may change origin param 'erase_pos', so to real erase use index returned
        */
        
        unsigned restore_on_erase(
            Content* hash_data, unsigned erase_pos)
        {
            using namespace details;
            auto& item = hash_data[erase_pos];
            unsigned erased_hash = details::hash(item.key, _capacity);
            unsigned limit = max_hash_neighbors( _capacity); 

            for (unsigned i = (erase_pos + 1) % _capacity; limit; ++i %= _capacity, --limit)
            {
                auto& local_item = hash_data[i];
                if (!local_item.presence)
                    break; //stop optimization and erase item at this pos
                unsigned local_hash = details::hash(local_item.key, _capacity);
                bool item_in_right_place = i == local_hash;
                if (item_in_right_place)
                    continue;
                unsigned x = less_pos(erased_hash, erase_pos, _capacity) ? erase_pos : erased_hash;
                if (!less_pos(x, local_hash, _capacity)/*equivalent of <=*/)
                {
                    hash_data[erase_pos] = std::move(local_item);
                    erase_pos = i;
                    erased_hash = local_hash;
                    limit = (erase_pos + max_hash_neighbors(_capacity)) % _capacity;
                }
            }
            hash_data[erase_pos].presence = false;
            return erase_pos;
        }

        /** test if tst_min is less than tst_max on condition of cyclyng nature of hash buffer, :
            For page-size = 16:
            less(0xF, 0x1) == true
            less(0x1, 0x2) == true
            less(0x1, 0xF) == false
            less(0x2, 0x1) == false
            less(0x5, 0xF) == true
            less(0xF, 0x5) == false
        */
        static bool less_pos(unsigned tst_min, unsigned tst_max, unsigned capacity) 
        {
            int dif = static_cast<int>(tst_min) - static_cast<int>(tst_max);
            unsigned a = std::abs(dif);
            if (a > (static_cast<unsigned>(capacity) / 2)) //use inversion of signs
            {
                return dif > 0;
            }
            return dif < 0;
        }

    private:
        SegmentManager& _segment_manager;
        HeapManagerSlot& _heap_manager;
        const ParentInfo& _node_info;
        const dim_t _capacity;
    };

    //template <class T, class >
    //inline SegmentManager& resolve_segment_manager(PersistedHashTable<T>& htbl)
    //{
    //    return htbl.topology().segment_manager();
    //}
}//ns: OP::trie::containers


#endif //_OP_TRIE_HASHTABLE__H_
