#pragma once

#ifndef _OP_TRIE_KEYVALUECONTAINER__H_
#define _OP_TRIE_KEYVALUECONTAINER__H_

#include <optional>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>

namespace OP::trie::containers
{
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
        using dim_t = vtm::dim_t;
        using FarAddress = vtm::FarAddress;

        virtual ~KeyValueContainer() = default;
        /**
        * Create persisted storage 
        * @return far-address wrapped with table management interface (\sa #PersistedSizedArray)
        */
        virtual FarAddress create() = 0;
        
        /** Destroy on persisted layer entire table block previously allocated by this #create */
        virtual void destroy(FarAddress htbl) = 0;

        /** Functor interface to create payload only when it needed. Such complicated decision is needed
        * since c++ lambda allows cast to function pointer only without capture [see standard 5.1.2].
        * To make it simpler additional light-weight structure introduce 1 method interface. To make 
        * operation transparent, this class also appends `insert` method with arbitrary template lambda.
        */
        struct FPayloadFactory
        {
            virtual void inplace_construct(Payload& to_construct) const = 0;    
        };

        /**
        * Insert key to this hashtable.
        *   @param key - 1 character as a key
        *   #param payload_factory - callback interface that allows associate payload with key when it is really 
        *           needed, so it is not involved when such key already exists. Factory may have a very complicated 
        *           logic inside, for example if long chain inserted to entire trie, it can make decision don't paste
        *           value to intermedia chain.
        *   @return insert position or #end() if no more capacity
        */
        virtual std::pair<dim_t, bool> insert(
            atom_t key, const FPayloadFactory& payload_factory) = 0;

        /** 
        * Same as method above, but allows accept arbitrary lambda for payload creation.
        * \tparam F - functor of signature `void (Payload&)`
        */
        template <class F>
        inline std::pair<dim_t, bool> insert(
            atom_t key, F payload_factory) 
        {
            using callback_t = std::decay_t<F>;
            PayloadFactoryImpl<F> callback(std::move(payload_factory));
            FPayloadFactory& casted_callback = callback;
            return this->insert(key, casted_callback);
        }

        virtual atom_t hash(atom_t key) const = 0;
        /** Given origin key returns position in `persisted_table_t` where really key is located 
        * for "production" mode this method never fails because reindex called for existing keys only
        */
        virtual atom_t reindex(atom_t key) const = 0;
        /** Try locate index in `ref_data` by key.
        * @return index or dim_nil_c if no key contained in ref_data
        */
        virtual dim_t find(atom_t key) const = 0;

        /** Same as #find but return pointer to payload or nullptr */
        virtual Payload* get(atom_t key) = 0;
        
        virtual std::optional<Payload> cget(atom_t key) const = 0;

        virtual bool erase(atom_t key) = 0;

        virtual bool grow_from(base_t& from, FarAddress& result) = 0;
        
    protected:
        /** 
        *   Simple implementation of FPayloadFactory that leverages arbitrary lambda to implement payload creation 
        *   callback
        */
        template <class F>
        struct PayloadFactoryImpl : FPayloadFactory
        {
            PayloadFactoryImpl(F factory)
                : _factory(std::move(factory))
            {};
            void inplace_construct(Payload& to_construct) const override    
            {
                _factory(to_construct);
            }
            F _factory;
        };

        template <class U>
        static auto make_payload_factory(U&& factory)
        {
            using functor_t = std::decay_t<U>;
            return PayloadFactoryImpl<functor_t>(std::forward<U>(factory));
        }
    };

}//ns:OP::trie::containers

#endif //_OP_TRIE_KEYVALUECONTAINER__H_
