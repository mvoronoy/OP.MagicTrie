#pragma once

#ifndef _OP_UTEST_PLATFORM_WINSYSINFO__H_
#define _OP_UTEST_PLATFORM_WINSYSINFO__H_

#include <optional>
#include <fstream>
#include <string>
#include <ctype.h>

namespace OP::utest::sysinfo::platform
{
    inline OP::utest::sysinfo::Measure<float, std::mega/*10e6*/> cpu_frequency() noexcept
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;

        while (cpuinfo.good() && std::getline(cpuinfo, line)) 
        {
            if (auto pos = line.find("cpu MHz"); pos != std::string::npos) {
                for(; pos < line.size(); ++pos)
                {
                    if( std::isdigit(line[pos]) )
                        return std::stof(line, pos);
                }
                //something wrong with info
                return {};
            }
        }
        return {};//indicate an error
    }
}//ns:OP::utest::sysinfo

#endif //_OP_UTEST_PLATFORM_WINSYSINFO__H_
