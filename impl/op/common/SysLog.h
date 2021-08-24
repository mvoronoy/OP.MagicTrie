#pragma once
#ifndef _OP_COMMON_SYSLOG__H_
#define _OP_COMMON_SYSLOG__H_

#ifdef _WINDOWS
    #include <Windows.h>
#ifdef max
#undef max
#endif
#else //asume LINUX platform
    #include <syslog.h>
#endif // _WINDOWS
#include <string>

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
#ifdef _WINDOWS
                Transport()
                {}
                ~Transport()
                {}
                void print(const std::string& s)
                {
                    OutputDebugStringA(s.c_str());
                }
#else //asume LINUX platform
                Transport()
                {
                    openlog("", LOG_PID | LOG_CONS | LOG_NDELAY,
                        LOG_USER);
                }
                ~Transport()
                {
                    closelog();
                }
                void print(const std::string& s)
                {
                    syslog(LOG_ERR, s.c_str());
                }
#endif // _WINDOWS
            };//Transport
            Transport _transport;
        public:
            void print(const std::string& s)
            {
                _transport.print(s.c_str());
            }
        };//SysLog
    }//ns:utils
}//ns:OP

#endif //_OP_COMMON_SYSLOG__H_
