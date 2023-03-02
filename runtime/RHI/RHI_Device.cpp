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
#include "RHI_Device.h"
#include "RHI_Implementation.h"
#include "RHI_CommandPool.h"
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

        // Sort devices by type, discrete devices come first.
        sort(m_physical_devices.begin(), m_physical_devices.end(), [](const PhysicalDevice& adapter1, const PhysicalDevice& adapter2)
        {
            return adapter1.GetType() == RHI_PhysicalDevice_Type::Discrete;
        });

        // Sort devices by memory, in an ascending order. The type order will be maintained.
        sort(m_physical_devices.begin(), m_physical_devices.end(), [](const PhysicalDevice& adapter1, const PhysicalDevice& adapter2)
        {
                return adapter1.GetMemory() > adapter2.GetMemory() && adapter1.GetType() == adapter2.GetType();
        });

        SP_LOG_INFO("%s (%d MB)", physical_device.GetName().c_str(), physical_device.GetMemory());
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
            SP_LOG_INFO("%s (%d MB)", physical_device->GetName().c_str(), physical_device->GetMemory());
        }
    }

    RHI_CommandPool* RHI_Device::AllocateCommandPool(const char* name, const uint64_t swap_chain_id)
    {
        return m_cmd_pools.emplace_back(make_shared<RHI_CommandPool>(this, name, swap_chain_id)).get();
    }
    
    void RHI_Device::DestroyCommandPool(RHI_CommandPool* cmd_pool)
    {
        vector<shared_ptr<RHI_CommandPool>>::iterator it;
        for (it = m_cmd_pools.begin(); it != m_cmd_pools.end();)
        {
            if (cmd_pool->GetObjectId() == (*it)->GetObjectId())
            {
                it = m_cmd_pools.erase(it);
                return;
            }
            it++;
        }
    }
    
    bool RHI_Device::IsValidResolution(const uint32_t width, const uint32_t height)
    {
        return width  > 4 && width  <= m_max_texture_2d_dimension &&
               height > 4 && height <= m_max_texture_2d_dimension;
    }

    RHI_Api_Type RHI_Device::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }
    
    void RHI_Device::QueueWaitAll()
    {
        QueueWait(RHI_Queue_Type::Graphics);
        QueueWait(RHI_Queue_Type::Copy);
        QueueWait(RHI_Queue_Type::Compute);
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

    bool RHI_Device::HasDescriptorSetCapacity()
    {
        const uint32_t required_capacity = static_cast<uint32_t>(m_descriptor_sets.size());
        return m_descriptor_set_capacity > required_capacity;
    }

    RHI_CommandList* RHI_Device::ImmediateBegin(const RHI_Queue_Type queue_type)
    {
        m_mutex_immediate.lock();

        // Create command pool for the given queue type, if needed.
        uint32_t queue_index = static_cast<uint32_t>(queue_type);
        if (!m_cmd_pools_immediate[queue_index])
        {
            m_cmd_pools_immediate[queue_index] = make_shared<RHI_CommandPool>(this, "cmd_immediate_execution", 0);
            m_cmd_pools_immediate[queue_index]->AllocateCommandLists(queue_type, 1, 1);
        }

        //  Get command pool
        RHI_CommandPool* cmd_pool = m_cmd_pools_immediate[queue_index].get();

        cmd_pool->Step();
        cmd_pool->GetCurrentCommandList()->Begin();
        return cmd_pool->GetCurrentCommandList();
    }

    void RHI_Device::ImmediateSubmit(RHI_CommandList* cmd_list)
    {
        cmd_list->End();
        cmd_list->Submit();

        // Don't log if it waits, since it's always expected to wait.
        bool log_on_wait = false;
        cmd_list->Wait(log_on_wait);

        m_mutex_immediate.unlock();
    }
}
