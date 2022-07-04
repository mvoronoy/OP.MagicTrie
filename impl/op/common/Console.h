#ifndef _OP_COMMON_CONSOLE__H_
#define _OP_COMMON_CONSOLE__H_

#include <type_traits>
#include <iostream>
#include <op/common/OsDependedMacros.h>

#ifdef OP_COMMON_OS_WINDOWS
#include <windows.h>
#endif //OP_COMMON_OS_WINDOWS

namespace OP::console
{
    /**
    *   Esc secuence allows put special chars supported by Linux and Win10+ to color output text
    */
    template <char ... esc>
    struct Esc
    {
        constexpr static const char seq_c[] = {esc ...};
        constexpr static const size_t seq_size_c = std::extent_v<seq_c>;
    };

    
#if defined( OP_COMMON_OS_LINUX )
    inline
#endif // OP_COMMON_OS_LINUX

    namespace linux {

        template <char ... esc>
        using console_esc_t = Esc<'\x1b', '[', esc ..., 'm'>;

        using black_t = console_esc_t<'3', '0'>;
        using background_black_t = console_esc_t<'4', '0'>;
        using red_t = console_esc_t<'3', '1'>;
        using background_red_t = console_esc_t<'4', '1'>;
        using green_t = console_esc_t<'3', '2'>;
        using background_green_t = console_esc_t<'4', '2'>;
        using yellow_t = console_esc_t<'3', '3'>;
        using background_yellow_t = console_esc_t<'4', '3'>;
        using blue_t = console_esc_t<'3', '4'>;
        using background_blue_t = console_esc_t<'4', '4'>;
        using magenta_t = console_esc_t<'3', '5'>;
        using background_magenta_t = console_esc_t<'4', '5'>;
        using cyan_t = console_esc_t<'3', '6'>;
        using background_cyan_t = console_esc_t<'4', '6'>;
        using white_t = console_esc_t<'3', '7'>;
        using background_white_t = console_esc_t<'4', '7'>;
        using bright_black_t = console_esc_t<'9', '0'>;
        using background_bright_black = console_esc_t<'1', '0', '0'>;
        using bright_red_t = console_esc_t<'9', '1'>;
        using background_bright_red = console_esc_t<'1', '0', '1'>;
        using bright_green_t = console_esc_t<'9', '2'>;
        using background_bright_green = console_esc_t<'1', '0', '2'>;
        using bright_yellow_t = console_esc_t<'9', '3'>;
        using background_bright_yellow = console_esc_t<'1', '0', '3'>;
        using bright_blue_t = console_esc_t<'9', '4'>;
        using background_bright_blue = console_esc_t<'1', '0', '4'>;
        using bright_magenta_t = console_esc_t<'9', '5'>;
        using background_bright_magenta = console_esc_t<'1', '0', '5'>;
        using bright_cyan_t = console_esc_t<'9', '6'>;
        using background_bright_cyan = console_esc_t<'1', '0', '6'>;
        using bright_white_t = console_esc_t<'9', '7'>;
        using background_bright_white = console_esc_t<'1', '0', '7'>;
        using reset_t = console_esc_t<'0'>;
        using void_t = Esc<>;

        template <class ... TEscInit >
        struct LinuxColorer
        {
            using esc_init_t = std::tuple<TEscInit ...>;
            using esc_close_t = reset_t;

            LinuxColorer(std::ostream& console_os)
                : _console_os(&console_os)
            {
                _console_os->flush(); //prevent stuck symbols be colored with unexpected color
                run_sequence(std::make_index_sequence<std::tuple_size_v<esc_init_t>>{});
            }

            LinuxColorer(LinuxColorer&& other) noexcept
                :_console_os(other._console_os)
            {
                other._console_os = nullptr;
            }
            
            LinuxColorer() = delete;
            LinuxColorer(const LinuxColorer&) = delete;

            ~LinuxColorer()
            {
                if(_console_os)
                {
                    _console_os->write(esc_close_t::seq_c, esc_close_t::seq_size_c);
                    _console_os->flush();
                }
            }

        private:
            template<std::size_t... I>
            constexpr void run_sequence(std::index_sequence<I...>)
            {
                using elt_t = std::tuple_element_t<I, esc_init_t>;
                _console_os->write(elt_t::seq_c, elt_t::seq_size_c);
            }

            std::ostream* _console_os;
        };
        template <class ... TEscInit >
        using default_colorer_t = LinuxColorer<TEscInit ...>;
    } //ns: linux

#if defined( OP_COMMON_OS_WINDOWS )
}//ns:OP::console

#include  <Winbase.h>
namespace OP::console{

        inline namespace win_os {

        template <WORD color = 0>
        struct console_esc_t 
        {
            constexpr static WORD color_c = color;
        };

        using black_t = console_esc_t<>;
        using background_black_t = console_esc_t<>;
        using red_t = console_esc_t<FOREGROUND_RED>;
        using background_red_t = console_esc_t<BACKGROUND_RED>;
        using green_t = console_esc_t<FOREGROUND_GREEN>;
        using background_green_t = console_esc_t<BACKGROUND_GREEN>;
        using yellow_t = console_esc_t<FOREGROUND_RED| FOREGROUND_GREEN>;
        using background_yellow_t = console_esc_t<BACKGROUND_GREEN| BACKGROUND_RED>;
        using blue_t = console_esc_t<FOREGROUND_BLUE>;
        using background_blue_t = console_esc_t<BACKGROUND_BLUE>;
        using magenta_t = console_esc_t<FOREGROUND_BLUE| FOREGROUND_RED>;
        using background_magenta_t = console_esc_t<BACKGROUND_RED| BACKGROUND_BLUE>;
        using cyan_t = console_esc_t<FOREGROUND_GREEN| FOREGROUND_BLUE>;
        using background_cyan_t = console_esc_t<BACKGROUND_GREEN| BACKGROUND_BLUE>;
        using white_t = console_esc_t<FOREGROUND_GREEN| FOREGROUND_BLUE| FOREGROUND_RED>;
        using background_white_t = console_esc_t<BACKGROUND_GREEN| BACKGROUND_BLUE| BACKGROUND_RED>;
        using bright_black_t = console_esc_t<FOREGROUND_INTENSITY>;
        using background_bright_black = console_esc_t<BACKGROUND_INTENSITY>;
        using bright_red_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_RED>;
        using background_bright_red = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_RED>;
        using bright_green_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_GREEN>;
        using background_bright_green = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_GREEN>;
        using bright_yellow_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_RED| FOREGROUND_GREEN>;
        using background_bright_yellow = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_GREEN| BACKGROUND_RED>;
        using bright_blue_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_BLUE>;
        using background_bright_blue = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_BLUE>;
        using bright_magenta_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_RED| FOREGROUND_BLUE>;
        using background_bright_magenta = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_BLUE| BACKGROUND_RED>;
        using bright_cyan_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_BLUE| FOREGROUND_GREEN>;
        using background_bright_cyan = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_BLUE| BACKGROUND_GREEN>;
        using bright_white_t = console_esc_t<FOREGROUND_INTENSITY| FOREGROUND_RED| FOREGROUND_BLUE| FOREGROUND_GREEN>;
        using background_bright_white = console_esc_t<BACKGROUND_INTENSITY| BACKGROUND_RED| BACKGROUND_BLUE| BACKGROUND_GREEN>;
        using void_t = console_esc_t<>;

        template <class ... TEscInit>
        struct WindowsConsoleColorer
        {
            using esc_init_t = std::tuple<TEscInit ...>;
            constexpr static bool is_void_c =
                std::tuple_size_v< esc_init_t> == 0 
                || (std::tuple_size_v< esc_init_t> == 1 
                    && std::is_same_v<std::tuple_element_t <0, esc_init_t>,  void_t>);

            constexpr static WORD color_c =
                (TEscInit::color_c | ... | 0);

            WindowsConsoleColorer(std::ostream& os)
                : _console_os(&os)
            {
                _console_os->flush(); //prevent stuck symbols be colored with unexpected color
                if constexpr (!is_void_c)
                {
                    _h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

                    //store current state
                    CONSOLE_SCREEN_BUFFER_INFO csbi_info;
                    if (GetConsoleScreenBufferInfo(_h_stdout, &csbi_info))
                    {
                        _old_color_attr = csbi_info.wAttributes;
                        SetConsoleTextAttribute(_h_stdout, color_c);//ignore error result
                    }
                    else//on error, just disalow another ops
                    {
                        _h_stdout = INVALID_HANDLE_VALUE;
                    }
                }
            }
            
            WindowsConsoleColorer(WindowsConsoleColorer&& other) noexcept
                :_h_stdout(other._h_stdout)
                , _console_os(other._console_os)
                , _old_color_attr(other._old_color_attr)
            {
                other._h_stdout = INVALID_HANDLE_VALUE; //prevent from twice destructor apply
                other._console_os = nullptr;
            }

            WindowsConsoleColorer() = delete;
            WindowsConsoleColorer(const WindowsConsoleColorer&) = delete;

            ~WindowsConsoleColorer()
            {
                if(_h_stdout != INVALID_HANDLE_VALUE)
                {
                    _console_os->flush();
                    // Restore the original text colors
                    SetConsoleTextAttribute(_h_stdout, _old_color_attr);
                }
            }
        private:
            HANDLE _h_stdout = INVALID_HANDLE_VALUE;
            WORD _old_color_attr;
            std::ostream* _console_os;
        };

        template <class ... TEscInit >
        using default_colorer_t = WindowsConsoleColorer<TEscInit ...>;
    } //ns: win_os

#endif // OP_COMMON_OS_WINDOWS
    template <class V>
    struct WrapSeqBase
    {
        virtual std::ostream& print(std::ostream& os) const = 0;
    };

    /** Encode value to print with Esc<...> sequence */
    template <class Colorer, class V>
    struct WrapSeq : WrapSeqBase<V>
    {
        template <class U>
        constexpr WrapSeq(U&& u) noexcept
            : _v(std::forward<U>(u))
        {
        }

        ~WrapSeq()
        {
        }
        
        std::ostream& print(std::ostream& os) const override
        {
            auto colorer = bind(os);
            os << _v;
            return os;
        }

        const V _v;

        template <class ... ColorerArg>
        constexpr Colorer bind(ColorerArg&& ... arg) const
        {
            return Colorer(std::forward<ColorerArg>(arg)...);
        }
    private:
        bool _need_close = true;
    };

    /** Allows wrap data with color sequence print 
    * For example:
    *  std::cout << esc<red_t, background_bright_green>("Red text on bright green") << esc<blue_t>(4) << "\n";
    */
    template <class ... TColor, class V>
    constexpr auto esc(V && v) noexcept
    {
        return WrapSeq<default_colorer_t<TColor...>, V>(std::forward<V>(v));
    }

    template <class V>
    std::ostream& operator << (std::ostream& os, const WrapSeqBase<V>& colored_data)
    {
        return colored_data.print(os);
    }

}//OP::console
#endif //_OP_COMMON_CONSOLE__H_
