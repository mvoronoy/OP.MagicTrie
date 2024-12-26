#pragma once
#ifndef _OP_NULLSTREAM__H_
#define _OP_NULLSTREAM__H_

#include <iostream>

namespace OP
{
    /** no-op stream-buffer */
    class NullStreamBuffer : public std::streambuf
    {

    public:
        int overflow(int c) final { return c; }
    };

    class NullStream : public std::ostream
    {
        NullStreamBuffer _null_buffer = {};

    public:
        NullStream() 
            : std::ostream(&_null_buffer) {}

        NullStream(const NullStream&) = delete;
        NullStream& operator=(const NullStream&) = delete;

    };

} //end of namespace OP

#endif //_OP_NULLSTREAM__H_
