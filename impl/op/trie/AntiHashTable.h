#ifndef _OP_TRIE_ANTIHASHTABLE__H_
#define _OP_TRIE_ANTIHASHTABLE__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>
#include <op/trie/KeyValueContainer.h>

namespace OP::trie::containers
{
    /** Implement full-sized (256 byte) table to persist Payload by key.
    * In compare to HashTable<Payload> class doesn't use additional key manipulation
    * and provide straight access to Payload by key.
    */ 
    template <class Payload, class ParentInfo>
    struct AntiHashTable : KeyValueContainer<Payload, ParentInfo>
    {
        using this_t = AntiHashTable<Payload, ParentInfo>;
        using base_t = KeyValueContainer<Payload, ParentInfo>;

        using typename base_t::atom_t;
        using typename base_t::fast_atom_t;
        using typename base_t::dim_t;
        using typename base_t::fast_dim_t;
        using typename base_t::FarAddress;
        using typename base_t::payload_factory_t;
        using typename base_t::foreach_callback_t;

        using persisted_table_t = vtm::PersistedArray<Payload>;
        using const_persisted_table_t = vtm::ConstantPersistedArray<Payload>;

        static inline constexpr fast_dim_t capacity_c = 256;
        /**
        * \tparam some specialization of SegmentTopology with mandatory slot `HeapManagerSlot`
        */
        template <class TSegmentTopology>
        AntiHashTable(TSegmentTopology& topology, 
                const ParentInfo& node_info,
                dim_t capacity)
            : _segment_manager(topology.segment_manager())
            , _heap_manager(topology.template slot<vtm::HeapManagerSlot>())
            , _node_info(node_info)
        {
            assert(capacity == capacity_c);
        }
        
        virtual fast_dim_t capacity() const override
        {
            return capacity_c;
        }

        FarAddress create() override
        {
            constexpr auto byte_size = persisted_table_t::memory_requirement(capacity_c);

            persisted_table_t result { _heap_manager.allocate(byte_size) };
            auto* container = result.ref(_segment_manager, capacity_c);
            for(dim_t i = 0; i < capacity_c; ++i)
                container[i] = {};
            return result.address;
        }
        
        /** Destroy on persisted layer entire table block previously allocated by this #create */
        void destroy(FarAddress tbl) override
        {
            _heap_manager.deallocate(tbl);
        }
        
        KvInsert insert(
            atom_t key, payload_factory_t payload_factory, void* user_data) override
        {
            assert(!_node_info.presence(key));
            persisted_table_t ref_data(_node_info.reindex_table());
            auto& content = ref_data.ref_element(
                _segment_manager, key );
            payload_factory(content, user_data);
            return KvInsert::ok;
        }
        
        //atom_t hash(atom_t key) const 
        //{
        //    return key;
        //}

        /** Try locate index in `ref_data` by key.
        * @return index or dim_nil_c if no key contained in ref_data
        */
        dim_t find(atom_t key) const 
        {
            if(_node_info.presence(key))
                return key;
            return ~dim_t{ 0 };
        }
        
        Payload* get(atom_t key) override
        {
            if(!_node_info.presence(key))
                return nullptr;
            persisted_table_t ref_data(_node_info.reindex_table());
            return &ref_data.ref_element( _segment_manager, key );
        }
        
        std::optional<Payload> cget(atom_t key) const override
        {
            if(_node_info.presence(key)) 
            {
                const_persisted_table_t ref_data(_node_info.reindex_table());
                MemoryAlignedStorage<Payload> value;
                ref_data.ref_element(_segment_manager, key, *value.data());
                return std::optional<Payload>(std::move(*value.data()));
            }
            return std::optional<Payload>();
        }

        bool erase(atom_t key) override
        {
            assert(_node_info.presence(key));
            persisted_table_t ref_data(_node_info.reindex_table());

            auto& content = ref_data.ref_element( _segment_manager, key );
            std::destroy_at(&content);
            return true;
        }

        void foreach(foreach_callback_t callback, void* user_data) override
        {
            const_persisted_table_t ref_data(_node_info.reindex_table());
            MemoryAlignedStorage<Payload> value;
            //iterate only over occupied slots
            for(auto i = _node_info.presence_first_set();
               !vtm::is_nil(i);
               i = _node_info.presence_next_set(i))
            {
                ref_data.ref_element(_segment_manager, i, *value.data());
                if(!callback(i, *value.data(), user_data))
                    break;
            }
        }

        bool grow_from(base_t& from) override
        {
            assert(from.capacity() < capacity()); //this object must be bigger

            persisted_table_t to_ref(_node_info.reindex_table());
            auto* to_data = to_ref.ref(_segment_manager, capacity_c);

            auto move_source_item = [&](fast_atom_t key, Payload& v){
                to_data[key] = std::move(v);
            };

            using local_callback_t = decltype(&move_source_item);

            foreach_callback_t source_items_callback_adapter = +[](fast_atom_t key, Payload& value, void* user_data) -> bool{
                (*reinterpret_cast<local_callback_t>(user_data))(key, value);
                return true; 
            };

            from.foreach(source_items_callback_adapter, &move_source_item);
            return true;
        }

    private:
        
        vtm::SegmentManager& _segment_manager;
        vtm::HeapManagerSlot& _heap_manager;
        const ParentInfo& _node_info;
    
    };
}//ns: OP::trie::containers

#endif //_OP_TRIE_ANTIHASHTABLE__H_