#ifndef _OP_TRIE_ANTIHASHTABLE__H_
#define _OP_TRIE_ANTIHASHTABLE__H_

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>

#include <op/trie/TrieOptions.h>
#include <op/trie/KeyValueContainer.h>

namespace OP::trie::containers
{
    /** Implement full-size (256 byte) table to persist Payload by key.
    * In compare to BitIndexedPayload class doesn't use additional key manipulation
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
        using typename base_t::cforeach_callback_t;
        using typename base_t::foreach_callback_t;

        using persisted_table_t = vtm::PersistedArray<Payload>;
        using const_persisted_table_t = vtm::ConstantPersistedArray<Payload>;

        static inline constexpr fast_dim_t capacity_c = 256;
        /**
        * \tparam some specialization of SegmentTopology with mandatory slot `HeapManagerSlot`
        */
        AntiHashTable(TrieOptions& options, 
                const ParentInfo& node_info)
            : _options(options)
            , _parent_node(node_info)
        {
        }
        
        virtual fast_dim_t capacity() const override
        {
            return capacity_c;
        }

        FarAddress create() override
        {
            FarAddress result =
                _options.allocator_service().allocate_for<DataVector>();
            _options.memory_service().make_new<DataVector>(result);
            return result;
        }

        virtual FarAddress create_from(const base_t& from) override
        {
            assert(from.capacity() < capacity()); //this object must be bigger
            FarAddress residence = _options.allocator_service().allocate_for<DataVector>();
            auto data_vector = _options.memory_service().make_new<DataVector>(residence);

            auto move_source_item = [&](fast_atom_t key, const Payload& v){
                data_vector->_payload[key] = v;
            };

            using local_callback_t = decltype(&move_source_item);

            from.cforeach(source_items_callback_adapter<local_callback_t>, &move_source_item);
            return residence;
        }
        
        /** Destroy on persisted layer entire table block previously allocated by this #create */
        void destroy(FarAddress tbl) override
        {
            _options.allocator_service().deallocate(tbl);
        }
        
        KvInsert insert(
            atom_t key, payload_factory_t payload_factory, void* user_data) override
        {
            assert(!_parent_node.presence(key));
            auto wr_element = _options.memory_service().access_element<Payload>(
                _parent_node.reindex_table() + data_offset_c, key);
            auto& content = *wr_element;
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
            if(_parent_node.presence(key))
                return key;
            return ~dim_t{ 0 };
        }
        
        Payload& get(atom_t key) override
        {
            assert(_parent_node.presence(key));
            return *_options.memory_service().access_element<Payload>(
                _parent_node.reindex_table() + data_offset_c, key);
        }
        
        vtm::ReadonlyAccess<Payload> cget(atom_t key) const override
        {
            assert(_parent_node.presence(key));        
            return _options.memory_service().view_element<Payload>(
                _parent_node.reindex_table() + data_offset_c, key);
        }

        bool erase(atom_t key) override
        {
            assert(_parent_node.presence(key));
            auto element = _options.memory_service().access_element<Payload>(
                _parent_node.reindex_table() + data_offset_c, key);
            std::destroy_at(&element);
            return true;
        }

        void cforeach(cforeach_callback_t callback, void* user_data) const override
        {
            auto ro_data = _options.memory_service().view<DataVector>(_parent_node.reindex_table());
            const auto* payload = ro_data->_payload;
            //iterate only over occupied slots
            for(auto i = _parent_node.presence_first_set();
               !vtm::is_nil(i);
               i = _parent_node.presence_next_set(i))
            {
                if(!callback(i, payload[i], user_data))
                    break;
            }
        }

        void foreach(foreach_callback_t callback, void* user_data) override
        {
            auto wr_data = _options.memory_service().access<DataVector>(_parent_node.reindex_table());
            auto* payload = wr_data->_payload;

            for(auto i = _parent_node.presence_first_set();
                !vtm::is_nil(i);
                i = _parent_node.presence_next_set(i))
            {
                //allow early exit
                if (!callback(static_cast<atom_t>(i), payload[i], user_data))
                    break;
            }
        }

    private:                                                        
        struct DataVector
        {
            Payload _payload[capacity_c] = {};
        };
        constexpr static inline vtm::segment_pos_t data_offset_c = offsetof(DataVector, _payload);


        template <class FCallback>
        static bool source_items_callback_adapter(fast_atom_t key, const Payload& value, void* user_data)
        {
            (*reinterpret_cast<FCallback>(user_data))(key, value);
            return true;
        }
        TrieOptions& _options;
        const ParentInfo& _parent_node;
    
    };
}//ns: OP::trie::containers

#endif //_OP_TRIE_ANTIHASHTABLE__H_