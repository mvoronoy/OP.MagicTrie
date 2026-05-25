#pragma once
#ifndef _OP_LEXICO_LEXCODEC__H_
#define _OP_LEXICO_LEXCODEC__H_

#include <cassert>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <bit>
#include <variant>

#include <op/common/BitBuilder.h>

namespace OP::lexico
{

namespace codevars
{
    struct invert_code
    {
        bool _value = false;
        constexpr invert_code() noexcept = default;
        constexpr invert_code(bool value) noexcept :_value(value) {}
    };

    constexpr inline invert_code invert_code_c{ true };

    struct section_pad
    {
        bool _value = false;
        constexpr section_pad() noexcept = default;
        constexpr section_pad(bool value) noexcept :_value(value) {}
    };

    template <class T, class ...Tx>
    constexpr inline bool check(const Tx&...tx) noexcept
    {
        return ((std::is_same_v<T, Tx> && tx._value) || ...);
    }


}//ns:codevars

template <auto ...algorithms, class TBitBuilder>
inline TBitBuilder& encode_zero(TBitBuilder& builder)
{
    using namespace codevars;

    if constexpr (check<invert_code>(algorithms...))
    {
        builder.append_uint(0b011u, 3); //matches to RealNumberPresentation's (nz)
    }
    else
    {
        builder.append_uint(0b100u, 3); //matches to RealNumberPresentation's (pz)
    }
    return builder; 
}

template <auto ...algorithms, class TBitBuilder, class Int>
inline TBitBuilder& encode_sign(TBitBuilder& builder, Int v)
{
    using namespace codevars;

    constexpr unsigned bit_len_c = 3;
    unsigned code = (v < 0) 
        ? 0b001u /*(nnpe)*/ 
        : (v == 0 ? 0b100u : 0b110u/*(pnpe)*/);

    if constexpr (check<invert_code>(algorithms...))
        code = ~code; 

    builder.append_uint(code, bit_len_c);
    return builder;
}


template <auto ...algorithms, class TBitBuilder, class UInt>
    requires std::unsigned_integral<UInt>
inline TBitBuilder& golomb_code(TBitBuilder& builder, UInt v)
{
    using namespace codevars;

    if (v == 0)
    {
        return encode_zero(builder);
    }

    const uint_fast8_t ln2 = std::bit_width(v);
    //create 1...1 mask
    const UInt mask = ln2 == std::numeric_limits<UInt>::digits
        ? (std::numeric_limits<UInt>::max()) //avoid UB on overflow
        : (UInt(1) << ln2) - 1;
    if constexpr (check<invert_code>(algorithms...))
    {
        builder
            .append_uint(static_cast<UInt>(~mask), ln2)
            .append_1() //stop bit    
            .append_uint(static_cast<UInt>(~v), ln2);
    }
    else
    {
        builder
            .append_uint(mask, ln2)
            .append_0() //stop bit    
            .append_uint(v, ln2);
    }
    return builder;
}

template <class TBitBuilder, class Int>
    requires std::signed_integral<Int>
inline TBitBuilder& golomb_code(TBitBuilder& builder, Int v)
{
    using namespace codevars;

    using local_uint_t = std::make_unsigned_t<Int>;
    //for positive numbers use UInt version
    if (v >= 0)
    {
        return golomb_code(
            builder, static_cast<local_uint_t>(v));
    }

    local_uint_t mantis = static_cast<local_uint_t>(-v);//make abs
    return golomb_code<invert_code_c>(builder, mantis);
}

template <auto ...algorithms, class TBitBuilder>
inline size_t revert_sign_bits(const TBitBuilder& builder, size_t from, bool& sign, bool& exponent_sign, bool& qualifier)
{
    using namespace codevars;

    unsigned num_sign{};
    constexpr size_t seq_bit_len = 3;
    builder.take_n(from, seq_bit_len, num_sign);
    if constexpr (check<invert_code>(algorithms...))
        num_sign = ~num_sign;
    sign = (num_sign & 0b100u);
    exponent_sign = (num_sign & 0b010u);
    qualifier = (num_sign & 0b001u);
    return seq_bit_len;
}


/** \brief Revert result of Golomb coding of previously encoded unsigned number.
    \return number of bits consumed from builder, starting from position `from`
*/
template <class TBitBuilder, class UInt>
    requires std::unsigned_integral<UInt>
inline size_t revert_golomb_code(const TBitBuilder& builder, size_t from, UInt& v, bool* result_sign = nullptr)
{
    using namespace codevars;
    const bool sign = builder.take_bit(from);
    size_t seq_bit_len = sign
            ? builder.sequence_width_1(from)
            : builder.sequence_width_0(from);
    if (result_sign)
        *result_sign = sign;
    if (seq_bit_len == 0) //detect 0 when no sign 
    {
        v = 0;
        return 1; // exact 1 bit for stop sequence
    }

    from += seq_bit_len;
    ++from; //skip stop-bit
    builder.take_n(from, seq_bit_len, v);
    return 2 * seq_bit_len + 1;
}

/**
* @return number of bits extracted from builder
*/
template <auto ... algorithms, class TBitBuilder, class Int>
    requires std::signed_integral<Int>
inline size_t revert_golomb_code(const TBitBuilder& builder, size_t from, Int& v)
{
    using namespace codevars;
    //take sign bit
    unsigned classifier = 0;
    size_t seq_bit_len = 0;
    bool sign = builder.take_bit(from + seq_bit_len);

    const auto mantis_width = sign
        ? builder.sequence_width_1(from + seq_bit_len)
        : builder.sequence_width_0(from + seq_bit_len)
        ;
    seq_bit_len += mantis_width + 1; // +1 -skip stop bit
    using local_unsigned_t = std::make_unsigned_t<Int>;
    local_unsigned_t mantis{};
    builder.take_n(from + seq_bit_len, mantis_width, mantis);
    if (!sign) //negative
    {
        const local_unsigned_t mask = mantis_width == std::numeric_limits<local_unsigned_t>::digits
            ? (std::numeric_limits<local_unsigned_t>::max()) //avoid UB on overflow
            : (local_unsigned_t{ 1 } << mantis_width) - 1;
        v = -std::bit_cast<Int>(mask ^ mantis);
    }
    else
        v = mantis;
    return seq_bit_len + mantis_width;
}

template <class T>
    requires std::floating_point<T>
struct RealNumberPresentation
{
    using num_defs_t = std::numeric_limits<T>;
    /*
    *
    *   111 - positive outliers
    *   110 - positive num, positive exp (pnpe)
    *   101 - positive num, negative exp (pnne)
    *   100 - positive zero (pz)
    *   011 - negative zero (nz)
    *   010 - neg num, negative exp (nnne)
    *   001 - neg num, positive exp (nnpe)
    *   000 - neg. outliers
    *
    */
    struct Classifier { unsigned value : 8; unsigned size : 3; T def; };
    /**
    * Negative NaN. Number is less than negative infinity
    * encode: [000-negative-outlier][0]
    */
    constexpr inline static Classifier negative_nan_c =
    { 0b0000u, 4, -num_defs_t::quiet_NaN() };
    /** Negative infinity.
    * encode: [000-negative-outlier][1]
    */
    constexpr inline static Classifier negative_inf_c = { 0b0001u, 4, -num_defs_t::infinity() };
    constexpr inline static Classifier nnpe_c = { 0b001u, 3, T{-1.0} };
    constexpr inline static Classifier nnne_c = { 0b010u, 3, T{-0.1} };
    //encodes (-0.0) [011-negative zero]
    constexpr inline static Classifier negative_zero_c = { 0b011u, 3, -T{0} };

    //encodes (+0.0) [100-positive zero]
    constexpr inline static Classifier positive_zero_c = { 0b100u, 3, T{0} };
    constexpr inline static Classifier pnne_c = { 0b101u, 3, T{0.1} };
    constexpr inline static Classifier pnpe_c = { 0b110u, 3, T{1.0} };
    /**
    * Positive infinity.
    * encode: [11-positive-outlier][1][0-stop]
    */
    constexpr inline static Classifier positive_inf_c = { 0b1110u, 4, num_defs_t::infinity() };
    /**
    * Positive  NaN. Number is bigger than positive infinity.
    * encode: [11-positive-outlier][1][1]
    */
    constexpr inline static Classifier positive_nan_c = { 0b1111u, 4, num_defs_t::quiet_NaN() };

    template <class TBitBuilder>
    static TBitBuilder& decompose(TBitBuilder& builder, T x)
    {
        T frac{};
        // Extract Mantissa and Exponent
        int exp_2 = 0;
        unsigned head = pnpe_c.value;
        const bool sign = std::signbit(x);

        if (sign)
        {//negative
            if (x == T{ 0 })
                return builder.append_uint(
                    negative_zero_c.value, negative_zero_c.size);
            //check for negative outliers
            if (std::isnan(x)) //the negative number that ever less than infinity
                return builder.append_uint(negative_nan_c.value, negative_nan_c.size);
            if (std::isinf(x)) //negative infinity
                return builder.append_uint(negative_inf_c.value, negative_inf_c.size);
            frac = x * T{ -1 };  //make abs
            //@!!head = frac < T{ 1 } ? nnne_c.value : nnpe_c.value;
            frac = std::frexp(frac, &exp_2); //in [0.5, 1)
            head = exp_2 < 1 ? nnne_c.value : nnpe_c.value;
            exp_2 *= -1;// revert exponent for negatives, so: (-0.1) > (-1)
        }
        else
        {
            if (x == T{ 0 })
                return builder.append_uint(positive_zero_c.value, positive_zero_c.size);
            //check for positive outliers
            if (std::isnan(x)) //the positive number that ever bigger than infinity
                return builder.append_uint(positive_nan_c.value, positive_nan_c.size);
            if (std::isinf(x)) //negative infinity
                return builder.append_uint(positive_inf_c.value, positive_inf_c.size);
            frac = std::frexp(x, &exp_2); //in [0.5, 1)
            if (x < T{ 1 })
                //@!! if ( exp_2 < 0 )
                head = pnne_c.value;
        }
        builder.append_uint(head, 3);
        //already encoded exponent sign bit in the head 
        golomb_code(builder, exp_2);


        constexpr int upper_estimate_c = std::numeric_limits<T>::digits; //biggest possible size of mantis
        using mantisa_holder_t = std::uint8_t;
        constexpr int holder_bits_c = std::numeric_limits<mantisa_holder_t>::digits;

        unsigned i = 0;
        int consume_digits = 0; //will keep last entry value
        for (int rest_digits = upper_estimate_c; rest_digits > 0 && frac > 0; ++i)
        {
            consume_digits = std::min(holder_bits_c, rest_digits);
            frac = std::ldexp(frac, consume_digits);
            mantisa_holder_t integer_part = static_cast<mantisa_holder_t>(frac);
            //
            mantisa_holder_t mantissa = sign
                ? static_cast<mantisa_holder_t>(~integer_part)
                : integer_part;
            frac -= integer_part;
            rest_digits -= consume_digits;
            builder.append_uint(mantissa, consume_digits);
        }

        return builder;
    }

    template <class TBitBuilder>
    static size_t take_real(TBitBuilder& builder, size_t from, T& v)
    {
        using namespace codevars;

        bool is_positive{}, is_exp_positive{}, sign_qualifier{};
        size_t seq_bit_len = revert_sign_bits(builder, from, is_positive, is_exp_positive, sign_qualifier);
        if (is_positive && !is_exp_positive && !sign_qualifier)
        {
            v = 0;
            return seq_bit_len;
        }

        const int sign = is_positive ? 1 : -1;
        const int exp_sign = (is_exp_positive ? 1 : -1);
        //some bit-trick to distinct regular values from explicit_value
        const bool is_outlier = //(head & 0b011u) == 0 || (head & 0b011u) == 3;
            (!is_exp_positive && !sign_qualifier) || (is_exp_positive && sign_qualifier);
            
        if (is_outlier)
        {
            if (!is_positive && is_exp_positive && sign_qualifier)
            {
                v = negative_zero_c.def;
            }
            else if (is_positive && !is_exp_positive && !sign_qualifier)
            {
                v = positive_zero_c.def;
            }
            else //need 4 bits to recognize
            {
                seq_bit_len = 4;
                unsigned head{};
                builder.take_n(from, seq_bit_len, head = 0);
                constexpr static std::array<Classifier, 4> explicit_value{ //sorted!
                    negative_nan_c, negative_inf_c, positive_inf_c, positive_nan_c
                };
                for (const auto& x : explicit_value)
                    if (x.value == head)
                    {
                        v = x.def;
                        break;
                    }
            }
            return seq_bit_len;
        }
        from += seq_bit_len;
        int exp_2;
        size_t bit_width = revert_golomb_code(builder, from, exp_2);
        exp_2 *= sign;

        // can number be zero?
        if (bit_width == 0)
        {
            v = std::copysign(T{ 0 }, static_cast<T>(sign)); //may recreate (-0.0)
            return seq_bit_len;
        }
        from += bit_width;
        seq_bit_len += bit_width;
        using mantissa_holder_t = std::uint8_t;
        constexpr int mantissa_digits_c = std::numeric_limits<T>::digits;
        constexpr size_t holder_bits_c = std::numeric_limits<mantissa_holder_t>::digits;
        constexpr size_t mantissa_byte_width = (mantissa_digits_c + holder_bits_c - 1) / holder_bits_c;
        std::array<T, mantissa_byte_width> mantissa{};
        size_t take = 0;
        size_t i = 0;
        for (; i < mantissa_byte_width && from < builder.size(); ++i)
        {
            take = std::min(holder_bits_c, builder.size() - from);
            unsigned u{};
            builder.take_n(from, take, u);
            if (!is_positive)
            { //invert only extracted bits, 
                //no need to protect from `<<` overflow since `unsigned` wider than `mantissa_holder_t`
                u ^= (1 << take) - 1;
            }
            mantissa[i] = static_cast<T>(u);
            from += take;
            seq_bit_len += take;
        }
        T frac{};
        // start from last
        for (; i > 0; --i)
        {
            frac += mantissa[i - 1];
            //last `take` from previous loop will be applied in the first order
            frac = std::ldexp(frac, -static_cast<int>(take));
            take = holder_bits_c;
        }
        frac = std::ldexp(frac, exp_2);
        v = std::copysign(frac, static_cast<T>(sign));

        return seq_bit_len;
    }
};


    namespace details
    {
        
        // Variadic case for 2 or more arguments
        template <typename T, typename... Args>
        constexpr decltype(auto) _mx(T&& head, T&& second, Args&&... tail) 
        {
            if constexpr (sizeof ...(tail))
                // Unpacks the tail using a binary left fold expression
                return _mx(std::max(std::forward<T>(head), std::forward<T>(second)), std::forward<Args>(tail)...);
            else
                return std::max(std::forward<T>(head), std::forward<T>(second));
        }
    
    }
    

    template <class T, class TBitBuilder>
        requires std::floating_point<T>
    TBitBuilder& encode(TBitBuilder& builder, T t, size_t padding = 0)
    {
        using real_presentation_t = RealNumberPresentation<T>;
        const auto start_pos = builder.size();
        real_presentation_t::decompose(builder, t);
        if (auto effect_len = (builder.size() - start_pos); padding > effect_len)
        { // need pads at the end
            auto pad_bits = padding - effect_len;
            std::uint64_t mask = t < 0 ? std::numeric_limits<std::uint64_t>::max():0;
            builder.append_uint(mask, static_cast<std::uint_fast8_t>(pad_bits));
        }

        return builder;
    }

    template <class Int, class TBitBuilder>
        requires std::integral<Int>
    TBitBuilder& encode(TBitBuilder& builder, Int v, size_t padding = 0)
    {
        encode_sign(builder, v);
        using local_unsigned_t = std::make_unsigned_t<Int>;
        int sign = (v < 0 ? -1 : 1);
        //add value that plays role of exponent for real numbers (not needed for restore, but crucial to compare with other values)
        local_unsigned_t mantissa = static_cast<local_unsigned_t>(v * sign);
        int exp = std::bit_width(mantissa);
        int ln2 = exp;
        //if (mantissa > 0)
        //    ln2 += 1;
        golomb_code(builder, ln2 * sign);
        const auto start_pos = builder.size();
        builder.append_uint(
            static_cast<local_unsigned_t>(sign < 0 ? ~mantissa : mantissa), exp);
        
        if (auto effect_len = (builder.size() - start_pos); padding > effect_len)
        { // need pads at the end
            auto pad_bits = padding - effect_len;
            std::uint64_t mask = sign < 0 ? std::numeric_limits<std::uint64_t>::max() : 0;
            builder.append_uint(mask, static_cast<std::uint_fast8_t>(pad_bits));
        }
        return builder;
        //return encode(builder, static_cast<float>(v));
    }

    template<class ...Tx, class TBitBuilder>
    TBitBuilder& encode(TBitBuilder& builder, const std::variant<Tx...>& v)
    {
        constexpr auto max_pad = details::_mx(std::numeric_limits<Tx>::digits...);
        //static_assert(std::is_arithmetic_v<Tx> && ...);
        std::visit([&](auto num) {
                encode(builder, num, max_pad);
            }, v);
        return builder;
    }

    template <class TBitBuilder, class T>
        requires std::integral<T>
    size_t decode(const TBitBuilder& builder, T& t, size_t from = 0)
    {
        bool sign{}, exponent_sign{}, qualifier{};
        size_t off = revert_sign_bits(
            builder, from, sign, exponent_sign, qualifier);
        int exponent{}; //will ignore this
        off += revert_golomb_code(builder, from + off, exponent);
        return off + revert_golomb_code(builder, from + off, t);
    }

    template <class TBitBuilder, class T>
        requires std::floating_point<T>
    size_t decode(TBitBuilder& builder, T& t, size_t from = 0)
    {
        using real_presentation_t = RealNumberPresentation<T>;
        return real_presentation_t::take_real(builder, from, t);
    }

}//ns:OP::lexico

#endif //_OP_LEXICO_LEXCODEC__H_