#pragma once
#ifndef _OP_COMMON_SYSLOG__H_
#define _OP_COMMON_SYSLOG__H_

#include <op/common/OsDependedMacros.h>
#include <string>

#ifdef  OP_COMMON_OS_LINUX
#include <syslog.h>
#endif //OP_COMMON_OS_LINUX

#ifdef OP_COMMON_OS_WINDOWS
//#include <Windows.h>
#endif //OP_COMMON_OS_WINDOWS

namespace OP
{
    namespace utils
    {
        /**
        *   Non-crossplatform way to put log information to operation system.
        *      - Windows uses OutputDebugString
        *      - Linux uses syslog  
        */
        class SysLog
        {
            struct Transport
            {
#if defined (OP_COMMON_OS_WINDOWS)
                Transport()
                {}
                ~Transport()
                {}
                void print(const char* s)
                {
                    OutputDebugStringA(s);
                }
#elif defined(OP_COMMON_OS_LINUX) //asume LINUX platform
                Transport()
                {
                    openlog("", LOG_PID | LOG_CONS | LOG_NDELAY,
                        LOG_USER);
                }
                ~Transport()
                {
                    closelog();
                }
                static void print(const char* s)
                {
                    syslog(LOG_ERR, "%s", s);
                }
#endif // _WINDOWS / LINUX
            };//Transport
            Transport _transport;
        public:
            void print(const char* s)
            {
                _transport.print(s);
            }
        };//SysLog
    }//ns:utils
}//ns:OP

#endif //_OP_COMMON_SYSLOG__H_
