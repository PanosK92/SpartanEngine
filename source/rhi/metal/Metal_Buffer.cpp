/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_Buffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../../core/ThreadPool.h"
#include "Metal_Internal.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    void RHI_Buffer::RHI_DestroyResource()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DescriptorSetInvalidateReferencingResource(this);
            // open command lists may still reference it
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_data_gpu       = nullptr;
        m_device_address = 0;
    }

    void RHI_Buffer::RHI_CreateResource(const void* data)
    {
        SP_ASSERT(RHI_Context::device != nullptr);

        // constant buffers are bound at offsets, which metal wants 256 byte aligned like d3d12
        uint64_t size = m_object_size;
        if (m_type == RHI_Buffer_Type::Constant)
        {
            m_stride      = (m_stride_unaligned + 255) & ~255;
            size          = static_cast<uint64_t>(m_stride) * m_element_count;
            m_object_size = size;
        }

        // shader binding tables are laid out [raygen][miss][hit], per record offsets are precomputed for GetRegion
        if (m_type == RHI_Buffer_Type::ShaderBindingTable)
        {
            SP_ASSERT(m_element_count >= 3);
            const uint32_t handle_size  = RHI_Device::PropertyGetShaderGroupHandleSize();
            const uint32_t handle_align = RHI_Device::PropertyGetShaderGroupHandleAlignment();
            const uint64_t base_align   = RHI_Device::PropertyGetShaderGroupBaseAlignment();
            m_aligned_handle_size       = ((handle_size + handle_align - 1) / handle_align) * handle_align;
            m_raygen_offset             = 0;
            m_miss_offset               = ((m_aligned_handle_size + base_align - 1) / base_align) * base_align;
            m_hit_offset                = m_miss_offset + ((m_aligned_handle_size + base_align - 1) / base_align) * base_align;
            size                        = m_hit_offset + m_aligned_handle_size;
            m_object_size               = size;
        }

        // apple gpus share memory with the cpu, so shared storage is the upload heap and the readback heap at once
        // gpu written buffers stay private, which lets the gpu compress them and keeps cpu writes ordered through copies
        const bool shared            = m_mappable || m_type == RHI_Buffer_Type::Upload || m_type == RHI_Buffer_Type::Readback;
        MTL::ResourceOptions options = shared ? MTL::ResourceStorageModeShared : MTL::ResourceStorageModePrivate;
        if (m_type == RHI_Buffer_Type::Upload)
        {
            options |= MTL::ResourceCPUCacheModeWriteCombined;
        }
        options |= MTL::ResourceHazardTrackingModeUntracked;

        MTL::Buffer* buffer = RHI_Context::device->newBuffer(max<uint64_t>(size, 1), options);
        SP_ASSERT_MSG(buffer != nullptr, "Failed to allocate buffer");
        metal_utility::set_label(buffer, m_object_name.c_str());
        metal_residency::Add(metal_residency::Group::Static, buffer);

        m_rhi_resource   = buffer;
        m_device_address = buffer->gpuAddress();
        m_data_gpu       = shared ? buffer->contents() : nullptr;

        if (data)
        {
            if (m_data_gpu)
            {
                memcpy(m_data_gpu, data, m_object_size);
            }
            else
            {
                UploadSubRegion(data, 0, m_object_size);
            }
        }
    }

    void RHI_Buffer::UploadSubRegion(const void* data, uint64_t offset_bytes, uint64_t size_bytes)
    {
        SP_ASSERT(data && offset_bytes + size_bytes <= m_object_size);
        if (!m_rhi_resource || size_bytes == 0)
            return;

        if (m_data_gpu)
        {
            memcpy(static_cast<uint8_t*>(m_data_gpu) + offset_bytes, data, size_bytes);
            return;
        }

        RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics);
        if (!cmd_list)
            return;

        RHI_CommandList::UpdateBuffer(this, offset_bytes, size_bytes, data, false);
        RHI_CommandList::ImmediateExecutionEnd(cmd_list, false);
    }

    void RHI_Buffer::Update(void* data_cpu, const uint32_t size)
    {
        Update(RHI_Device::Cmd(), data_cpu, size);
    }

    void RHI_Buffer::Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size)
    {
        if (!data_cpu)
            return;

        // mappable buffers ring advance by stride, so consumers that read through GetOffset see the right slot
        if (m_data_gpu)
        {
            if (first_update)
            {
                first_update = false;
            }
            else
            {
                m_offset += m_stride;
            }

            const uint64_t upload_size = size != 0 ? static_cast<uint64_t>(size) : static_cast<uint64_t>(m_stride_unaligned);
            SP_ASSERT_MSG(
                static_cast<uint64_t>(m_offset) + upload_size <= m_object_size,
                ("buffer \"" + GetObjectName() + "\" was handed more data than it can hold").c_str()
            );
            ThreadPool::ParallelCopy(static_cast<uint8_t*>(m_data_gpu) + m_offset, data_cpu, upload_size);
            return;
        }

        SP_ASSERT(cmd_list);
        cmd_list->update_buffer(this, 0, size != 0 ? size : m_stride_unaligned, data_cpu);
    }

    void RHI_Buffer::UpdateHandles(RHI_CommandList* cmd_list)
    {
        // shader binding tables are filled once ray tracing pipelines exist
    }

    RHI_StridedDeviceAddressRegion RHI_Buffer::GetRegion(const RHI_Shader_Type group_type, const uint32_t stride_extra) const
    {
        uint64_t offset = 0;
        if (group_type == RHI_Shader_Type::RayMiss)
        {
            offset = m_miss_offset;
        }
        else if (group_type == RHI_Shader_Type::RayHit)
        {
            offset = m_hit_offset;
        }

        RHI_StridedDeviceAddressRegion region = {};
        region.device_address                 = m_device_address + offset;
        region.stride                         = m_aligned_handle_size;
        region.size                           = m_aligned_handle_size;
        return region;
    }
}
