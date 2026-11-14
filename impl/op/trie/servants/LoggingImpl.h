#pragma once

#ifndef _OP_TRIE_SERVANTS_LOGGINGIMPL__H_
#define _OP_TRIE_SERVANTS_LOGGINGIMPL__H_

#include <iostream>

#include <op/common/SysLog.h>

namespace OP::trie::servants
{
    struct LoggingImpl : Logging
    {
        virtual void log(Level level, const char* message) noexcept override
        {
            //if exception happens, it is going to std::terminate entire app            
            prefix(level, std::clog) << message << '\n';
            if(level > info)
                std::clog.flush();
        }

    private:
        
        #define SERVANTS_LOG_LVL_PREFIX(prefix) case prefix: {os << #prefix; break;}

        template <class Os>
        Os& prefix(Level level, Os& os) 
        {
            os << "[";
            switch(level)
            {
            SERVANTS_LOG_LVL_PREFIX(debug)
            default:
                [[fallthrough]];
            SERVANTS_LOG_LVL_PREFIX(info)
            SERVANTS_LOG_LVL_PREFIX(warning)
            SERVANTS_LOG_LVL_PREFIX(error)
            SERVANTS_LOG_LVL_PREFIX(fatal)
            }
            os << "|thr:" << std::this_thread::get_id() << "] ";
            return os;
        }

        #undef SERVANTS_LOG_LVL_PREFIX

    };

}//ns:OP::trie
#endif //_OP_TRIE_SERVANTS_LOGGINGIMPL__H_
