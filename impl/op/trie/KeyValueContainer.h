#pragma once

#ifndef _OP_TRIE_KEYVALUECONTAINER__H_
#define _OP_TRIE_KEYVALUECONTAINER__H_

#include <optional>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>

namespace OP::trie::containers
{
    enum class KvInsert
    {
        ok,
        already_exists,
        need_grow
    };    

    /**
    *   \tparam Payload - value that is stored by Trie, it must be plain struct (e.g.
    *       std::is_standard_layout_v<Payload> == true).
    *   \tparam ParentInfo - information provided for internal algorithms by parent, must support:
    *       \li `bool presence(atom_t key) const` - return true if key (not a hash) presented in node;
    *       \li `dim_t capacity() const` - max allowed items in parent node;
    *       \li `FarAddress reindex_table() const` - address of this hash-table;
    */
    template <class Payload, class ParentInfo>
    struct KeyValueContainer
    {
        static_assert(std::is_standard_layout_v<Payload>, 
            "only standard-layout allowed in persisted hash-table");
        
        using base_t = KeyValueContainer<Payload, ParentInfo>;
        using atom_t = OP::common::atom_t;
        using fast_atom_t = OP::common::fast_atom_t;
        using dim_t = vtm::dim_t;
        using fast_dim_t = vtm::fast_dim_t;
        using FarAddress = vtm::FarAddress;

        using cforeach_callback_t = bool (*)(fast_atom_t key, const Payload& value, void * user_data);
        using foreach_callback_t = bool (*)(fast_atom_t key, Payload& value, void * user_data);

        virtual ~KeyValueContainer() = default;

        virtual fast_dim_t capacity() const = 0;
        /**
        * Create persisted storage 
        * @return far-address wrapped with table management interface (\sa #PersistedSizedArray)
        */
        virtual FarAddress create() = 0;

        /** @brief create and copy content from smaller container.
        *   @param from - origin table that will be replaced after call during this grow. Method doesn't destroy previous table.
        *   @param new_data - where new table WILL reside (since you cannot trust to `parent_node().reindex_table()`).
        *   @return FarAddress - new created table address. This can be vtm::is_nill, if content cannot fit into this instance capacity 
        *       and caller must re-try creation with bigger capacity.
        */
        virtual FarAddress create_from(const base_t& from) = 0;
        
        /** Destroy on persisted layer entire table block previously allocated by this #create */
        virtual void destroy(FarAddress htbl) = 0;

        /** Functor to create payload inplace only when it needed. 
        */
        using payload_factory_t = void (*)(Payload& to_construct, void* user_data);

        /**
        * Insert key to this hashtable.
        *   @param key - 1 character as a key
        *   #param payload_factory - callback interface that allows associate payload with key when it is really 
        *           needed, so it is not involved when such key already exists. Factory may have a very complicated 
        *           logic inside, for example if long chain inserted to entire trie, it can make decision don't paste
        *           value to intermedia chain.
        *   @return KvInsert enum, where:
        *           - ok - means successful insert, 
        *           - already_exists - duplicate, 
        *           - need_grow - indicate no capacity to insert new key.
        */
        virtual KvInsert insert(
            atom_t key, payload_factory_t payload_factory, void *user_data) = 0;

        /** 
        * Same as method above, but allows accept arbitrary lambda for payload creation.
        * \tparam F - functor of signature `void (Payload&)`
        */
        template <class F>
        inline KvInsert insert(atom_t key, F&& payload_factory) 
        {
            using callback_t = std::add_pointer_t<F>;
            payload_factory_t callback = +[](Payload& to_construct, void* user_data)->void{
                auto lambda = reinterpret_cast<callback_t>(user_data);
                (*lambda)(to_construct);
            };
            return this->insert(key, callback, &payload_factory);
        }

        /** 
        * @brief Locate entry in container by the key for update. 
        * @return reference. Data must exists otherwise UB.
        */
        virtual Payload& get(atom_t key) = 0;
        
        /** get RO value by key.
        *   Key must exists, otherwise UB.
        */
        virtual vtm::ReadonlyAccess<Payload> cget(atom_t key) const = 0;

        virtual bool erase(atom_t key) = 0;

        /** Iterate each immutable item in the container */
        virtual void cforeach(cforeach_callback_t, void*) const = 0;

        /** Iterate each mutable item in the container */
        virtual void foreach(foreach_callback_t, void*) = 0;
        
    };

}//ns:OP::trie::containers

#endif //_OP_TRIE_KEYVALUECONTAINER__H_
