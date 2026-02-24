#pragma once

#ifndef _OP_COMMON_ASSOC__H_
#define _OP_COMMON_ASSOC__H_

namespace OP
{

     /**
      * \brief Associates a compile-time constant key with a C++ type.
      *
      * Assoc is a lightweight metaprogramming utility that binds an arbitrary
      * compile-time constant (`auto` non-type template parameter) to a type.
      *
      * The key may be any value that is valid as a non-type template parameter,
      * including:
      *   - enum / enum class values
      *   - integral constants
      *   - pointers to static objects
      *   - static constexpr character arrays (e.g. string identifiers)
      *   - any other compile-time constant with external linkage
      *
      * Example:
      *
      * \code
      * // Associate enum value with a payload type
      * using my_enum_payload_t = Assoc<enum_1, payload_type1>;
      *
      * // Associate string key with a payload type
      * constexpr static char key[] = "key1";
      * using my_named_payload_t = Assoc<key, payload_type2>;
      * \endcode
      *
      * \tparam key_c Compile-time constant key.
      * \tparam T Associated type.
      */
     template <auto key_c, class T>
     struct Assoc
     {
         using key_t = decltype(key_c);
         constexpr static key_t code = key_c;
         using type = T;
     };


     /**
      * \brief Associates a compile-time constant key with a C++ type and value.
      *
      * Useful in metaprogramming to lookup values over tuple with specific key traits.
      *
      * \tparam key_c Compile-time constant key.
      * \tparam T Associated type.
      */
     template <auto c, class T>
     struct AssocVal : Assoc<c, T>
     {
         T value;

         template < class U >
         constexpr AssocVal(U&& v) noexcept
             : value(std::forward<U>(v))
         {
         }

         constexpr AssocVal() noexcept
             : value{}
         {
         }
     };


}//ns:OP

#endif //_OP_COMMON_ASSOC__H_