/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "RHI_Device.h"
#include "RHI_Implementation.h"
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
            return adapter1.GetMemory() > adapter2.GetMemory();
        });

        LOG_INFO("%s (%d MB)", physical_device.GetName().c_str(), physical_device.GetMemory());
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
            LOG_INFO("%s (%d MB)", physical_device->GetName().c_str(), physical_device->GetMemory());
        }
    }

    bool RHI_Device::IsValidResolution(const uint32_t width, const uint32_t height)
    {
        return width  > 4 && width  <= m_max_texture_2d_dimension &&
               height > 4 && height <= m_max_texture_2d_dimension;
    }

    bool RHI_Device::QueueWaitAll() const
    {
        return QueueWait(RHI_Queue_Type::Graphics) && QueueWait(RHI_Queue_Type::Copy) && QueueWait(RHI_Queue_Type::Compute);
    }

    void* RHI_Device::GetQueue(const RHI_Queue_Type type) const
    {
        if (type == RHI_Queue_Type::Graphics)
        {
            return m_queue_graphics;
        }
        else if (type == RHI_Queue_Type::Copy)
        {
            return m_queue_copy;
        }
        else if (type == RHI_Queue_Type::Compute)
        {
            return m_queue_compute;
        }

        return nullptr;
    }

    uint32_t RHI_Device::GetQueueIndex(const RHI_Queue_Type type) const
    {
        if (type == RHI_Queue_Type::Graphics)
        {
            return m_queue_graphics_index;
        }
        else if (type == RHI_Queue_Type::Copy)
        {
            return m_queue_copy_index;
        }
        else if (type == RHI_Queue_Type::Compute)
        {
            return m_queue_compute_index;
        }

        return 0;
    }

    void RHI_Device::SetQueueIndex(const RHI_Queue_Type type, const uint32_t index)
    {
        if (type == RHI_Queue_Type::Graphics)
        {
            m_queue_graphics_index = index;
        }
        else if (type == RHI_Queue_Type::Copy)
        {
            m_queue_copy_index = index;
        }
        else if (type == RHI_Queue_Type::Compute)
        {
            m_queue_compute_index = index;
        }
    }

}
