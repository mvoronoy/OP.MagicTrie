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
    Also this member provides additional predicate is_invocable<Ax...> that allows to check
    if specified member is invocable for checking class with probe arguments `Ax...`. In compare
    with std::is_invocable_v it doesn't fail when method is not exist. Examples:
    ~~~~~~~~~~~~~~~~~~~~~~
    has_size<std::string>::is_invocable() //`true` since exists std::string::size()    
    has_size<std::string>::is_invocable<double>() //`false` since std::string::size() has no double arg
    has_some_method<std::string>::is_invocable() //`false` since no std::string::some_method() (in compare 
                                                 //std::is_invocable_v that even cannot be compiled)
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
        template <class ... Ax> \
        static constexpr bool is_invocable() \
        { if constexpr (value) {return std::is_invocable_v<&A::Member, A&, Ax...>;} \
        else return false; \
        }\
    };

/** 
    Define macro same as OP_DECLARE_CLASS_HAS_MEMBER but allows to check presence of template member.
    For example you need check if some class has defined `template <typename T> method`.  
    Define is similary straigh `OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(method)`. Later use:
    ~~~~~~~~~~~~~~~~~~~~~~
    has_method<MyClassWithMethod, int*>::value //evaluates true if defined MyClassWithMethod::method<int*>()
    ~~~~~~~~~~~~~~~~~~~~~~
*/
#define OP_DECLARE_CLASS_HAS_TEMPLATE_MEMBER(Member)  template <typename A, typename ...Ts>\
    class has_##Member \
    { \
        typedef char YesType[1]; \
        typedef char NoType[2]; \
        template <typename C> static YesType& test( decltype(&C::template Member<Ts ...>) ) ; \
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

        template<class T, class = decltype(std::declval<T>() <= std::declval<T>() )> 
        std::true_type  has_less_eq_than_test(const T&);
        std::false_type has_less_eq_than_test(...);

        template<class T, class = decltype(std::declval<T>() > std::declval<T>() )> 
        std::true_type  has_greater_than_test(const T&);
        std::false_type has_greater_than_test(...);

        template<class T, class = decltype(std::declval<T>() >= std::declval<T>() )> 
        std::true_type  has_greater_eq_than_test(const T&);
        std::false_type has_greater_eq_than_test(...);

        template<class T, class = decltype( !std::declval<T>() )> 
        std::true_type  has_logical_not_test(const T&);
        std::false_type has_logical_not_test(...);

        template<class T, class = decltype(std::declval<T>() == std::declval<T>() )> 
        std::true_type  has_eq_test(const T&);
        std::false_type has_eq_test(...);

        template<class T, class = decltype(std::declval<T>() != std::declval<T>() )> 
        std::true_type  has_not_eq_test(const T&);
        std::false_type has_not_eq_test(...);

        template<class T, class = decltype(std::declval<std::ostream&>() << std::declval<T>() )> 
        std::true_type  has_ostream_out(const T&);
        std::false_type has_ostream_out(...);
    
    }//ns:details

    /** Compile time check if type T supports operator less `<` */
    template<class T> using less = decltype(details::has_less_than_test(std::declval<T>()));
    template<class T> inline constexpr bool less_v = less<T>::value;

    /** Compile time check if type T supports operator less-eq `<=` */
    template<class T> using less_eq = decltype(details::has_less_eq_than_test(std::declval<T>()));
    template<class T> inline constexpr bool less_eq_v = less_eq<T>::value;

    /** Compile time check if type T supports operator greater `>` */
    template<class T> using greater = decltype(details::has_greater_than_test(std::declval<T>()));
    template<class T> inline constexpr bool greater_v = greater<T>::value;

    /** Compile time check if type T supports operator greater-eq `>=` */
    template<class T> using greater_eq = decltype(details::has_greater_eq_than_test(std::declval<T>()));
    template<class T> inline constexpr bool greater_eq_v = greater_eq<T>::value;

    /** Compile time check if type T supports operator logical-not `!` */
    template<class T> using logical_not = decltype(details::has_logical_not_test(std::declval<T>()));
    template<class T> inline constexpr bool logical_not_v = logical_not<T>::value;

    /** Compile time check if type T supports operator equals `==` */
    template<class T> using equals = decltype(details::has_eq_test(std::declval<T>()));
    template<class T> inline constexpr bool equals_v = equals<T>::value;

    /** Compile time check if type T supports operator equals `!=` */
    template<class T> using not_equals = decltype(details::has_not_eq_test(std::declval<T>()));
    template<class T> inline constexpr bool not_equals_v = not_equals<T>::value;

    /** Compile time check if type T supports operator << for std::ostream */
    template<class T> using ostream_out = decltype(details::has_ostream_out(std::declval<T>()));
    template<class T> inline constexpr bool ostream_out_v = ostream_out<T>::value;
} //ns:OP::operators_check

#endif //_OP_COMMON_HAS_MEMBER_DEF__H_
