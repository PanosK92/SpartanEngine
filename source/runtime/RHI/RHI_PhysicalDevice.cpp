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
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    const char* PhysicalDevice::decode_api_version(const uint32_t version)
    {
        static char api_version_str[64];
        uint32_t major = (version >> 22);
        uint32_t minor = (version >> 12) & 0x3ff;
        uint32_t patch = version & 0xfff;
    
        snprintf(api_version_str, sizeof(api_version_str), "%u.%u.%u", major, minor, patch);
        return api_version_str;
    }
    
    const char* PhysicalDevice::decode_driver_version(const uint32_t version, const char* driver_info)
    {
        static char driver_version_str[128];
    
        if (IsNvidia())
        {
            uint32_t major     = (version >> 22) & 0x3ff;
            uint32_t minor     = (version >> 14) & 0x0ff;
            uint32_t secondary = (version >> 6)  & 0x0ff;
            uint32_t tertiary  = version         & 0x003f;
    
            snprintf(driver_version_str, sizeof(driver_version_str), "%u.%u.%u.%u", major, minor, secondary, tertiary);
            return driver_version_str;
        }
    
        if (IsAmd())
        {
            // for amd gpus, driver_info matches the adrenalin version, version is an internal version
            if (driver_info)
            {
                strncpy_s(driver_version_str, sizeof(driver_version_str), driver_info, _TRUNCATE);

                // truncate at first space
                char* space_ptr = strchr(driver_version_str, ' ');
                if (space_ptr) *space_ptr = '\0';
                return driver_version_str;
            }
            return "Unknown AMD driver";
        }
    
        if (IsIntel())
        {
            uint32_t major = (version >> 14);
            uint32_t minor = version & 0x3fff;
            snprintf(driver_version_str, sizeof(driver_version_str), "%u.%u", major, minor);
            return driver_version_str;
        }
    
        return "Unable to determine driver version";
    }
}
