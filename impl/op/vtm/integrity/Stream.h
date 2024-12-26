#pragma once
#ifndef _OP_VTM_INTEGRITY_STREAM__H_
#define _OP_VTM_INTEGRITY_STREAM__H_

#include <iostream>
#include <atomic>
#include <op/common/NullStream.h>

namespace OP::vtm::integrity
{
    struct Stream
    {
        static inline ::OP::NullStream _defult_instance = {};

        static inline std::ostream& os()
        {
            return *(instance()._stream.load());
        }

        /** \return previous value of stream */
        static inline std::ostream& os(std::ostream& other)
        {
            return *(instance()._stream.exchange(&other));
        }

    private:
        static Stream& instance() noexcept
        {
            static Stream zhis{};
            return zhis;
        }
        std::atomic<std::ostream*> _stream = {&_defult_instance};
    };

}//ns::OP::vtm

#endif //_OP_VTM_INTEGRITY_STREAM__H_
