#pragma once
#ifndef _OP_COMMON_FIXEDSTRING__H_
#define _OP_COMMON_FIXEDSTRING__H_

#include <string>
#include <set>
#include <memory>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <op/common/Unsigned.h>
#include <op/common/Utils.h>

#ifdef _MSC_VER
//Need suppress IntelliSense warning about +1 array over SAL hints
#define RANGE_SAFE_BUF(n) _Out_writes_bytes_all_(n)
#else
#define RANGE_SAFE_BUF(n)
#endif
namespace OP
{

    template <class TChar, size_t limit_c>
    struct fix_str_policy_noexcept
    {
        using char_t = TChar;

        constexpr static size_t buffer_size_c = limit_c;

        static void fail(const char*message) noexcept
        {
            std::cerr << message;
            std::terminate();
        }
    };

    template <class TChar, size_t limit_c>
    struct fix_str_policy_throw_exception
    {
        using char_t = TChar;

        constexpr static size_t buffer_size_c = limit_c;

        static void fail(const char* message)
        {
            throw std::runtime_error(message);
        }
    };


    template <class TChar>
    using FixedStringView = std::basic_string_view<TChar>;

    template <class Policy>
    class FixedString
    {
        using str_policy_t = Policy;
        using inner_char_t = typename str_policy_t::char_t;
        static_assert(sizeof(inner_char_t) <= sizeof(int), "Must use character size less or equal to platform int (char, uchar...)");

        constexpr static size_t buf_capacity_c = str_policy_t::buffer_size_c;
        static_assert(buf_capacity_c >= 1, "buffer capacity must be at least 1 (means zero size buffer)");
        static_assert(buf_capacity_c <= (1 << (sizeof(inner_char_t) << 3)),
            "Specified capacity cannot be implemented for Policy::char_t type"
            );

        //allow most of the methods be noexcept
        constexpr static bool use_noexcept_c = noexcept(str_policy_t::fail(""));

        template <typename T,
            typename = std::enable_if_t<
            std::is_convertible_v<std::decay_t<T>, inner_char_t> > >
        using LiteralEnforce = T;

        //template <class T, 
        //    std::enable_if_t<std::is_convertible_v<typename FixedString<T>::value_type, inner_char_t>, int> = 0>
        //static std::true_type _is_compatible(const FixedString<T>&) noexcept;
        
        template <class T, std::enable_if_t<std::is_convertible_v<T, inner_char_t>, int> = 0>
        static std::true_type _is_compatible(const std::basic_string_view<T>&) noexcept;

        template <class T, std::enable_if_t<std::is_convertible_v<T, inner_char_t>, int> = 0>
        static std::true_type _is_compatible(const std::basic_string<T>&) noexcept;

        static std::false_type _is_compatible(...) noexcept;

        template <class T>
        constexpr static inline bool buffer_is_compatible_c = decltype(_is_compatible(std::declval<T>()))::value;

        template <typename T,
            typename = std::enable_if_t<buffer_is_compatible_c<T> > >
        using StringEnforce = T;

        static void enforce(bool condition, const char *reason) noexcept(use_noexcept_c)
        {
            if(!condition)
                str_policy_t::fail(reason);
        }
        template <class T>
        static T enforce_value(bool condition, T t, const char *reason) noexcept(use_noexcept_c)
        {
            if(!condition)
                str_policy_t::fail(reason);
            return t;
        }
        constexpr static inline const char err_exceed_limit[] = "exceed fixed string capacity";
            
    public:

        using value_type = inner_char_t;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = pointer; //LegacyRandomAccessIterator and LegacyContiguousIterator to value_type
        using const_iterator = const_pointer;//LegacyRandomAccessIterator, contiguous_iterator, and ConstexprIterator to const value_type
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using view_t = FixedStringView<value_type>;

        constexpr static size_type capacity_c = buf_capacity_c - 1;
        constexpr static size_type npos = ~size_type{0};

        constexpr FixedString() noexcept
            : _buffer{ 0 }
        {
        }

        constexpr FixedString(size_type count, value_type ch) noexcept(use_noexcept_c)
            : _buffer{ static_cast<value_type>(enforce_value(count <= capacity_c, count, err_exceed_limit)) }
        {
            RANGE_SAFE_BUF(count+1) value_type* dest = _buffer;
            for (; count; --count) //plays role of +1
                dest[count] = ch;
        }

        template <class T>
        constexpr FixedString(const LiteralEnforce<T>* arg, size_t count) noexcept(use_noexcept_c)
            : _buffer{ static_cast<value_type>(enforce_value(count <= capacity_c, count, err_exceed_limit)) }
        {
            RANGE_SAFE_BUF(count + 1) value_type* dest = _buffer;
            for (; count; --count) //plays role of +1
                dest[count] = static_cast<value_type>(arg[count - 1]);
        }

        template <class StringLike>
        explicit constexpr FixedString(const StringEnforce<StringLike>& sv) noexcept(use_noexcept_c)
            : FixedString{ 
                sv.data(), 
                enforce_value(sv.size() <= capacity_c, sv.size(), err_exceed_limit) 
            }
        {
        }

        /**
        * 
        * \throws std::out_of_range when pos is invalid for sv string.
        */ 
        template <class StringLike>
        explicit constexpr FixedString(
            const StringEnforce<StringLike>& sv, size_type pos, size_type count = npos) 
            : FixedString{ OP::utils::subview<FixedStringView<typename StringLike::value_type>>(sv, pos, count) }
        {
        }

        template <class T, std::enable_if_t<std::is_convertible_v<T, value_type>, int> = 0>
        explicit constexpr FixedString(const std::initializer_list<T> init) noexcept(use_noexcept_c)
            : _buffer{ 0 }
        {
            enforce(init.size() <= capacity_c, err_exceed_limit);
            auto p = end(); //keep before assign size
            _buffer[0] = static_cast<value_type>(init.size());
            for (auto first = init.begin(), last = init.end(); first != last; ++first, ++p)
            {
                *p = *first;
            }
        }
        
        template <class TPolicy,
            std::enable_if_t<std::is_convertible_v<typename FixedString<TPolicy>::value_type, inner_char_t>, int> = 0
        >
        explicit constexpr FixedString(const FixedString<TPolicy>& other) noexcept(use_noexcept_c)
            : FixedString{
                sv.data(),
                enforce_value(sv.size() <= capacity_c, sv.size(), err_exceed_limit)
            }
        {
        }

        constexpr FixedString(const FixedString&) noexcept = default;
        constexpr FixedString(FixedString&&) noexcept = default;

        constexpr size_t capacity() const noexcept
        {
            return capacity_c;
        }

        constexpr size_t size() const noexcept
        {
            return _buffer[0];
        }

        constexpr bool empty() const noexcept
        {
            return _buffer[0] == 0;
        }

        pointer data() noexcept
        {
            return &_buffer[1];
        }

        const_pointer data() const noexcept
        {
            return &_buffer[1];
        }

        constexpr iterator begin() noexcept
        {
            return &_buffer[1];
        }

        constexpr const_iterator begin() const noexcept
        {
            return &_buffer[1];
        }

        constexpr iterator end() noexcept
        {
            return begin() + size();
        }

        constexpr const_iterator end() const noexcept
        {
            return begin() + size();
        }

        constexpr const_iterator cbegin() const noexcept
        {
            return &_buffer[1];
        }

        constexpr const_iterator cend() const noexcept
        {
            return cbegin() + size();
        }

        /**
        *
        * \throws std::out_of_range when pos is invalid.
        */
        constexpr reference at(size_t pos)
        {
            if (pos >= size())
                throw std::out_of_range("`at` out of the range");
            return *(begin() + pos);
        }

        /**
        * 
        * \throws std::out_of_range when pos is invalid.
        */ 
        constexpr const_reference at(size_t pos) const
        {
            if (pos >= size())
                throw std::out_of_range("`at` is out of the range");
            return *(cbegin() + pos);
        }

        constexpr reference front() noexcept
        {
            assert(!empty());
            return *begin();
        }

        constexpr const_reference front() const noexcept
        {
            assert(!empty());
            return *cbegin();
        }

        constexpr reference back() noexcept
        {
            assert(!empty());
            return _buffer[_buffer[0]]; //-1+1
        }

        constexpr const_reference back() const noexcept
        {
            assert(!empty());
            return _buffer[_buffer[0]];//-1+1
        }

        constexpr int compare(const FixedString& other) const noexcept
        {
            const auto limit = size() + 1; //+1 to calm down static analyzers, because comparing 0-char as well
            for (auto i = 0; i < limit; ++i)
            {
                int delta = _buffer[i]; //static_assert must control that sizeof(inner_char_t) less than int
                delta -= other._buffer[i];
                if (delta) //fails immediately even if length is not the same
                    return delta;
            }
            return 0;
        }

        /** the same as std::string::substr(...) 
        * \throws std::out_of_range when `pos > size()`.
        */
        FixedString substr( size_type pos = 0, size_type count = npos ) const
        {
            if( pos > size() ) //empty string is allowed
                throw std::out_of_range("`substr` is out of the range");
            size_type n = size() - pos;
            if(count < n)
                n = count;
            return FixedString{data() + pos, n};
        }

        /** the same as std::string::clear() */
        constexpr void clear() noexcept
        {
            _buffer[0] = 0;
        }

        void resize(size_t n) noexcept(use_noexcept_c)
        {
            enforce(n <= capacity_c, err_exceed_limit);

            _buffer[0] = static_cast<value_type>(n);
        }

        void reserve(size_t n) noexcept(use_noexcept_c)
        {
            enforce(n <= capacity_c, err_exceed_limit);
            // do nothing
        }

        template< class InputIt >
        constexpr FixedString& append(InputIt first, InputIt last) noexcept(use_noexcept_c)
        {
            const auto delta = std::distance(first, last);
            enforce((size() + delta) <= capacity_c, err_exceed_limit);
            auto p = end();
            for (; first != last; ++first, ++p)
            {
                *p = *first;
            }
            _buffer[0] += static_cast<value_type>(delta);
            return *this;
        }

        template <class TPolicy, 
            std::enable_if_t<std::is_convertible_v<typename FixedString<TPolicy>::value_type, inner_char_t>, int> = 0>
        constexpr FixedString& append(const FixedString<TPolicy>& other) noexcept(use_noexcept_c)
        {
            return append(other.data(), other.size());
        }

        template <class StringLike>
        constexpr FixedString& append(const StringEnforce<StringLike>& sv) noexcept(use_noexcept_c)
        {
            return append(sv.data(), sv.size());
        }

        template <class T>
        constexpr FixedString& append(const LiteralEnforce<T>* src, size_t n) noexcept(use_noexcept_c)
        {
            enforce((size() + n) <= capacity_c, err_exceed_limit);
            auto p = end();
            _buffer[0] += static_cast<value_type>(n);
            for (; n; ++p, ++src, --n)
            {
                *p = static_cast<value_type>(*src);
            }
            return *this;
        }

        template <class T>
        constexpr FixedString& append(size_t n, LiteralEnforce<T> symbol) noexcept(use_noexcept_c)
        {
            enforce((size() + n) <= capacity_c, err_exceed_limit);
            auto p = end(); //before change of size
            _buffer[0] += static_cast<value_type>(n);
            for (; n; ++p, --n)
                *p = static_cast<value_type>(symbol);
            return *this;
        }

        template <class StringLike>
        FixedString& assign(const StringEnforce<StringLike>& sv) noexcept(use_noexcept_c)
        {
            clear();
            return append(sv);
        }

        template <class StringLike>
        FixedString& assign(const StringEnforce<StringLike>& sv, size_type pos, size_type count = npos) noexcept(use_noexcept_c)
        {
            size_type n = sv.size() - pos;
            if (count < n)
                n = count;

            enforce(n <= capacity_c, err_exceed_limit); //need test before string changed
            clear();
            return append(sv.data() + pos, n);
        }

        template <class T>
        FixedString& push_back(LiteralEnforce<T> symbol) noexcept(use_noexcept_c)
        {
            enforce((size() + 1) <= capacity_c, err_exceed_limit);
            _buffer[++_buffer[0]] = static_cast<value_type>(symbol);
            return *this;
        }

        template <class T>
        constexpr FixedString& replace(size_t pos, size_t count,
                       const LiteralEnforce<T>* symbols, size_t src_length) 
        {
            if(pos > size())
                throw std::out_of_range("replace position is out of the range");
            const size_t cut_count = std::min(size() - pos, count);
            const auto tail_offset = OP::utils::uint_diff_int(src_length, cut_count);
            enforce((size() + tail_offset) < capacity_c, err_exceed_limit);
            
            auto move_dest = begin() + pos;
            //save suffix first
            auto suffix_pos = move_dest + cut_count;
            if(tail_offset < 0) //need move suffix left
            {
                auto suffix_dest = move_dest + src_length;
                for(;suffix_pos != end(); ++suffix_pos, ++suffix_dest)
                    *suffix_dest = *suffix_pos;
            }
            else if(tail_offset > 0) //move suffix right
            {
                auto suffix_dest = end() + tail_offset - 1;
                for(auto r = end(); r != suffix_pos; --suffix_dest)
                {
                    *suffix_dest = *--r;
                }
            }
            //else when `tail_offset == 0`: {nothing to move in suffix}

            for(size_t i = 0; i < src_length; ++i)
                *move_dest++ = symbols[i];

            _buffer[0] = static_cast<value_type>((size() + tail_offset));

            return *this;
        }

        /**
        *
        * \throws std::out_of_range when position exceeds size or new string overflows capacity.
        */
        template <class StringLike>
        constexpr FixedString& replace(size_t pos, size_t count,
                       const StringEnforce<StringLike>& sv) 
        {
            return replace(pos, count, sv.data(), sv.size());
        }

        constexpr FixedString& operator = (const FixedString&) noexcept = default;
        constexpr FixedString& operator = (FixedString&&) noexcept = default;

        template <class StringLike>
        constexpr FixedString& operator = (const StringEnforce<StringLike>& right) noexcept(use_noexcept_c)
        {
            return assign(right);
        }

        template <class StringLike>
        constexpr FixedString& operator += (const StringEnforce<StringLike>& right) noexcept(use_noexcept_c)
        {
            return append(right);
        }

        template <class TPolicy>
        constexpr FixedString& operator += (const FixedString<TPolicy>& right) noexcept(use_noexcept_c)
        {
            return append(right);
        }
        /** create new instance
        * 
        * \throws std::out_of_range when total length exceeds FixedString capacity.
        */
        template <class StringLike>
        constexpr FixedString operator + (const StringEnforce<StringLike>& right) const noexcept(use_noexcept_c)
        {
            enforce(
                (size() + right.size()) <= capacity_c, "sum length exceeds capacity");
            FixedString result{ *this };
            result.append(right);
            return result;
        }

        template <class TPolicy,
            std::enable_if_t<std::is_convertible_v<typename FixedString<TPolicy>::value_type, inner_char_t>, int> = 0
        >
        constexpr FixedString operator + (const FixedString<TPolicy>& right) const noexcept(use_noexcept_c)
        {
            enforce(
                (size() + right.size()) <= capacity_c, "sum length exceeds capacity");
            FixedString result{ *this };
            result.append(right);
            return result;
        }

        constexpr explicit operator view_t() const noexcept
        {
            return view_t{ data(), size() };
        }

        constexpr reference operator[](size_t idx) noexcept
        {
            assert(idx < size()); // need in the sake of compatibility with array[]
            return _buffer[idx + 1];
        }

        constexpr const_reference operator[](size_t idx) const noexcept
        {
            assert(idx < size());
            return _buffer[idx + 1];
        }

        constexpr bool operator == (const FixedString& other) const noexcept
        {
            return compare(other) == 0;
        }

        constexpr bool operator != (const FixedString& other) const noexcept
        {
            return !operator == (other);
        }
#ifdef OP_CPP20_FEATURES
        // note! operator less follows compatibility with std::string instead of lexico-compare
        constexpr auto operator <=> (const FixedString& other) -> decltype(this->compare(other)) const noexcept
        {
            return std::lexicographical_compare_three_way(
                begin(), end(), other.begin(), other.end());
        }
#else
        // note! operator less follows compatibility with std::string instead of lexico-compare
        constexpr bool operator < (const FixedString& other) const noexcept
        {
            return std::lexicographical_compare(
                begin(), end(), other.begin(), other.end());
        }

        constexpr bool operator > (const FixedString& other) const noexcept
        {
            return std::lexicographical_compare(
                other.begin(), other.end(), //swap
                begin(), end());
        }
#endif //OP_CPP20_FEATURES

        template <class U>
        friend inline constexpr bool operator == (const StringEnforce<U>& left, const FixedString& right) noexcept
        {
            auto lb = std::begin(left), le = std::end(left);
            auto rb = std::begin(right), re = std::end(right);
            for (; lb != le && rb != re; ++lb, ++rb)
            {
                if (!(*lb == *rb))
                    return false;
            }
            return true;
        }

        template <class T>
        friend inline constexpr bool operator == (const FixedString& left, const StringEnforce<T>& right) noexcept
        {
            return (right == left);
        }

        template <class U>
        friend inline constexpr bool operator != (const StringEnforce<U>& left, const FixedString& right) noexcept
        {
            return !(left == right);
        }

        template <class T>
        friend inline constexpr bool operator != (const FixedString& left, const StringEnforce<T>& right) noexcept
        {
            return !(right == left);
        }

    private:
        value_type _buffer[buf_capacity_c];
    };

    template <class TPolicy>
    inline std::ostream& operator << (std::ostream& os, const FixedString<TPolicy>& ek)
    {
        os.write(reinterpret_cast<const char*>(ek.data()), ek.size());
        return os;
    }

    
} //ns:OP

#undef RANGE_SAFE_BUF

namespace std
{
    template <class TPolicy> struct hash<OP::FixedString<TPolicy>>
    {
        using char_t = typename OP::FixedString<TPolicy>::value_type;
        template <class T>
        size_t operator()(const T& x) const
        {
            size_t seed = 0x52dfe1397;
            ::std::hash<char_t> h;
            for (auto c : x)
                seed = (seed * 101) + h(c);
            return seed;
        }
    };

}//ns:std

#endif // _OP_COMMON_FIXEDSTRING__H_
