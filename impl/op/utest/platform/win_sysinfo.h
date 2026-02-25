#pragma once

#ifndef _OP_UTEST_PLATFORM_WINSYSINFO__H_
#define _OP_UTEST_PLATFORM_WINSYSINFO__H_

#include <optional>
#include <windows.h>

namespace OP::utest::sysinfo::platform
{
    inline OP::utest::sysinfo::Measure<float, std::mega/*10e6*/> cpu_frequency() noexcept
    {
        HKEY hKey;
        LONG lRes = RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0,
            KEY_READ,
            &hKey
        );

        if (lRes == ERROR_SUCCESS) {
            DWORD data;
            DWORD dataSize = sizeof(DWORD);

            RegQueryValueExA(hKey, "~MHz", nullptr, nullptr,
                             (LPBYTE)&data, &dataSize);

            float result = static_cast<float>( data );// MHz
            RegCloseKey(hKey);
            return OP::utest::sysinfo::Measure<float, std::mega/*10^-6*/>{result};
        }
        return {};//indicate an error
    }
}//ns:OP::utest::sysinfo

#endif //_OP_UTEST_PLATFORM_WINSYSINFO__H_