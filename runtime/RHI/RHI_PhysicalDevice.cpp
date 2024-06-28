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
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    string PhysicalDevice::decode_api_version(const uint32_t version)
    {
        char buffer[256];

        // vulkan version conventions
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
            uint32_t secondary = (version >> 6) & 0x0ff;
            uint32_t tertiary  = version & 0x003f;
            sprintf(buffer, "%d.%d.%d.%d", major, minor, secondary, tertiary);

            return buffer;
        }
        else if (IsIntel())
        {
            char buffer[256];

            uint32_t major = (version >> 14);
            uint32_t minor = version & 0x3fff;
            sprintf(buffer, "%d.%d", major, minor);

            return buffer;
        }

        // Use the Vulkan convention for all other vendors as we don't have a specific convention for them
        return decode_api_version(version);
    }
}
