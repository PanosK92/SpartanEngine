/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "RHI_Device.h"
#include "../Core/Context.h"
#include "../Core/Settings.h"
#include "../Core/Timer.h"
#include "../Math/MathHelper.h"
#include <algorithm>
//=============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void RHI_Device::RegisterPhysicalDevice(const PhysicalDevice& physical_device)
    {
        m_physical_devices.emplace_back(physical_device);

        // Keep devices sorted, based on memory (from highest to lowest)
        sort(m_physical_devices.begin(), m_physical_devices.end(), [](const PhysicalDevice& adapter1, const PhysicalDevice& adapter2)
        {
            return adapter1.memory > adapter2.memory;
        });

        LOG_INFO("%s (%d MB)", physical_device.name.c_str(), physical_device.memory);
    }

    const PhysicalDevice* RHI_Device::GetPrimaryPhysicalDevice()
    {
        if (m_physical_device_index >= m_physical_devices.size())
            return nullptr;

        return &m_physical_devices[m_physical_device_index];
    }

    void RHI_Device::SetPrimaryPhysicalDevice(const uint32_t index)
	{
        m_physical_device_index = index;

        if (const PhysicalDevice* physical_device = GetPrimaryPhysicalDevice())
        {
            LOG_INFO("%s (%d MB)", physical_device->name.c_str(), physical_device->memory);
        }
	}

    void RHI_Device::RegisterDisplayMode(const DisplayMode& display_mode)
    {
        DisplayMode& mode = m_display_modes.emplace_back(display_mode);

        // Keep display modes sorted, based on refresh rate (from highest to lowest)
        sort(m_display_modes.begin(), m_display_modes.end(), [](const DisplayMode& display_mode_a, const DisplayMode& display_mode_b)
        {
            return display_mode_a.refresh_rate > display_mode_b.refresh_rate;
        });

        // Let the timer know about the refresh rates this monitor is capable of (will result in low latency/smooth ticking)
        m_context->GetSubsystem<Timer>()->SetTargetFps(m_display_modes.front().refresh_rate);
    }

    const DisplayMode* RHI_Device::GetPrimaryDisplayMode()
    {
        if (m_display_mode_index >= m_display_modes.size())
            return nullptr;

        return &m_display_modes[m_display_mode_index];
    }
}
