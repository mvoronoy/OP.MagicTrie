#ifndef _OP_TRIE_UTILS__H_
#define _OP_TRIE_UTILS__H_

#include <type_traits>
#include <tuple>
#include <atomic>
#include <op/common/typedefs.h>


namespace OP
{
    namespace utils
    {
        /**
        *   Allows get index of type in variadic template parameters.
        * Usage: \code
        * static_assert(0 == get_type_index<char, char, int, void>::value, "");
        * static_assert(1 == get_type_index<int, char, int, void>::value, "");
        * static_assert(2 == get_type_index<void, char, int, void>::value, "");
        * \endcode
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
            explicit ext_tuple(Args&& ... args):
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
        /**
        *   Access tuple element by type instead index. In compare with get_type_index
        *   this indexer allows to operate over tuple explictly.
        *   Usage:\code
        *   void my_function(std::tuple<A, B, C>& values)
        *   {
        *       std::cout << "A index:" << tuple_ref_index<A, decltype(values)>::value << '\n';//0 is printed
        *       std::cout << "B index:" << tuple_ref_index<B, decltype(values)>::value << '\n';//1 is printed
        *       std::cout << "C index:" << tuple_ref_index<C, decltype(values)>::value << '\n';//2 is printed
        *   }
        *   \endcode
        */
        template< class T, class Tuple >
        struct tuple_ref_index;

        // recursive case
        template<class T, class Head, class... Tail >
        struct tuple_ref_index<T, std::tuple<Head, Tail...> >  
             : tuple_ref_index<T, std::tuple<Tail...> >
        { 
            enum { value = tuple_ref_index<T, std::tuple<Tail...>>::value + 1 };
        };

        template<class T, class... Tail >
        struct tuple_ref_index<T, std::tuple<T, Tail...> >  
        { 
            enum { value = 0 };
        };
        /** Simple accessor to the tuple entry by type. Usage:\code
        *   void my_function(std::tuple<int, double, std::string>& values)
        *   {
        *       std::cout << "Ref int:" << tuple_ref<int>(values) << '\n';//Some int is printed
        *       std::cout << "Ref double:" << tuple_ref<double>(values) << '\n';//Some double is printed
        *       std::cout << "Ref string:" << tuple_ref<std::string>(values).c_str() << '\n';//Some string is printed
        *   }
        *\endcode
        */
        template <class T, class Tuple>
        inline OP_CONSTEXPR(OP_EMPTY_ARG) T&& tuple_ref(Tuple&& tuple) OP_NOEXCEPT
        {
            return std::forward<T&&>(
                std::get< tuple_ref_index<T, Tuple>::value >(std::forward<Tuple>(tuple)) );
        }
        template <class T, class Tuple>
        inline constexpr T& tuple_ref(Tuple& tuple) OP_NOEXCEPT
        {
            return std::get< tuple_ref_index<T, Tuple>::value >(tuple);
        }
        template <class T, class Tuple>
        inline constexpr const T& tuple_ref(const Tuple& tuple) OP_NOEXCEPT
        {
            return std::get< tuple_ref_index<T, Tuple>::value >(tuple);
        }


        /**Allows to check if parameter T is defined from other template. Usage:
        *   \code
        *   typedef std::tuple<int, double> my_tuple; //sample type defined from template
        *   ...
        *   std::cout << is_generic<my_tuple, std::tuple>::value; //is true
        *   \endcode
        */
        template <typename T, template <typename...> class Template>
        struct is_generic : public std::false_type {};
        template <template <typename...> class Template, typename... Args>
        struct is_generic<Template<Args...>, Template> : public std::true_type {};

        /**
        * Allow evaluate sizeof for type (including tuples) alligned to current compiler requirements.
        *   Usage:\code
        *      memory_requirement<std::tuple<int, double> > :: requirement //byte-size of entire tuple
        *      memory_requirement< A > :: requirement //byte-size of struct A
        * \endcode
        */
        template <class T, size_t N = 1>
        struct memory_requirement
        {
            constexpr static std::uint32_t element_size_c =
                static_cast<std::uint32_t>(
                    ((sizeof(T) + alignof(T) - 1) / alignof(T)) * alignof(T));

            constexpr static std::uint32_t requirement = element_size_c * N;

            template <typename Int>
            static constexpr Int array_size(Int n)
            {
                return static_cast<Int>(element_size_c) * n;
            }
        };

        template <class T, class Y>
        OP_CONSTEXPR(OP_EMPTY_ARG) inline T align_on(T address, Y base)
        {
            return ((address + base - 1) / base)*base;//(address % base) ? (address + (base - (address % base))) : address;
        }
        
        template <class T, class Y>
        OP_CONSTEXPR(OP_EMPTY_ARG) inline std::uint32_t aligned_sizeof(Y base)
        {
            return align_on(static_cast<std::uint32_t>(memory_requirement<T>::requirement), base);
        }

        template <class T, class Y>
        inline bool is_aligned(T address, Y base)
        {
            return ((size_t)(address) % base) == 0;
        }
        
   } //utils
} //OP
#endif //_OP_TRIE_UTILS__H_
