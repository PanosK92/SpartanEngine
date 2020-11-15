/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ======================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorCache.h"
#include "../RHI_Shader.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorCache::~RHI_DescriptorCache()
    {
        if (m_descriptor_pool)
        {
            // Wait in case the buffer is still in use
            m_rhi_device->Queue_WaitAll();

            vkDestroyDescriptorPool(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
            m_descriptor_pool = nullptr;
        }
    }

    void RHI_DescriptorCache::Reset(uint32_t descriptor_set_capacity /*= 0*/)
    {
        // If the requested capacity is zero, then only recreate the descriptor pool
        if (descriptor_set_capacity == 0)
        {
            descriptor_set_capacity = m_descriptor_set_capacity;
        }

        // Wait in case the pool is being used
        m_rhi_device->Queue_WaitAll();

        // Destroy layouts (and descriptor sets)
        m_descriptor_set_layouts.clear();
        m_descriptor_layout_current = nullptr;

        // Destroy pool
        if (m_descriptor_pool)
        {
            vkDestroyDescriptorPool(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
            m_descriptor_pool = nullptr;
        }

        // Create pool
        CreateDescriptorPool(descriptor_set_capacity);

        // Log
        if (descriptor_set_capacity > m_descriptor_set_capacity)
        {
            LOG_INFO("Capacity has been increased to %d elements", descriptor_set_capacity);
        }
        else if (descriptor_set_capacity < m_descriptor_set_capacity)
        {
            LOG_INFO("Capacity has been decreased to %d elements", descriptor_set_capacity);
        }
        else
        {
            LOG_INFO("Descriptor pool has been reset");
        }
    }

    void RHI_DescriptorCache::SetDescriptorSetCapacity(uint32_t descriptor_set_capacity)
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi())
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        if (m_descriptor_set_capacity == descriptor_set_capacity)
        {
            LOG_INFO("Capacity is already %d elements", m_descriptor_set_capacity);
            return;
        }

        // Re-create descriptor pool
        Reset(descriptor_set_capacity);

        // Update capacity
        m_descriptor_set_capacity = descriptor_set_capacity;
    }

    bool RHI_DescriptorCache::CreateDescriptorPool(uint32_t descriptor_set_capacity)
    {
        // Pool sizes
        std::array<VkDescriptorPoolSize, 5> pool_sizes =
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,                   rhi_descriptor_max_samplers },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,             rhi_descriptor_max_textures },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             rhi_descriptor_max_storage_textures },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            rhi_descriptor_max_constant_buffers },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,    rhi_descriptor_max_constant_buffers_dynamic }
        };

        // Create info
        VkDescriptorPoolCreateInfo pool_create_info = {};
        pool_create_info.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.flags          = 0;
        pool_create_info.poolSizeCount  = static_cast<uint32_t>(pool_sizes.size());
        pool_create_info.pPoolSizes     = pool_sizes.data();
        pool_create_info.maxSets        = descriptor_set_capacity;

        // Pool
        const auto descriptor_pool = reinterpret_cast<VkDescriptorPool*>(&m_descriptor_pool);
        return vulkan_utility::error::check(vkCreateDescriptorPool(m_rhi_device->GetContextRhi()->device, &pool_create_info, nullptr, descriptor_pool));
    }
}
