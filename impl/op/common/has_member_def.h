#pragma once
#ifndef _OP_COMMON_HAS_MEMBER_DEF__H_
#define _OP_COMMON_HAS_MEMBER_DEF__H_

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

#define OP_CHECK_CLASS_HAS_MEMBER_TYPE(Class, Member) details::has_##Member<Class>

/** Complimentar to OP_DECLARE_CLASS_HAS_MEMBER - generate compile time const that indicate if class `A` has member `Member`*/
#define OP_CHECK_CLASS_HAS_MEMBER(Class, Member) (OP_CHECK_CLASS_HAS_MEMBER_TYPE(Class, Member)::value)


#endif //_OP_COMMON_HAS_MEMBER_DEF__H_
