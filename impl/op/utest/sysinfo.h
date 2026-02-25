#pragma once
#ifndef _OP_UTEST_SYSINFO__H_
#define _OP_UTEST_SYSINFO__H_

#include <ratio> 
#include <optional> 
#include <op/common/OsDependedMacros.h>

//////////////////////////////////////////////////////////////
/// Disclaimer! : JUST TO ESTIMATE
/// Use more professional library like hwloc (https://www.open-mpi.org/projects/hwloc/doc/)
//////////////////////////////////////////////////////////////

namespace OP::utest::sysinfo
{

    template <typename V, typename Ratio>
    struct Measure : std::optional<V>
    {
        using ratio_t = Ratio;
        using base_t = std::optional<V>;

        using base_t::base_t;
    };

}//ns:OP::utest::sysinfo

#ifdef OP_COMMON_OS_WINDOWS
#include <op/utest/platform/win_sysinfo.h>
#elif defined(OP_COMMON_OS_LINUX)
#include <op/utest/platform/linux_sysinfo.h>
#elif defined(OP_COMMON_OS_MACOS)
#include <op/utest/platform/linux_sysinfo.h>
#else
#include <op/utest/platform/fallback.h>
#endif // OS-MACROS

namespace OP::utest::sysinfo
{
    /** \return OP::utest::sysinfo::Measure<float, ratio>, where ratio OS-specific (ether std::micro - 10^-6*, std::nano 10^-9)
    */
    inline auto cpu_frequency() noexcept
    {
        return OP::utest::sysinfo::platform::cpu_frequency();
    }
}//ns:OP::utest::sysinfo
#endif //_OP_UTEST_SYSINFO__H_
