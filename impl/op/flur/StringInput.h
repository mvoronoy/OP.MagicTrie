#pragma once
#ifndef _OP_FLUR_STRINGINPUT__H_
#define _OP_FLUR_STRINGINPUT__H_
namespace OP::flur
{
    /**
    *   Split string by one of the separators
    *@tparam Str sequence to split
    *@param separators string of separators, any symbol from it is treated as possible separator
    */
    template <class Str, class StrView>
    struct StringSplit : Sequence< StrView > 
    {
        using base_t = Sequence< StrView >;
        using element_t = typename base_t::element_t;

        constexpr StringSplit(Str str, Str separator)
            :_hold(std::move(str))
            , _separators(std::move(separator))
            , _start( std::string::npos )
            , _endp( std::string::npos )
            {}

        virtual void start()
        {
            auto& s = details::get_reference(_hold);
            _start = s.find_first_not_of(_separators);
            _endp = s.find_first_of(
                details::get_reference(_separators), _start);
        }
        virtual bool in_range() const
        {
            return (_start < _hold.size())
                ;
        }
        virtual element_t current() const
        {
            auto& s = details::get_reference(_hold);
            return element_t(s.data() + _start, 
                (_endp == std::string::npos ? s.size() : _endp ) - _start);
        }
        virtual void next()
        {
            auto& s = details::get_reference(_hold);

            if (_endp == std::string::npos)
            {
                _start = _endp;
                return;
            }
            
            _start = s.find_first_not_of(_separators, _endp + 1);
            _endp = s.find_first_of(_separators, _start);
            //} while(_start == _endp || _endp != std::string::npos);
        }

    private:
        size_t _start, _endp;
        Str _hold, _separators;    
    };


} //ns: OP::flur
#endif //_OP_FLUR_STRINGINPUT__H_

