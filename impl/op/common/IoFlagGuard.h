#ifndef _OP_IOFLAGGUARD__H_
#define _OP_IOFLAGGUARD__H_

#include <iostream>

namespace OP
{
    /** RAII pattern to save/restore flags of std::stream to avoid altering
    shared streams (like std::cout) parameters by manipulators like: 
        \li boolalpha
        \li hex
        \li fill
        \li ...
    Usage: \code
        {//RAII scope
           IoFlagGuard guard( std::cout );
           //modify stream in local scope
           std::cout << std::hex << std::setw(16) << std::setfill('0')
                    << 0xAA55 << '\n';
        } //end scope
        std::cout << 42 << '\n'; //print as regular decimal    
    \endcode

    */
    template <class TStream = std::ostream>
    struct IoFlagGuard
    {
        IoFlagGuard(TStream& stream)
            : _stream(stream)
            , _origin_flags(_stream.flags())
            , _format(nullptr) 
        {
            _format.copyfmt(stream);
        }
        ~IoFlagGuard()
        {
            reset();
        }
        void reset()
        {
            _stream.copyfmt(_format);
            _stream.flags( _origin_flags );
        }
    private:
        TStream& _stream;
        std::ios_base::fmtflags _origin_flags;
        std::ios _format;
    };

    namespace console
    {
        template <char ... esc>
        struct Esc
        {
            constexpr static const char seq_c[] = {'\x1b', '[',  esc ..., 'm', '\x0'};
        };

        using black_t = Esc<'3', '0'>;
        using background_black_t = Esc<'4', '0'>;
        using red_t = Esc<'3', '1'>;
        using background_red_t = Esc<'4', '1'>;
        using green_t = Esc<'3', '2'>;
        using background_green_t = Esc<'4', '2'>;
        using yellow_t = Esc<'3', '3'>;
        using background_yellow_t = Esc<'4', '3'>;
        using blue_t = Esc<'3', '4'>;
        using background_blue_t = Esc<'4', '4'>;
        using magenta_t = Esc<'3', '5'>;
        using background_magenta_t = Esc<'4', '5'>;
        using cyan_t = Esc<'3', '6'>;
        using background_cyan_t = Esc<'4', '6'>;
        using white_t = Esc<'3', '7'>;
        using background_white_t = Esc<'4', '7'>;
        using bright_black_t = Esc<'9', '0'>;
        using background_bright_black = Esc<'1', '0', '0'>;
        using bright_red_t = Esc<'9', '1'>;
        using background_bright_red = Esc<'1', '0', '1'>;
        using bright_green_t = Esc<'9', '2'>;
        using background_bright_green = Esc<'1', '0', '2'>;
        using bright_yellow_t = Esc<'9', '3'>;
        using background_bright_yellow = Esc<'1', '0', '3'>;
        using bright_blue_t = Esc<'9', '4'>;
        using background_bright_blue = Esc<'1', '0', '4'>;
        using bright_magenta_t = Esc<'9', '5'>;
        using background_bright_magenta = Esc<'1', '0', '5'>;
        using bright_cyan_t = Esc<'9', '6'>;
        using background_bright_cyan = Esc<'1', '0', '6'>;
        using bright_white_t = Esc<'9', '7'>;
        using background_bright_white = Esc<'1', '0', '7'>;
        using reset_t = Esc<'0'>;
        using void_t = Esc<>;

        /** Encode value to print with Esc<...> sequence */
        template <class TEscInit, class V, 
            class TEscClose = std::conditional_t<std::is_same_v<TEscInit, void_t>, void_t, reset_t> >
        struct WrapSeq
        {
            using esc_init_t = TEscInit;
            using esc_close_t = TEscClose;
            
            template <class U>
            constexpr WrapSeq(U&& u) noexcept
                : _v(std::forward<U>(u))
                {}
            const V _v;
        };

        /** Allows wrap data with color sequence print 
        * For example:
        *  std::cout << esc<red_t>("Red text") << esc<blue_t>(4) << "\n";
        */
        template <class TEsc, class V>
        constexpr auto esc(V && v) noexcept
        {
            return WrapSeq<TEsc, V>(std::forward<V>(v));
        }

        template <class T, class V>
        std::ostream& operator << (std::ostream& os, const WrapSeq<T, V>& colored)
        {
            using seq_t = WrapSeq<T, V>;
            os << seq_t::esc_init_t::seq_c << colored._v << seq_t::esc_close_t::seq_c;
            return os;
        }

    }

} //end of namespace OP
#endif //_OP_RANGE__H_
