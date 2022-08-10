#ifndef _OP_TRIE_STORECONVERTER__H_
#define _OP_TRIE_STORECONVERTER__H_

#include <cstdint>

#include <op/vtm/SegmentManager.h>
#include <op/vtm/PersistedReference.h>
#include <op/vtm/StringMemoryManager.h>

#include <op/trie/StoreConverter.h>

namespace OP::trie::store_converter
{
    /** Define type supporting conversion between persisted and user defined type.
     *  By default (when type meets condition std::is_standard_layout_v) type and 
     *  stored type are the same.
     *
     *  If for some reason you need persist some type that is not std::is_standard_layout_v
     *  (for example std::string, std::variant, ...) you can introduce specialization 
     *  of this definition. To so it you need:
     *  -# declare you specialization in the namespace `OP::trie::store_converter`:\code
     *   namespace OP::trie::store_converter{
     *   template <> struct Storage<MyType>{ ... };
     *   }//ns:OP::trie::store_converter
     *  \endcode
     * -# define c++ origin type as `using type_t = MyType;`
     * -# define storage type that is complaint with std::is_standard_layout_v: \code
     *  using storage_type_t = PersistedStateOfMyType; \endcode
     * -# define serialize method (see below for signature)
     * -# define deserialize method (see below for signature)
     */
    template <class T>
    struct Storage
    {
        using type_t = T;
        using storage_type_t = T;

        template <class TTopology>
        static void serialize(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            dest = source;
        }

        template <class TTopology>
        static void reassign(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            dest = source;
        }

        template <class TTopology>
        static const type_t& deserialize(TTopology& topology, const storage_type_t& storage)
        {
            return storage;
        }

        template <class TTopology>
        static void destroy(TTopology& topology, storage_type_t& dest)
        {
            std::destroy_at(&dest);
        }
    };

    /** 
    * Specialization of storage for std::string. STL implementation of std::string 
    * is not std::is_standard_layout_v so smm::SmartStringAddress used instead 
    */
    template <>
    struct Storage<std::string>
    {
        using type_t = std::string;

        using storage_type_t = smm::SmartStringAddress;

        template <class TTopology>
        static void serialize(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            StringMemoryManager memmngr(topology);
            dest = memmngr.smart_insert(source);
        }

        template <class TTopology>
        static void reassign(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            StringMemoryManager memmngr(topology);
            if(!dest.is_nil()) 
                memmngr.destroy(dest);    
            dest = memmngr.smart_insert(source);
        }

        template <class TTopology>
        static type_t deserialize(TTopology& topology, const storage_type_t& source)
        {
            StringMemoryManager memmngr(topology);
            type_t result;
            memmngr.get(source, std::back_inserter(result));
            return result;
        }

        template <class TTopology>
        static void destroy(TTopology& topology, storage_type_t& dest)
        {
            if(!dest.is_nil()) 
            {
                StringMemoryManager memmngr(topology);
                memmngr.destroy(dest);    
                dest = {};
            }
        }

    };

    /** 
    * Specialization of storage for std::monostate. Just to allow std::variant
    * keep empty 
    */
    template <>
    struct Storage<std::monostate>
    {
        using type_t = std::monostate;

        struct dummy{};
        using storage_type_t = dummy;

        template <class TTopology>
        static void serialize(TTopology& topology, const type_t&, storage_type_t& )
        {
        }

        template <class TTopology>
        static void reassign(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
        }

        template <class TTopology>
        static type_t deserialize(TTopology& topology, const storage_type_t& source)
        {
            return type_t{};
        }

        template <class TTopology>
        static void destroy(TTopology& topology, storage_type_t& dest)
        {
            std::destroy_at(&dest);
        }
    };

    template <class ...Tx>
    struct PersistedVariant
    {
        static_assert(sizeof...(Tx) < 256, "persistance allowed no more than 256 variant types");
        using this_t = PersistedVariant<Tx...>;

        constexpr static size_t buffer_size_c =
            std::max({ OP::utils::memory_requirement<Tx>::requirement ... });

        constexpr static size_t align_size_c =
            std::max({ alignof(Tx) ... });

        template <class T>
        using is_member = std::disjunction< std::is_same<T, Tx> ...>;

        template <class T>
        inline static constexpr uint8_t type_index()
        {
            return static_cast<std::uint8_t>(
                index_of<T>(std::make_index_sequence<sizeof ...(Tx)>{}));
        }

        template <class T, std::enable_if_t<is_member<T>::value, void*> = nullptr>
        PersistedVariant(T&& t)
            : _selector(index_of<T>(std::make_index_sequence<sizeof ...(Tx)>{}))
        {
            using arg_t = std::decay_t<T>;
            *std::launder(reinterpret_cast<arg_t*>(_data)) = t;
        }

        PersistedVariant() = default;

        //template <std::enable_if_t<is_member<std::monostate>::value, void*> = nullptr>
        //PersistedVariant()
        //    : _selector(index_of<std::monostate>(std::make_index_sequence<sizeof ...(Tx)>{}))
        //{}

        std::uint8_t _selector;
        alignas(align_size_c) std::byte _data[buffer_size_c];

    private:
        template <class T, size_t ... I>
        constexpr static size_t index_of(std::index_sequence<I...>)
        {
            return ((std::is_same_v<T, Tx> ? I : 0) | ...);
        }
    };

    /** 
    * Specialization of storage for std::variant. STL implementation of std::variant
    * is not std::is_standard_layout_v so PersistedVariant used instead. Implementation
    * uses nested type lookup to find best cast operation to persist inner types. For
    * example use std::variant<std::string> is permitted since Storage<std::string>
    *  specialization can be used 
    */
    template <class ...Tx>
    struct Storage<std::variant<Tx...>>
    {
        using this_t = Storage<std::variant<Tx...>>;

        using type_t = std::variant<Tx...>;

        using storage_type_t = PersistedVariant<
            typename Storage<Tx>::storage_type_t ...>;

        template <class TTopology>
        static void serialize(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            std::visit([&](const auto& v) {
                using arg_t = std::decay_t<decltype(v)>;
                using dest_item_t = typename Storage<arg_t>::storage_type_t;
                dest._selector = storage_type_t::template type_index< dest_item_t>();
                assert(source.index() == dest._selector);
                Storage<arg_t>::serialize(
                    topology, v, *std::launder(reinterpret_cast<dest_item_t*>(dest._data)));
                }, source);
        }

        template <class TTopology>
        static void reassign(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            std::visit([&](const auto& v) {
                using arg_t = std::decay_t<decltype(v)>;
                using dest_item_t = typename Storage<arg_t>::storage_type_t;
                constexpr auto new_selector = storage_type_t::template type_index< dest_item_t>();
                dest_item_t* persisted_data = std::launder(reinterpret_cast<dest_item_t*>(dest._data));
                if(dest._selector == new_selector) //type is not changing, delegate to nested reassign
                {
                    Storage<arg_t>::reassign(topology, v, *persisted_data);
                }
                else //type has been changed destroy/serialize
                { 
                    destroy_impl(topology, dest);
                    dest._selector = new_selector;
                    Storage<arg_t>::serialize(topology, v, *persisted_data);
                }
            }, source);
                
        }

        template <class TTopology>
        static type_t deserialize(TTopology& topology, const storage_type_t& source)
        {
            using narrow_conv_t = type_t(*)(TTopology& topology, const storage_type_t&);
            constexpr static std::array< narrow_conv_t, sizeof...(Tx)> conv = {
                narrow_converter<Tx, TTopology> ...
            };
            return conv[source._selector](topology, source);
        }

        template <class TTopology>
        static void destroy(TTopology& topology, storage_type_t& dest)
        {
            destroy_impl(topology, dest);
            std::destroy_at(&dest); //call destructor for PersistedVariant
        }

    private:
        template <class V, class TTopology>
        static type_t narrow_converter(TTopology& topology, const storage_type_t& storage)
        {
            using storage_narrow_t = typename Storage<V>::storage_type_t;
            return Storage<V>::deserialize(topology, 
                *reinterpret_cast<const storage_narrow_t*>(storage._data));
        }
        template <class V, class TTopology>
        static void narrow_destroyer(TTopology& topology, storage_type_t& storage)
        {
            using arg_t = std::decay_t<V>;
            using dest_item_t = typename Storage<arg_t>::storage_type_t;
            dest_item_t* persisted_data = 
                std::launder(reinterpret_cast<dest_item_t*>(storage._data));
            Storage<arg_t>::destroy(topology, *persisted_data);
        }

        template <class TTopology>
        static void destroy_impl(TTopology& topology, storage_type_t& dest)
        {
            using destroyer_t = void (*)(TTopology&, storage_type_t&);
            constexpr static std::array< destroyer_t, sizeof...(Tx)> destroyers =
            {
                this_t::narrow_destroyer<Tx, TTopology>...
            };
            assert(dest._selector < sizeof...(Tx));
            destroyers[dest._selector](topology, dest);
        }
    };

    //--

template <class ... Tx>
struct PersistedTuple;
    
template <class T, class ... Tx>
struct PersistedTuple<T, Tx...>
{
    T _value;
    PersistedTuple<Tx...> _continuation;
};

template <>
struct PersistedTuple<>
{
};

template <class T>
struct PersistedTuple<T>
{
    T _value;
};

template <size_t I, class ...Tx>
auto& pt_get(PersistedTuple<Tx...>& tup)
{
    if constexpr(I == 0)
        return tup._value;
    else
        return pt_get<I-1>(tup._continuation);
}
template <size_t I, class ...Tx>
const auto& pt_get(const PersistedTuple<Tx...>& tup)
{
    if constexpr (I == 0)
        return tup._value;
    else
        return pt_get<I - 1>(tup._continuation);
}


    //--
    template <class ...Tx>
    struct Storage<std::tuple<Tx...>>
    {
        using this_t = Storage<std::tuple<Tx...>>;

        using type_t = std::tuple<Tx...>;

        using storage_type_t = PersistedTuple<
            typename Storage<Tx>::storage_type_t ...>;

        template <class TTopology>
        static void serialize(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            serialize_impl(topology, source, dest, 
                  std::make_index_sequence<sizeof ...(Tx)>{});
        }

        template <class TTopology>
        static void reassign(TTopology& topology, const type_t& source, storage_type_t& dest)
        {
            reassign_impl(topology, source, dest, 
                  std::make_index_sequence<sizeof...(Tx)>{});
        }

        template <class TTopology>
        static type_t deserialize(TTopology& topology, const storage_type_t& source)
        {
            return deserialize_impl(topology, source, std::make_index_sequence<sizeof...(Tx)>{});
        }

        template <class TTopology>
        static void destroy(TTopology& topology, storage_type_t& dest)
        {
            destroy_impl(topology, dest, std::make_index_sequence<type_t>{});
            std::destroy_at(&dest); //call destructor for PersistedTuple
        }
    private:
        template <class TTopology, size_t ... I>
        static void serialize_impl(TTopology& topology, const type_t& src, storage_type_t& dest, std::index_sequence<I...>)
        {
            (Storage<std::tuple_element_t<I, type_t>>::serialize(
                    topology, std::get<I>(src), pt_get<I>(dest)), ...);
        }
        template <class TTopology, size_t ... I>
        static void reassign_impl(TTopology& topology, const type_t& src, storage_type_t& dest, std::index_sequence<I...>)
        {
            (Storage<std::tuple_element_t<I, type_t>>::reassign(
                    topology, std::get<I>(src), pt_get<I>(dest)), ...);
        }
        template <class TTopology, size_t ... I>
        static type_t deserialize_impl(TTopology& topology, const storage_type_t& source, std::index_sequence<I...>)
        {
            return std::make_tuple (
                Storage<std::tuple_element_t<I, type_t>>::deserialize(
                    topology, pt_get<I>(source)) ...);
        }
        template <class TTopology, size_t ... I>
        static void destroy_impl(TTopology& topology, storage_type_t& dest, std::index_sequence<I...>)
        {
            (Storage<std::tuple_element_t<I, type_t>>::destroy(topology, pt_get<I>(dest)), ...);
        }
    };

    
}//ns:OP::trie::store_converter
    
#endif //_OP_TRIE_STORECONVERTER__H_
