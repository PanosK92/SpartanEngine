/*
Copyright(c) 2015-2025 Panos Karabelas

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
#ifdef _WIN32
#include <Windows.h>
#endif
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
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
            #ifdef _WIN32
            HKEY hKey;
            const char* subkey     = "SOFTWARE\\AMD\\CN";
            const char* value_name = "OldReleaseVersion";
            char value[255];
            DWORD value_length = 255;
            
            if (RegOpenKeyExA(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                if (RegQueryValueExA(hKey, value_name, nullptr, nullptr, (LPBYTE)&value, &value_length) == ERROR_SUCCESS)
                {
                    RegCloseKey(hKey);
            
                    // remove everything after the version number (a bunch of codes and text)
                    string version_amd = value;
                    size_t pos_dash = version_amd.find('-');
                    if (pos_dash != string::npos)
                    {
                        version_amd = version_amd.substr(0, pos_dash);
                    }
                    
                    // extract year, month, and revision
                    size_t pos1     = version_amd.find('.');
                    size_t pos2     = version_amd.find('.', pos1 + 1);
                    size_t pos3     = version_amd.find('.', pos2 + 1);               // find the fourth dot
                    string year     = version_amd.substr(0, pos1);
                    string month    = version_amd.substr(pos1 + 1, pos2 - pos1 - 1); // exclude the dot
                    string revision = version_amd.substr(pos3 + 1);                  // start from the fourth dot

                    return year + "." + month + "." + revision;
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
