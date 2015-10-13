#ifndef _OP_TRIE_UTILS__H_
#define _OP_TRIE_UTILS__H_

#include <type_traits>
#include <tuple>

#ifdef _MSC_VER
    #if _MSC_VER <= 1800
        #define OP_CONSTEXPR 
    #else
        #define OP_CONSTEXPR constexpr
    #endif
#else
    #define OP_CONSTEXPR constexpr
#endif //

namespace OP
{
    namespace trie
    {
        /**
        *   Allows get index of type in variadic template parameters.
        * Usage:
        * static_assert(0 == get_type_index<char, char, int, void>::value, "");
        * static_assert(1 == get_type_index<int, char, int, void>::value, "");
        * static_assert(2 == get_type_index<void, char, int, void>::value, "");
        */
        template <typename T, typename... Ts> struct get_type_index;
         
        template <typename T, typename... Ts>
        struct get_type_index<T, T, Ts...> : std::integral_constant<std::size_t, 0> {};
         
        template <typename T, typename Tail, typename... Ts>
        struct get_type_index<T, Tail, Ts...> :
            std::integral_constant<std::size_t, 1 + get_type_index<T, Ts...>::value> {};

        /**
        *   Aggregates std::tuple and get access by type of tuple entry instead of index
        */ 
        template <typename ... Ts>
        struct ext_tuple
        {
            typedef std::tuple<Ts...> tuple_t;

            template <class ...Args>
            ext_tuple(Args&& ... args):
                _instance(std::forward<Args>(args)...)
            {
            }
            template <class T>
            T& get() 
            {
                return std::get< get_type_index<T, Ts...>::value >(
                    _instance );    
            }
            template <class T>
            const T& get()  const
            {
                return std::get< get_type_index<T, Ts...>::value >(
                    _instance );    
            }

            tuple_t& tuple()
            {
                return _instance;
            }
            const tuple_t& tuple() const
            {
                return _instance;
            }
        private:
            tuple_t _instance;
        };
        //////////////////////////////////
        /**
        *   Wrapper to grant RAI operation by invoking pair of methods
        */
        template < class T, 
            void (T::*start_op)(), 
            void (T::*end_op)() >
        struct operation_guard_t
        {
            operation_guard_t(T* ref) :
                _ref(ref),
                _is_closed(false)
            {
                (_ref->*start_op)();
            }
            void close()
            {
                _is_closed = true;
            }
            ~operation_guard_t()
            {
                if (!_is_closed)
                    (_ref->*end_op)();
            }
        private:
            T* _ref;
            bool _is_closed;
        };

    } //trie
} //OP
#endif //_OP_TRIE_UTILS__H_
