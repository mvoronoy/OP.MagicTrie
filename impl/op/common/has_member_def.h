#pragma once
#ifndef _OP_COMMON_HAS_MEMBER_DEF__H_
#define _OP_COMMON_HAS_MEMBER_DEF__H_

/** 
    Define macro to create compile-time checker of specific method name in arbitrary class.
    For example you need check if some class has `size` method. To accomplish define somewhere 
    checker `OP_DECLARE_CLASS_HAS_MEMBER(size)`. Later use:
    ~~~~~~~~~~~~~~~~~~~~~~
    has_size<std::string>::value //evaluates true since exists std::string::size()    
    ~~~~~~~~~~~~~~~~~~~~~~
*/
#define OP_DECLARE_CLASS_HAS_MEMBER(Member)  template <typename A>\
    class has_##Member \
    { \
        typedef char YesType[1]; \
        typedef char NoType[2]; \
        template <typename C> static YesType& test( decltype(&C::Member) ) ; \
        template <typename C> static NoType& test(...); \
    public: \
        enum { value = sizeof(test<A>(nullptr)) == sizeof(YesType) }; \
    };

/** 
    Define macro to create compile-time checker of specific type definition in arbitrary class.
    For example you need check if some class has `iterator` definition. To accomplish define somewhere 
    checker `OP_DECLARE_HAS_TYPEDEF(iterator)`. Later use:
    ~~~~~~~~~~~~~~~~~~~~~~
    has_typedef_iterator<std::vector<int>>::value //evaluates true since exists std::vector<int>::iterator
    ~~~~~~~~~~~~~~~~~~~~~~
*/
#define OP_DECLARE_HAS_TYPEDEF(type_name) template <class T> \
    class has_typedef_##type_name {\
        typedef char YesType[1]; \
        typedef char NoType[2]; \
        template <typename U> static YesType& test( typename U::type_name*  ) ; \
        template <typename U> static NoType& test(...); \
    public: \
            enum { value = sizeof(test<T>(nullptr)) == sizeof(YesType) }; \
        };

#define OP_CHECK_CLASS_HAS_MEMBER_TYPE(Class, Member) details::has_##Member<Class>

/** Complimentar to OP_DECLARE_CLASS_HAS_MEMBER - generate compile time const that indicate if class `A` has member `Member`*/
#define OP_CHECK_CLASS_HAS_MEMBER(Class, Member) (OP_CHECK_CLASS_HAS_MEMBER_TYPE(Class, Member)::value)

namespace OP::has_operators
{
    namespace details{

        template<class T, class = decltype(std::declval<T>() < std::declval<T>() )> 
        std::true_type  has_less_than_test(const T&);
        std::false_type has_less_than_test(...);

        template<class T, class = decltype( !std::declval<T>() )> 
        std::true_type  has_logical_not_test(const T&);
        std::false_type has_logical_not_test(...);

        template<class T, class = decltype(std::declval<std::ostream&>() << std::declval<T>() )> 
        std::true_type  has_ostream_out(const T&);
        std::false_type has_ostream_out(...);
    
    }//ns:details

    /** Compile time check if type T supports operator less `<` */
    template<class T> using less = decltype(details::has_less_than_test(std::declval<T>()));
    template<class T> inline constexpr bool less_v = less<T>::value;

    /** Compile time check if type T supports operator logical-not `!` */
    template<class T> using logical_not = decltype(details::has_logical_not_test(std::declval<T>()));
    template<class T> inline constexpr bool logical_not_v = logical_not<T>::value;

    /** Compile time check if type T supports operator << for std::ostream */
    template<class T> using ostream_out = decltype(details::has_logical_not_test(std::declval<T>()));
    template<class T> inline constexpr bool ostream_out_v = logical_not<T>::value;
} //ns:OP::operators_check

#endif //_OP_COMMON_HAS_MEMBER_DEF__H_
