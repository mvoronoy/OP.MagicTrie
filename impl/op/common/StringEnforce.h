#pragma once
#ifndef _OP_COMMON_STRINGENFORCE__H_
#define _OP_COMMON_STRINGENFORCE__H_

#include <string>

namespace OP
{

        /** type definition to enforce char-like behaviour. This defined as ability to convert
        * to T.
        * Use case to check char-like buffer: \code
        *   template <typename T>
        *   void some_function_needed_literal(const ConvertibleToEnforce<T, char>* buffer, size_t size)
        *   .... \endcode
        *
        *   now:  \code
        *   some_function_needed_literal<char>( "abc", 3); // allowed 
        *   some_function_needed_literal<unsigned char>( 
        *       std::array<unsigned char, 1>(128).data(), 1); //allowed as unsigned char convertible to char
        *   some_function_needed_literal<char>( std::cout, 3); // not allowed as ostream is not convertible to char
        *   \endcode
        */ 
        template <typename TExpectingChar, typename T,
            typename = std::enable_if_t<
            std::is_convertible_v<std::decay_t<T>, TExpectingChar> > >
        using ConvertibleToEnforce = T;

        namespace details
        {
            template <typename TExpectingChar, typename T, std::enable_if_t<std::is_convertible_v<T, TExpectingChar>, int> = 0>
            [[maybe_unused]] static std::true_type _is_string_compatible(const std::basic_string_view<T>&) noexcept;

            template <typename TExpectingChar, typename T, std::enable_if_t<std::is_convertible_v<T, TExpectingChar>, int> = 0>
            [[maybe_unused]] static std::true_type _is_string_compatible(const std::basic_string<T>&) noexcept;

            template <typename TExpectingChar, typename T>
            [[maybe_unused]] static std::false_type _is_string_compatible(T) noexcept;

            template <typename TExpectingChar, typename T>
            constexpr static inline bool buffer_is_compatible_c = 
                decltype(_is_string_compatible<TExpectingChar>(std::declval<T>()))::value;

        } //ns: details

        /** type definition to enforce readonly (const) buffer behaviour like std::string or std::string_view. 
        *
        *   Use case: \code
        *
        *   template <typename T>
        *   void some_function_expecting_string_like(const BasicStringEnforce<char, T>& buffer_like);
        *   \endcode
        *
        *   now:  \code
        *   using namespace std::string_literals;
        *   some_function_expecting_string_like( "abc"s ); // allowed 
        *   some_function_expecting_string_like( "abc"sv ); //allowed 
        *   some_function_expecting_string_like( std::basic_string<int>(1, 2) ); //allowed as exists int to char conversion
        *   some_function_expecting_string_like( 3.5 ); // not allowed as double is not convertible to std::string
        *   \endcode
        */
        template <typename TExpectingChar, typename T,
            typename = std::enable_if_t<OP::details::buffer_is_compatible_c<TExpectingChar, T> > >
        using BasicStringEnforce = T;

       template <typename T>
       using StringEnforce = BasicStringEnforce<typename std::string::value_type, T>;

       template <typename T>
       using WstringEnforce = BasicStringEnforce<typename std::wstring::value_type, T>;

} //ns:OP


#endif // _OP_COMMON_STRINGENFORCE__H_
