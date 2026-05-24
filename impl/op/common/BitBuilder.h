#pragma once
#ifndef _OP_COMMON_BITBUILDER__H_
#define _OP_COMMON_BITBUILDER__H_

#include <string>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <bit>
#include <bitset>


namespace OP::common
{
    /** \brief Builder that allows creation of byte sequences from bits.
    *
    */
    template < class TByteHolder = std::basic_string<std::uint8_t> >
    class BitBuilder
    {
    public:
        constexpr BitBuilder() 
            noexcept(std::is_nothrow_constructible_v<TByteHolder>) = default;

        template <class UInt>
            requires std::unsigned_integral<UInt>
        constexpr BitBuilder(UInt value, std::uint_fast8_t valuable_bits = std::numeric_limits<UInt>::digits)
            noexcept(is_string_push_back_noexcept_c)
        {
            append_uint(value, valuable_bits);
        }

        template <std::forward_iterator TByteIter>
        constexpr BitBuilder(TByteIter begin, TByteIter end)
            noexcept(std::is_nothrow_constructible_v<TByteHolder, TByteIter, TByteIter>)
            : _buffer(begin, end)
        {
        }

        template <class TByte>
        BitBuilder(std::initializer_list<TByte> array)
            noexcept(std::is_nothrow_constructible_v<TByteHolder, 
                typename std::initializer_list<TByte>::iterator, typename std::initializer_list<TByte>::iterator>)
            : _buffer(array.begin(), array.end())
        {
        }

        constexpr size_t size() const noexcept
        {
            return _buffer.size() * byte_bits_c - _uninhabited;
        }

        constexpr auto operator<=>(const BitBuilder& other) const noexcept
        {
            auto this_bit_size = size(), other_bit_size = other.size();
            if (this_bit_size == other_bit_size) //can safely compare lexico
                return _buffer <=> other._buffer;
            //need handle unaligned bits more accurate

            auto common_size = std::min(complete_byte_size(), other.complete_byte_size());
            auto aligned_compare = //compare aligned part of bytes
                byte_view_t(_buffer.data(), common_size)
                <=> byte_view_t(other._buffer.data(), common_size);
            if (!std::is_eq(aligned_compare))
                return aligned_compare;
            //still equals need compare by bits
            auto tail_bits_size = (common_size)*byte_bits_c;
            byte_t mask = 1 << (byte_bits_c - 1);

            for (; mask && (tail_bits_size < this_bit_size) && (tail_bits_size < other_bit_size);
                mask >>= 1, ++tail_bits_size)
            {
                auto c = (_buffer[common_size] & mask) <=> (other._buffer[common_size] & mask);
                if (!std::is_eq(c))
                    return c;
            }
            auto result = tail_bits_size < this_bit_size ? std::strong_ordering::greater :
                tail_bits_size < other_bit_size ? std::strong_ordering::less :
                std::strong_ordering::equal;
            assert(!std::is_eq(result)); //this impossible since was proven at the first step
            return result;
        }

        constexpr bool operator==(const BitBuilder& other) const noexcept
        {
            return std::is_eq(operator <=> (other));
        }


        void clear() noexcept
        {
            _uninhabited = 0;
            _buffer.clear(); //actually noexcept from c++11
        }

        void reserve(size_t bits)
        {
            _buffer.reserve(bits);
        }

        BitBuilder& append_1()
            noexcept(is_string_push_back_noexcept_c)
        {
            auto& back = expansion();
            --_uninhabited/*& 0b0111*/;
            back |= 1 << _uninhabited;
            return *this;
        }

        BitBuilder& append_0()
            noexcept(is_string_push_back_noexcept_c)
        {
            expansion();
            --_uninhabited; //0 automatically presence
            return *this;
        }

        template <class UInt>
        BitBuilder& append_uint(UInt value, std::uint_fast8_t valuable_bits = std::numeric_limits<UInt>::digits)
            noexcept(is_string_push_back_noexcept_c)
        {
            static_assert(std::is_unsigned_v<UInt>);
            value <<= std::numeric_limits<UInt>::digits - valuable_bits;
            while (valuable_bits)
            {
                auto& back = expansion();
                auto chunk = (value >> (std::numeric_limits<UInt>::digits - _uninhabited));
                back |= chunk;
                auto n = std::min(valuable_bits, _uninhabited);
                _uninhabited -= n;
                valuable_bits -= n;
                value <<= n; //remove placed bits
            }
            return *this;
        }

        /**Append padding bits to align builder's last byte*/
        BitBuilder& append_pad(bool bit = false)
            noexcept(is_string_push_back_noexcept_c)
        {
            if (!bit)
            {
                _uninhabited = 0;
                return *this;
            }
            return this->append_uint(std::numeric_limits<byte_t>::max(), _uninhabited);
        }

        inline std::string to_string() const
        {
            std::string result;
            auto n = _buffer.size();
            for (size_t i = 0; i < n; ++i)
            {
                auto limit = byte_bits_c;
                if ((i + 1) == n) //last byte
                    limit -= _uninhabited;
                for (auto b = _buffer[i]; limit; --limit, b <<= 1)
                    result.push_back((b & (1u << (byte_bits_c - 1))) ? '1' : '0');
            }
            return result;
        }

        /**
        * Convert to string using arbitrary alphabet.
        *
        * \pre Alphabet must be power of 2.
        */
        template <class StringLike, class AlphabetIter>
        constexpr StringLike& to_string(StringLike& out, AlphabetIter begin, AlphabetIter end) const
        {
            const auto _sbase = std::distance(begin, end);
            const auto base = static_cast<std::make_unsigned_t<decltype(_sbase)>>(_sbase);

            assert((base & (base - 1)) == 0); // force power of 2

            const auto bs_ln = std::bit_width(base) - 1; //ln[2](Alphabet)
            const auto last_bits_mask = ((1 << bs_ln) - 1); //mask to cut lst bits
            // Calculate approximate size: (bits / bs_ln) rounded up
            size_t out_idx = out.size();
            out.resize(out.size() + (_buffer.size() * 8 + bs_ln - 1) / bs_ln, *begin);//populate with 'zero' symbols

            unsigned bit_buffer = 0;
            int bits_left = 0;
            for (auto i = 0; i < _buffer.size(); ++i)
            {
                // Push 8 bits into the buffer
                bit_buffer <<= byte_bits_c;
                if ((i + 1) == _buffer.size()) //last may be unaligned
                {
                    bit_buffer |= _buffer[i] >> _uninhabited;
                    bits_left += (byte_bits_c - _uninhabited);
                }
                else
                {
                    bit_buffer |= _buffer[i];
                    bits_left += byte_bits_c;
                }

                // Extract (bs_ln) bits chunks as long as we have enough
                while (bits_left >= bs_ln)
                {
                    bits_left -= bs_ln;
                    byte_t index = (bit_buffer >> bits_left) & last_bits_mask;
                    out[out_idx++] = (*(begin + index));
                }
            }

            // Handle remaining bits (padding/tail)
            if (bits_left > 0)
            {
                // Shift remaining bits to the left to form a full 3-bit "index"
                byte_t index = (bit_buffer << (bs_ln - bits_left)) & last_bits_mask;
                out[out_idx++] = (*(begin + index));
            }
            out.resize(out_idx); //truncate if needed
            return out;
        }

        friend inline std::ostream& operator << (std::ostream& os, const BitBuilder& bb)
        {
            if (!(os.flags() && (std::ios_base::hex | std::ios_base::oct)))
            {
                os << bb.to_string();
            }
            else
            {
                static constexpr std::string::value_type alphabet[] = "0123456789ABCDEF";
                std::string buf;
                const auto size = (os.flags() & std::ios_base::oct)
                    ? 8
                    : 16;
                bb.to_string(buf, alphabet, alphabet + size);
                os << buf;
            }
            return os;
        }

        /** Takes single bit from position specified.
        * \pre argument `from` must be in the range, otherwise behavior is undefined.
        */
        bool take_bit(size_t from) const noexcept
        {
            assert(from < size());
            auto byte = from >> 3, bit = from & 0b111;
            return _buffer[byte] & (1 << (byte_bits_c - 1 - bit));
        }

        /** Takes N bits (argument width) from the specified position.
        * Note: out parameter shifts previous value, so result of several `take_n` to one out variable has cumulative effect: \code
        * unsigned result = 0b10;
        * builder.append_uint(1, 1); //builder now is "1"
        * builder.take_n(0, 1, result); //result now == 0b101
        * \endcode
        * \pre `from+width` must be in the range, otherwise behavior is undefined.
        * \pre `width` must not exceed width of destination type `T`.
        * \pre `T` must be unsigned integer type.
        */
        template <class T>
        void take_n(size_t from, size_t width, T& out) const noexcept
        {
            assert(width <= std::numeric_limits<T>::digits);
            assert((from + width) <= size());
            auto byte = from >> 3, bit = byte_bits_c - 1 - (from & 0b111);
            for (; width; --width)
            {
                out <<= 1;
                T curr = (_buffer[byte] >> bit) & 1; //either 0 or 1
                //out &= ~curr;
                out |= curr;
                --bit; //may overflow to 0xff..ff
                byte += (bit >> 3) & 1; //only for overflow case
                bit &= 0b111;
            }
        }

        /**
        * Returns the number of consecutive `1`  bits, starting from the position `from`.
        */
        constexpr size_t sequence_width_1(size_t from) const noexcept
        {
            return sequence_width<false>(from);
        }
        /**
        * Returns the number of consecutive `0`  bits, starting from the position `from`.
        */
        constexpr size_t sequence_width_0(size_t from) const noexcept
        {
            return sequence_width<true>(from);
        }

    private:
        using byte_string_t = TByteHolder;
        using byte_t = typename byte_string_t::value_type;
        using byte_view_t = std::basic_string_view<byte_t>;

        constexpr static unsigned byte_bits_c = std::numeric_limits<byte_t>::digits;
        
        /**checks if `TByteHolder::push_back` is noexcept */
        constexpr static bool is_string_push_back_noexcept_c = noexcept(std::declval<byte_string_t>().push_back(std::declval<byte_t>()));
        

        std::uint_fast8_t _uninhabited = 0;
        byte_string_t _buffer;

        inline byte_t& expansion(byte_t expand_with = 0)
            noexcept(is_string_push_back_noexcept_c)
        {
            if (!_uninhabited)
            {
                _buffer.push_back(expand_with);
                _uninhabited = byte_bits_c;
            }
            return _buffer.back();
        }

        constexpr inline size_t complete_byte_size() const noexcept
        {
            return (_uninhabited % byte_bits_c) //some unaligned bits exists
                ? _buffer.size() - 1
                : _buffer.size();
        }

        template <bool count_zeros>
        constexpr size_t sequence_width(size_t from) const noexcept
        {
            assert(from <= size());
            auto idx = from >> 3;
            size_t result = 0,
                to_ignore = from & 0b111 // number of bits to ignore from beginning
                ;
            byte_t ignore_mask = static_cast<byte_t>(
                std::numeric_limits<byte_t>::max() << (byte_bits_c - to_ignore));
            for (;
                idx < _buffer.size(); ++idx, to_ignore = 0, ignore_mask = 0)
            {
                byte_t byte;
                if constexpr (count_zeros)
                {
                    byte = ~_buffer[idx];
                    if ((idx + 1) == _buffer.size())//last (may be unpad) byte
                        byte ^= ((1 << _uninhabited) - 1);
                }
                else
                    byte = _buffer[idx];
                byte |= ignore_mask;
                auto n = std::countl_one(byte);
                result += n - to_ignore;
                if (n != byte_bits_c) //byte has zeros, stop iteration
                {
                    return result;
                }
            }
            return result;
        }
    };

}//ns:common
#endif //_OP_COMMON_BITBUILDER__H_
