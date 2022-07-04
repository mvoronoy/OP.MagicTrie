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


} //end of namespace OP
#endif //_OP_RANGE__H_
