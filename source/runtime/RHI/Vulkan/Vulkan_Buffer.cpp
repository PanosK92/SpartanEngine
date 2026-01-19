/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Buffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_Implementation.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    void RHI_Buffer::RHI_DestroyResource()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_Buffer::RHI_CreateResource(const void* data)
    {
        RHI_DestroyResource();

        // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT       -> mappable
        // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT      -> persistently mapped (flushless)
        // VK_BUFFER_USAGE_TRANSFER_DST_BIT          -> vkCmdUpdateBuffer()
        // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT -> RHI_Device::GetBufferDeviceAddress(void* buffer)

        if (m_type == RHI_Buffer_Type::Vertex || m_type == RHI_Buffer_Type::Index || m_type == RHI_Buffer_Type::Instance)
        {
            bool vertex                     = m_type == RHI_Buffer_Type::Vertex || m_type == RHI_Buffer_Type::Instance;
            VkBufferUsageFlags flags_usage  = vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

             if (m_type == RHI_Buffer_Type::Vertex || m_type == RHI_Buffer_Type::Index)
             {
                if (RHI_Device::IsSupportedRayTracing())
                {
                    flags_usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                |  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
                }
             }

            if (m_mappable)
            {
                uint32_t flags_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, flags_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, flags_memory, data, m_object_name.c_str());
            }
            else
            {
                // create staging buffer, it's slower but we can copy data in and out of it
                void* staging_buffer = nullptr;
                RHI_Device::MemoryBufferCreate(staging_buffer, m_object_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, data, m_object_name.c_str());

                // create destination buffer, it's faster but we can only copy data into it
                RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | flags_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr, m_object_name.c_str());

                // copy staging buffer to destination buffer
                VkBuffer* buffer_vk         = reinterpret_cast<VkBuffer*>(&m_rhi_resource);
                VkBuffer* buffer_staging_vk = reinterpret_cast<VkBuffer*>(&staging_buffer);
                VkBufferCopy copy_region    = {};
                copy_region.size            = m_object_size;
                RHI_CommandList* cmd_list   = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Copy);
                vkCmdCopyBuffer(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), *buffer_staging_vk, *buffer_vk, 1, &copy_region);
                RHI_CommandList::ImmediateExecutionEnd(cmd_list);
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, staging_buffer);
            }
            
            // save device address for ray tracing vertex/index buffer access
            if (RHI_Device::IsSupportedRayTracing() && (m_type == RHI_Buffer_Type::Vertex || m_type == RHI_Buffer_Type::Index))
            {
                m_device_address = RHI_Device::GetBufferDeviceAddress(m_rhi_resource);
            }
        }
        else if (m_type == RHI_Buffer_Type::Storage)
        {
            // calculate required alignment based on minimum device offset alignment
            size_t min_alignment = RHI_Device::PropertyGetMinStorageBufferOffsetAlignment();
            if (min_alignment > 0 && min_alignment != m_stride)
            {
                m_stride      = static_cast<uint32_t>(static_cast<uint64_t>((m_stride + min_alignment - 1) & ~(min_alignment - 1)));
                m_object_size = m_stride * m_element_count;
            }

            // create
            VkMemoryPropertyFlags flags_memory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            if (m_mappable)
            {
                flags_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            }
            VkBufferUsageFlags flags_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, flags_usage, flags_memory, data, m_object_name.c_str());
        }
        else if (m_type == RHI_Buffer_Type::Constant)
        {
            // calculate required alignment based on minimum device offset alignment
            size_t min_alignment = RHI_Device::PropertyGetMinUniformBufferOffsetAlignment();
            if (min_alignment > 0 && min_alignment != m_stride)
            {
                m_stride      = static_cast<uint32_t>(static_cast<uint64_t>((m_stride + min_alignment - 1) & ~(min_alignment - 1)));
                m_object_size = m_stride * m_element_count;
            }

            // create
            VkMemoryPropertyFlags flags_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // mappable and flushless
            RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, flags_memory, data, m_object_name.c_str());
        }
        else if (m_type == RHI_Buffer_Type::ShaderBindingTable)
        {
            SP_ASSERT(m_element_count >= 3); // at minimum: raygen, miss, hit
        
            uint32_t handle_size   = RHI_Device::PropertyGetShaderGroupHandleSize();
            m_aligned_handle_size  = static_cast<uint32_t>(((handle_size + RHI_Device::PropertyGetShaderGroupHandleAlignment() - 1) / RHI_Device::PropertyGetShaderGroupHandleAlignment()) * RHI_Device::PropertyGetShaderGroupHandleAlignment());
            uint64_t base_align    = RHI_Device::PropertyGetShaderGroupBaseAlignment();
        
            // compute worst-case size for all shader groups
            uint64_t max_padding = base_align - 1;
            m_object_size        = m_element_count * m_aligned_handle_size + 2 * max_padding;
        
            VkBufferUsageFlags flags_usage     = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            VkMemoryPropertyFlags flags_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, flags_usage, flags_memory, nullptr, m_object_name.c_str());
        
            m_device_address = RHI_Device::GetBufferDeviceAddress(m_rhi_resource);
        
            // now align offsets based on device_address
            // sbt layout: [raygen] [miss shaders...] [hit shaders...]
            uint64_t current_address  = m_device_address;
            m_raygen_offset           = (base_align - (current_address % base_align)) % base_align;
            current_address          += m_raygen_offset + m_aligned_handle_size;
            uint64_t padding_miss     = (base_align - (current_address % base_align)) % base_align;
            m_miss_offset             = m_raygen_offset + m_aligned_handle_size + padding_miss;
            current_address          += padding_miss + m_aligned_handle_size;
            uint64_t padding_hit      = (base_align - (current_address % base_align)) % base_align;
            m_hit_offset              = m_miss_offset + m_aligned_handle_size + padding_hit;
        
            // actual size (account for extra shader groups if > 3)
            uint32_t extra_groups = (m_element_count > 3) ? (m_element_count - 3) : 0;
            m_object_size = m_hit_offset + m_aligned_handle_size + extra_groups * m_aligned_handle_size;
        }

        SP_ASSERT_MSG(m_rhi_resource != nullptr, "Failed to create buffer");
        m_data_gpu = m_mappable ? RHI_Device::MemoryGetMappedDataFromBuffer(m_rhi_resource) : nullptr;
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Buffer, m_object_name.c_str());
    }

    void RHI_Buffer::Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size)
    {
        SP_ASSERT(cmd_list);
        SP_ASSERT_MSG(m_mappable,                           "Can't update unmapped buffer");
        SP_ASSERT_MSG(data_cpu != nullptr,                  "Invalid cpu data");
        SP_ASSERT_MSG(m_data_gpu != nullptr,                "Invalid gpu data");
        SP_ASSERT_MSG(m_offset + m_stride <= m_object_size, "Out of memory");

        // advance offset
        if (first_update)
        {
            first_update = false;
        }
        else
        {
            m_offset += m_stride;
        }

        // vkCmdUpdateBuffer and vkCmdPipelineBarrier
        cmd_list->UpdateBuffer(this, m_offset, size != 0 ? size : m_stride, data_cpu);
    }

    RHI_StridedDeviceAddressRegion RHI_Buffer::GetRegion(const RHI_Shader_Type group_type, const uint32_t stride_extra /*= 0*/) const
    {
        uint64_t offset = 0;
        if (group_type == RHI_Shader_Type::RayGeneration) offset = m_raygen_offset;
        else if (group_type == RHI_Shader_Type::RayMiss)  offset = m_miss_offset;
        else if (group_type == RHI_Shader_Type::RayHit)   offset = m_hit_offset;

        RHI_StridedDeviceAddressRegion region = {};
        region.device_address                 = m_device_address + offset;
        region.stride                         = m_aligned_handle_size;
        region.size                           = m_aligned_handle_size;

        return region;
    }

    void RHI_Buffer::UpdateHandles(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(m_type == RHI_Buffer_Type::ShaderBindingTable);
    
        static PFN_vkGetRayTracingShaderGroupHandlesKHR pfn_vk_get_ray_tracing_shader_group_handles_khr = nullptr;
        if (!pfn_vk_get_ray_tracing_shader_group_handles_khr)
        {
            pfn_vk_get_ray_tracing_shader_group_handles_khr = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
                vkGetDeviceProcAddr(RHI_Context::device, "vkGetRayTracingShaderGroupHandlesKHR")
            );
            SP_ASSERT(pfn_vk_get_ray_tracing_shader_group_handles_khr != nullptr);
        }
    
        uint32_t handle_size = RHI_Device::PropertyGetShaderGroupHandleSize();
        vector<uint8_t> handles(m_element_count * handle_size);
        SP_ASSERT_VK(pfn_vk_get_ray_tracing_shader_group_handles_khr(
            RHI_Context::device,
            static_cast<VkPipeline>(cmd_list->GetRhiResourcePipeline()),
            0,
            m_element_count,
            handles.size(),
            handles.data()
        ));
    
        SP_ASSERT(m_data_gpu != nullptr);
        uint8_t* dst = static_cast<uint8_t*>(m_data_gpu);
        memset(dst, 0, m_object_size);
        
        // copy shader handles to their respective offsets
        // standard layout: [raygen][miss][hit] + optional extra groups
        memcpy(dst + m_raygen_offset, handles.data() + 0 * handle_size, handle_size);
        memcpy(dst + m_miss_offset, handles.data() + 1 * handle_size, handle_size);
        memcpy(dst + m_hit_offset, handles.data() + 2 * handle_size, handle_size);
        
        // copy any additional shader groups (for passes needing more than 3)
        for (uint32_t i = 3; i < m_element_count; i++)
        {
            uint64_t extra_offset = m_hit_offset + (i - 2) * m_aligned_handle_size;
            memcpy(dst + extra_offset, handles.data() + i * handle_size, handle_size);
        }
    
        // Use buffer memory barrier for more specific synchronization
        // Since memory is HOST_COHERENT, writes are immediately visible, but we need ordering
        VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = static_cast<VkBuffer>(m_rhi_resource);
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(
            static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, nullptr, 1, &barrier, 0, nullptr
        );
    }
}
