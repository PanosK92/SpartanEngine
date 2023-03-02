/*
Copyright(c) 2016-2023 Panos Karabelas

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
    string PhysicalDevice::decode_driver_version(const uint32_t version)
    {
        char buffer[256];

        if (IsNvidia())
        {
            sprintf
            (
                buffer,
                "%d.%d.%d.%d",
                (version >> 22) & 0x3ff,
                (version >> 14) & 0x0ff,
                (version >> 6) & 0x0ff,
                (version) & 0x003f
            );

        }
        else if (IsIntel())
        {
            sprintf
            (
                buffer,
                "%d.%d",
                (version >> 14),
                (version) & 0x3fff
            );
        }
        else // Use Vulkan version conventions if vendor mapping is not available
        {
            sprintf
            (
                buffer,
                "%d.%d.%d",
                (version >> 22),
                (version >> 12) & 0x3ff,
                version & 0xfff
            );
        }

        return buffer;
    }
}
