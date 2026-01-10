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
        static inline constexpr dim_t capacity_c = 256;
        using this_t = AntiHashTable<Payload, ParentInfo>;
        using base_t = KeyValueContainer<Payload, ParentInfo>;
        using FarAddress = vtm::FarAddress;

        using persisted_table_t = vtm::PersistedArray<Payload>;
        using const_persisted_table_t = vtm::ConstantPersistedArray<Payload>;
        using payload_factory_t = typename base_t::FPayloadFactory;

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
        
        std::pair<dim_t, bool> insert(
            atom_t key, const payload_factory_t& payload_factory) override
        {
            assert(!_node_info.presence(key));
            persisted_table_t ref_data(_node_info.reindex_table());
            auto& content = ref_data.ref_element(
                _segment_manager, key );
            payload_factory.inplace_construct(content);
            return std::pair<dim_t, bool>(key, true);
        }
        
        atom_t hash(atom_t key) const override
        {
            return key;
        }

        atom_t reindex(atom_t key) const override
        { // 256-wide table doesn't need any reindex op
            assert(_node_info.presence(key));
            return key;
        }

        /** Try locate index in `ref_data` by key.
        * @return index or dim_nil_c if no key contained in ref_data
        */
        virtual dim_t find(atom_t key) const override
        {
            if(_node_info.presence(key))
                return key;
            return vtm::dim_nil_c;
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
                auto refview = ref_data.ref_element(_segment_manager, key);
                return std::optional<Payload>(*refview);
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

         bool grow_from(base_t& from, FarAddress& result) override
         {
            assert(_node_info.capacity() < capacity_c); //this object must be bigger
            result = create();

            using namespace details;
            persisted_table_t to_ref(result);
            auto* to_data = to_ref.ref(_segment_manager, capacity_c);

            auto i = _node_info.presence_first_set();
            
            //iterate only over occupied slots
            for(atom_t key = static_cast<atom_t>(i);
                i != vtm::dim_nil_c;
                i = _node_info.presence_next_set(key), 
                key = static_cast<atom_t>(i))
            {
                auto *v = from.get(key);
                assert(v); //must exists since presence() == true
                to_data[i] = std::move(*v);
            }
            return true;
         }

    private:
        vtm::SegmentManager& _segment_manager;
        vtm::HeapManagerSlot& _heap_manager;
        const ParentInfo& _node_info;
    
    };
}//ns: OP::trie::containers

#endif //_OP_TRIE_ANTIHASHTABLE__H_