#ifndef _OP_TRIE_UTILS__H_
#define _OP_TRIE_UTILS__H_

#include <type_traits>
#include <tuple>
#include <atomic>           
#include <variant>
#include <op/common/typedefs.h>


namespace OP
{
    namespace utils
    {
        namespace details
        {
            template <class T, class U>
            constexpr bool any_step(T t, U u) noexcept
            {
                if constexpr (std::is_same_v<T, U>)
                    return t == u;
                else
                    return false;
            }
        }
        /** Check if argument value of type `T` is containing in the parameter pack `auto ... c`.
        *   For example may be used with enums (pay attention to mixing of several enums in the same 
        *       query Enum1, Enum2):
        *   \code
        *   enum class Enum1 {e1_1, e1_2};
        *   enum class Enum2 {e2_1, e2_2};
        *   ...
        *   assert( OP::utils::any_of<Enum1::e1_1, Enum2::e2_1>( Enum1::e1_1 ) );
        *   assert( !OP::utils::any_of<Enum1::e1_1, Enum2::e2_1>( Enum2::e2_2 ) );
        *   \endcode
        *   \tparam ... check_c - any type constants supporting operator `==`;
        *   \tparam T arbitrary type to check against pack of constants
        *   \return true when at least one constant is matched by type and value to the argument `T t`
        */
        template <auto ... check_c, class T>
        constexpr bool any_of(T t) noexcept
        {
            return (details::any_step(t, check_c) || ...);
        }

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

        /** Umbrela definition allows check if type container contains element
            of specified type.
        \tparam T - check presence of this type;
        \tparam TypeContainer - type container. Supported:
            \li std::tuple
            \li std::variant
        */
        template <typename T, typename TypeContainer>
        struct contains_type;

        template <typename T, typename... Us>
        struct contains_type<T, std::tuple<Us...>> : std::disjunction<std::is_same<T, Us>...> {};

        template <typename T, typename... Us>
        struct contains_type<T, std::variant<Us...>> : std::disjunction<std::is_same<T, Us>...> {};

        template <typename... Us>
        constexpr inline bool contains_type_v = contains_type<Us...>::value;

        template <typename ...Tx>
        struct tuple_merge
        {
            using type = std::tuple<Tx...>;
        };

        template <typename ...Tx, typename ... Ux>
        struct tuple_merge<std::tuple<Tx...>, std::tuple<Ux...>>
        {
            using type = std::tuple<Tx..., Ux...>;
        };

        template <typename T, typename ... Ux>
        struct tuple_merge<T, std::tuple<Ux...>>
        {
            using type = std::tuple<T, Ux...>;
        };

        template <typename ... Ux, typename T>
        struct tuple_merge<std::tuple<Ux...>, T>
        {
            using type = std::tuple<Ux..., T>;
        };

        /** helper to infer type of 2 tuples after tuple_cat */
        template <class ...Tx>
        using tuple_merge_t = typename tuple_merge<Tx...>::type;//decltype(std::tuple_cat(std::declval<Tup1>(), std::declval<Tup2>()));


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
        *   Type filter may be applied to create sub-type from origin std::tuple by applying compile-time
        * predicate to each entry.
        *
        * Use-case: need to get commands with 0 input args only \code
        * using origin_t = std::tuple< Command1, Command2, ...>;
        * \endcode
        * Predicate must be declared as \code
        * struct Predicate_Zero_Arg_Command{
        *     template <class T>
        *     //value must be evaluated on compile time
        *     static constexpr bool check = (T::arity_c == 0);
        * }; \endcode
        * Now it is possible to create sub-type:\code
        * using zero_args_only_t = typename TypeFilter<Predicate_Zero_Arg_Command, origin_t>::type;
        * \endcode
        */
        template <class P, class ... Tx>
        struct TypeFilter;

        template <class P, class T, class ... Tx  >
        struct TypeFilter<P, T, Tx ...>
        {
            using type = std::conditional_t< P::template check<T>,
                tuple_merge_t<T, typename TypeFilter<P, Tx ...>::type>,
                typename TypeFilter<P, Tx ...>::type >;
        };

        template <class P>
        struct TypeFilter<P>
        {//specialization for empty tuple
            using type = std::tuple<>;
        };
        template <class P, class ... Tx>
        struct TypeFilter<P, std::tuple<Tx...> >
        {//specialization to accept tuples
            using type = typename TypeFilter<P, Tx ...>::type;
        };

        /** shortcut for `typename TypeFilter<P, Tx...>::type` */
        template <class P, class ... Tx>
        using type_filter_t = typename TypeFilter<P, Tx...>::type;

        /**Utility used with TypeFilter to allow apply 'not' to other filter predicate*/
        template <class Predicate>
        struct filter_not
        {
            template <class T>
            static constexpr bool check = !(Predicate::template check<T>);
            
        };

        /**Utility used with TypeFilter to allow check if type exactly match `Expected`*/
        template <class Expected>
        struct filter_exact_match
        {
            template <class T>
            static constexpr bool check = std::is_same_v<Expected, T>;
        };


        /** compile time return true if applying TypeFilter<P, Tx ...> contains any record */
        template <class P, class ... Tx>
        constexpr bool any_typefilter_match() noexcept
        {
            return 0 < std::tuple_size_v<typename TypeFilter<P, Tx ...>::type>;
        }

        /** 
        * Umbrela template to iterate all types containing in the source std::tuple or std::variant and creates new type
        * containing new types defined by TMapFunction
        * For toy example imagine change definition of std::tuple<int, float> to std::tuple<std::string, double>.
        * \tparam  TMapFunction declared as structure with 1 type definition like: \code
        * struct MyMapFunction{
        * ...
        * template <class T>
        * using map_t = ... //whatever to map from T to other type 
        * };
        * \endcode
        * Just for example, following allows map type to 2 choices (int or string): \code
        *   map_t = std::conditional_t<std::is_arithmetic_v<T>, int, std::string>;
        * \endcode 
        *
        */
        template <class TMapFunction, class ... Tx>
        struct MapType;

        template <class TMapFunction, class ... Tx  >
        struct MapType<TMapFunction, std::tuple<Tx ...>>
        {
            using type = std::tuple< typename TMapFunction::template map_t<Tx>... >;
        };

        template <template <typename> typename U>
        struct SimpleTypeMap
        {
            template <typename T>
            using map_t = U<T>;
        };


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
        
        /** Priority-tag is used to simplify compiler select between two or more methods. For example you have 
        * following definitions:
        * \code
        *   template <class T>
        *   void f(const T& t, other args...){...}
        *
        *   template <class T, std::enable_if_t<std::is_base_of_v<SomeBase, T>, int> = 0>
        *   void f(const T& t, other args...){...}
        * \endcode
        * Sometimes (influencing of `other args`) f<T>() may not be selected in the expected way.
        * To help this you can use `priority_tag<0>` - for lowest priority selection, `priority_tag<1>` for regular
        * priroriy and during call provide `priority_tag<2>`.
        * For example: \code
        *   template <class T>
        *   void f(const T& t, other args..., priority_tag<0> = {} ){...} //let compiler know match this in last order.
        *
        *   template <class T, std::enable_if_t<std::is_base_of_v<SomeBase, T>, int> = 0>
        *   void f(const T& t, other args..., priority_tag<1> = {} ){...} //let compiler know to match this before `0`.
        *   ...
        *   //later in call:
        *   f(args..., priority_tag<2>{});
        * \endcode
        */
        template<unsigned N>
        struct priority_tag : priority_tag<N - 1> {};
        template<> struct priority_tag<0> {};

   } //utils
} //OP
#endif //_OP_TRIE_UTILS__H_
