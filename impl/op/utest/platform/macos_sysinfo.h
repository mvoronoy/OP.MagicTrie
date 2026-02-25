#pragma once

#ifndef _OP_UTEST_PLATFORM_MACOSSYSINFO__H_
#define _OP_UTEST_PLATFORM_MACOSSYSINFO__H_

#include <optional>
#include <sys/sysctl.h>

namespace OP::utest::sysinfo::platform
{
    inline OP::utest::sysinfo::Measure<float, std::giga/*10e9*/> cpu_frequency() noexcept
    {
        uint64_t freq = 0;
        size_t size = sizeof(freq);

        sysctlbyname("hw.cpufrequency", &freq, &size, NULL, 0);
        return freq / 1e9.f;
    }
}//ns:OP::utest::sysinfo::platform

#endif //_OP_UTEST_PLATFORM_MACOSSYSINFO__H_
