/*
Copyright(c) 2016-2024 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ==================
#include "pch.h"
#include "RHI_PhysicalDevice.h"
#ifdef _MSC_VER
#include <Windows.h>
#endif
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    string PhysicalDevice::decode_api_version(const uint32_t version)
    {
        char buffer[256];

        // vulkan version convention
        uint32_t major = (version >> 22);
        uint32_t minor = (version >> 12) & 0x3ff;
        uint32_t patch = version & 0xfff;
        sprintf(buffer, "%d.%d.%d", major, minor, patch);

        return buffer;
    }

    string PhysicalDevice::decode_driver_version(const uint32_t version)
    {
        if (IsNvidia())
        {
            char buffer[256];

            uint32_t major     = (version >> 22) & 0x3ff;
            uint32_t minor     = (version >> 14) & 0x0ff;
            uint32_t secondary = (version >> 6)  & 0x0ff;
            uint32_t tertiary  = version         & 0x003f;

            sprintf(buffer, "%d.%d.%d.%d", major, minor, secondary, tertiary);

            return buffer;
        }

        if (IsAmd())
        {
            #ifdef _MSC_VER
            HKEY hKey;
            const char* subkey     = "SOFTWARE\\AMD\\CN";
            const char* value_name = "WizardProfileReleaseVer";
            char value[255];
            DWORD value_length = 255;

            if (RegOpenKeyExA(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                if (RegQueryValueExA(hKey, value_name, nullptr, nullptr, (LPBYTE)&value, &value_length) == ERROR_SUCCESS)
                {
                    RegCloseKey(hKey);
                    return string(value);
                }
                RegCloseKey(hKey);
            }
            #endif
        }

        if (IsIntel())
        {
            char buffer[256];

            uint32_t major = (version >> 14);
            uint32_t minor = version & 0x3fff;

            sprintf(buffer, "%d.%d", major, minor);

            return buffer;
        }

        return "Unable to determine driver version";
    }
}
