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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Buffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_Queue.h"
#include "D3D12_Internal.h"
#include <wrl/client.h>
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    void RHI_Buffer::RHI_DestroyResource()
    {
        if (m_data_gpu && m_rhi_resource)
        {
            static_cast<ID3D12Resource*>(m_rhi_resource)->Unmap(0, nullptr);
            m_data_gpu = nullptr;
        }

        if (m_rhi_resource)
        {
            // evict cached descriptor sets keyed by this buffer pointer
            RHI_Device::DescriptorSetInvalidateReferencingResource(this);

            // defer release, open command lists may still reference this resource
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_device_address = 0;
        m_rhi_srv        = nullptr;
        m_rhi_uav        = nullptr;
    }

    void RHI_Buffer::DestroyResourceImmediate()
    {
        if (m_data_gpu && m_rhi_resource)
        {
            static_cast<ID3D12Resource*>(m_rhi_resource)->Unmap(0, nullptr);
            m_data_gpu = nullptr;
        }

        if (m_rhi_resource)
        {
            RHI_Device::DescriptorSetInvalidateReferencingResource(this);
            GpuMemory::Unregister(m_rhi_resource);
            d3d12_state::RemoveState(static_cast<ID3D12Resource*>(m_rhi_resource));
            static_cast<ID3D12Resource*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        m_device_address = 0;
        m_rhi_srv        = nullptr;
        m_rhi_uav        = nullptr;
    }

    void RHI_Buffer::RHI_CreateResource(const void* data)
    {
        SP_ASSERT(RHI_Context::device != nullptr);

        // for constant buffers d3d12 requires 256-byte alignment for cbv size
        uint64_t size = m_object_size;
        if (m_type == RHI_Buffer_Type::Constant)
        {
            m_stride = (m_stride_unaligned + 255) & ~255;
            size     = static_cast<uint64_t>(m_stride) * m_element_count;
            m_object_size = size;
        }

        // shader binding tables are upload buffers laid out [raygen][miss][hit], per-record offsets are precomputed for GetRegion
        if (m_type == RHI_Buffer_Type::ShaderBindingTable)
        {
            SP_ASSERT(m_element_count >= 3);

            uint32_t handle_size  = RHI_Device::PropertyGetShaderGroupHandleSize();
            uint32_t handle_align = RHI_Device::PropertyGetShaderGroupHandleAlignment();
            uint64_t base_align   = RHI_Device::PropertyGetShaderGroupBaseAlignment();

            m_aligned_handle_size = static_cast<uint32_t>(((handle_size + handle_align - 1) / handle_align) * handle_align);

            // worst case size, base alignment padding can be up to (base_align - 1) per group
            uint64_t max_padding = base_align - 1;
            size                 = m_element_count * m_aligned_handle_size + 2 * max_padding;
            m_object_size        = size;
        }

        // d3d12 forbids uav on upload heaps, so storage buffers live on the default heap and cpu updates go through staging
        const bool force_default_heap_uav = (m_type == RHI_Buffer_Type::Storage);

        D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
        if (m_type == RHI_Buffer_Type::Readback)
        {
            heap_type     = D3D12_HEAP_TYPE_READBACK;
            initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        else if (m_type == RHI_Buffer_Type::Upload || (m_mappable && !force_default_heap_uav))
        {
            heap_type     = D3D12_HEAP_TYPE_UPLOAD;
            initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        // a gpu upload heap lives in vram but stays cpu writable over resizable bar, so the shader reads it at
        // vram bandwidth instead of over pcie, which is the win for buffers the cpu rewrites and the gpu reads directly
        // staging buffers are excluded, the copy engine reads them once so vram would only burn the limited bar budget
        const bool is_staging = m_type == RHI_Buffer_Type::Upload;
        const bool use_gpu_upload =
            heap_type == D3D12_HEAP_TYPE_UPLOAD &&
            !is_staging                         &&
            d3d12_caps::IsGpuUploadHeapSupported();

        if (use_gpu_upload)
        {
            heap_type = D3D12_HEAP_TYPE_GPU_UPLOAD;
        }

        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type                  = heap_type;
        heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask      = 1;
        heap_props.VisibleNodeMask       = 1;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Alignment          = 0;
        resource_desc.Width              = size;
        resource_desc.Height             = 1;
        resource_desc.DepthOrArraySize   = 1;
        resource_desc.MipLevels          = 1;
        resource_desc.Format             = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc.Count   = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        if (force_default_heap_uav)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        ID3D12Resource* buffer = nullptr;
        HRESULT hr = RHI_Context::device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            initial_state,
            nullptr,
            IID_PPV_ARGS(&buffer)
        );

        // the gpu upload heap draws from the resizable bar window, which is far smaller than vram,
        // so retry on the regular upload heap rather than losing the buffer once that window is full
        if (FAILED(hr) && use_gpu_upload)
        {
            heap_type       = D3D12_HEAP_TYPE_UPLOAD;
            heap_props.Type = heap_type;

            hr = RHI_Context::device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                initial_state,
                nullptr,
                IID_PPV_ARGS(&buffer)
            );
        }

        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create buffer resource '%s' (%llu bytes): %s",
                m_object_name.c_str(), size, d3d12_utility::error::dxgi_error_to_string(hr));
            return;
        }

        m_rhi_resource   = buffer;
        m_device_address = buffer->GetGPUVirtualAddress();

        d3d12_gpu_memory::register_resource(
            buffer,
            GpuMemory::FromBufferType(static_cast<uint32_t>(m_type)),
            m_object_name.c_str()
        );

        // sbt offsets must respect the base alignment, compute them from the actual gpu virtual address
        if (m_type == RHI_Buffer_Type::ShaderBindingTable)
        {
            uint64_t base_align       = RHI_Device::PropertyGetShaderGroupBaseAlignment();
            uint64_t current_address  = m_device_address;
            m_raygen_offset           = (base_align - (current_address % base_align)) % base_align;
            current_address          += m_raygen_offset + m_aligned_handle_size;
            uint64_t padding_miss     = (base_align - (current_address % base_align)) % base_align;
            m_miss_offset             = m_raygen_offset + m_aligned_handle_size + padding_miss;
            current_address          += padding_miss + m_aligned_handle_size;
            uint64_t padding_hit      = (base_align - (current_address % base_align)) % base_align;
            m_hit_offset              = m_miss_offset + m_aligned_handle_size + padding_hit;
        }

        // seed the global state tracker so subsequent barrier transitions know the resource's starting state
        d3d12_state::SetState(buffer, initial_state);
        d3d12_state::SetDecaysToCommon(buffer, heap_type == D3D12_HEAP_TYPE_DEFAULT);
        d3d12_state::SetIsBuffer(buffer, true);
        d3d12_state::SetSubresourceCount(buffer, 1);

        if (m_type == RHI_Buffer_Type::Storage)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Shader4ComponentMapping         =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format                          =
                DXGI_FORMAT_UNKNOWN;
            srv_desc.ViewDimension                   =
                D3D12_SRV_DIMENSION_BUFFER;
            srv_desc.Buffer.NumElements              =
                m_element_count;
            srv_desc.Buffer.StructureByteStride      =
                m_stride;

            if (m_rhi_srv_index == UINT32_MAX)
            {
                m_rhi_srv_index =
                    d3d12_descriptors::
                        AllocateCbvSrvUavCpu();
            }
            D3D12_CPU_DESCRIPTOR_HANDLE srv_handle =
                d3d12_descriptors::GetCbvSrvUavCpuHandle(
                    m_rhi_srv_index
                );
            RHI_Context::device->CreateShaderResourceView(
                buffer,
                &srv_desc,
                srv_handle
            );
            m_rhi_srv =
                reinterpret_cast<void*>(srv_handle.ptr);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.Format                     =
                DXGI_FORMAT_UNKNOWN;
            uav_desc.ViewDimension              =
                D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.NumElements         =
                m_element_count;
            uav_desc.Buffer.StructureByteStride =
                m_stride;

            if (m_rhi_uav_index == UINT32_MAX)
            {
                m_rhi_uav_index =
                    d3d12_descriptors::
                        AllocateCbvSrvUavCpu();
            }
            D3D12_CPU_DESCRIPTOR_HANDLE uav_handle =
                d3d12_descriptors::GetCbvSrvUavCpuHandle(
                    m_rhi_uav_index
                );
            RHI_Context::device->CreateUnorderedAccessView(
                buffer,
                nullptr,
                &uav_desc,
                uav_handle
            );
            m_rhi_uav =
                reinterpret_cast<void*>(uav_handle.ptr);
        }

        if (!m_object_name.empty())
        {
            d3d12_utility::debug::set_name(buffer, m_object_name.c_str());
        }

        if (heap_type == D3D12_HEAP_TYPE_UPLOAD || heap_type == D3D12_HEAP_TYPE_GPU_UPLOAD || heap_type == D3D12_HEAP_TYPE_READBACK)
        {
            hr = buffer->Map(0, nullptr, &m_data_gpu);
            if (FAILED(hr))
            {
                SP_LOG_ERROR("Failed to map buffer '%s': %s", m_object_name.c_str(), d3d12_utility::error::dxgi_error_to_string(hr));
                m_data_gpu = nullptr;
            }
        }

        if (data)
        {
            if (m_data_gpu)
            {
                memcpy(m_data_gpu, data, m_object_size);
            }
            else
            {
                // default heap - upload via staging buffer using a one-shot command list
                void* staging_ptr = RHI_Device::StagingBufferAcquire(m_object_size);
                ID3D12Resource* staging = static_cast<ID3D12Resource*>(staging_ptr);
                if (staging)
                {
                    void* mapped = nullptr;
                    D3D12_RANGE rr = { 0, 0 };
                    if (SUCCEEDED(staging->Map(0, &rr, &mapped)) && mapped)
                    {
                        memcpy(mapped, data, m_object_size);
                        staging->Unmap(0, nullptr);
                    }

                    ID3D12CommandAllocator* alloc = nullptr;
                    ID3D12GraphicsCommandList* list = nullptr;
                    if (SUCCEEDED(RHI_Context::device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
                    {
                        if (SUCCEEDED(RHI_Context::device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list))))
                        {
                            // common -> copy_dest
                            D3D12_RESOURCE_BARRIER b = {};
                            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            b.Transition.pResource   = buffer;
                            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                            b.Transition.Subresource = 0;
                            d3d12_barriers::Submit(list, &b, 1);

                            list->CopyBufferRegion(buffer, 0, staging, 0, m_object_size);

                            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
                            d3d12_barriers::Submit(list, &b, 1);
                            list->Close();

                            ID3D12CommandQueue* q = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Graphics));
                            ID3D12CommandList* lists[] = { list };
                            q->ExecuteCommandLists(1, lists);
                            RHI_Device::QueueWaitAll();

                            list->Release();
                        }
                        alloc->Release();
                    }

                    RHI_Device::StagingBufferRelease(staging);
                }
            }
        }
    }

    void RHI_Buffer::UploadSubRegion(const void* data, uint64_t offset_bytes, uint64_t size_bytes)
    {
        SP_ASSERT(data != nullptr);
        SP_ASSERT(offset_bytes + size_bytes <= m_object_size);

        // skip if backing allocation failed (out of device memory)
        if (!m_rhi_resource)
        {
            return;
        }

        if (m_data_gpu)
        {
            memcpy(static_cast<uint8_t*>(m_data_gpu) + offset_bytes, data, size_bytes);
            return;
        }

        // device-local buffer, stage on an upload heap and copy via an immediate command list
        ID3D12Resource* dst     = static_cast<ID3D12Resource*>(m_rhi_resource);
        ID3D12Resource* staging = static_cast<ID3D12Resource*>(RHI_Device::StagingBufferAcquire(size_bytes));
        if (!staging)
        {
            SP_LOG_ERROR("UploadSubRegion: failed to acquire staging buffer for '%s'", m_object_name.c_str());
            return;
        }

        void* mapped = nullptr;
        D3D12_RANGE read_range = { 0, 0 };
        if (FAILED(staging->Map(0, &read_range, &mapped)) || !mapped)
        {
            SP_LOG_ERROR("UploadSubRegion: failed to map staging buffer for '%s'", m_object_name.c_str());
            RHI_Device::StagingBufferRelease(staging);
            return;
        }
        memcpy(mapped, data, size_bytes);
        staging->Unmap(0, nullptr);

        RHI_CommandList* cmd_list_rhi = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics);
        if (cmd_list_rhi)
        {
            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list_rhi->GetRhiResource());

            // transition to copy_dest, copy, then back to whatever state the tracker had,
            // so subsequent draws/dispatches see the buffer in a sane state
            const D3D12_RESOURCE_STATES state_before = d3d12_state::GetState(dst);
            if (state_before != D3D12_RESOURCE_STATE_COPY_DEST)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource   = dst;
                barrier.Transition.Subresource = 0;
                barrier.Transition.StateBefore = state_before;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                d3d12_barriers::Submit(cmd_list, &barrier, 1);
            }

            cmd_list->CopyBufferRegion(dst, offset_bytes, staging, 0, size_bytes);

            if (state_before != D3D12_RESOURCE_STATE_COPY_DEST)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource   = dst;
                barrier.Transition.Subresource = 0;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter  = state_before;
                d3d12_barriers::Submit(cmd_list, &barrier, 1);
            }

            RHI_CommandList::ImmediateExecutionEnd(cmd_list_rhi);
        }

        RHI_Device::StagingBufferRelease(staging);
    }

    void RHI_Buffer::Update(void* data_cpu, const uint32_t size)
    {
        Update(RHI_Device::Cmd(), data_cpu, size);
    }

    void RHI_Buffer::Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size)
    {
        if (!data_cpu || size == 0)
        {
            return;
        }

        // mappable buffers ring-advance by stride and memcpy into the persistent mapping, mirrors the vulkan path
        // so consumers that read via GetOffset (e.g. dynamic constant buffers) see the correct slot
        if (m_data_gpu)
        {
            if (first_update)
            {
                first_update = false;
            }
            else
            {
                m_offset += m_stride;
                if (m_offset + m_stride > m_object_size)
                {
                    m_offset = 0;
                }
            }

            const uint64_t upload_size = (size != 0) ? static_cast<uint64_t>(size) : static_cast<uint64_t>(m_stride);
            SP_ASSERT_MSG(
                static_cast<uint64_t>(m_offset) + upload_size <= m_object_size,
                ("buffer \"" + GetObjectName() + "\" was handed more data than it can hold").c_str()
            );
            memcpy(static_cast<uint8_t*>(m_data_gpu) + m_offset, data_cpu, upload_size);

            // upload-heap writes become visible to the gpu at the next ExecuteCommandLists boundary, no barrier needed
            return;
        }

        // unmapped path, storage buffers live on the default heap so route through cmd-list staging
        SP_ASSERT(cmd_list);
        cmd_list->update_buffer(this, 0, size, data_cpu);
    }

    void RHI_Buffer::UpdateHandles(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(m_type == RHI_Buffer_Type::ShaderBindingTable);
        SP_ASSERT(m_data_gpu);

        if (!cmd_list)
        {
            return;
        }

        // dxr exposes shader identifiers via ID3D12StateObjectProperties, query them from the bound state object
        ID3D12StateObject* state_object = static_cast<ID3D12StateObject*>(cmd_list->GetRhiResourcePipeline());
        if (!state_object)
        {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> properties;
        if (FAILED(state_object->QueryInterface(IID_PPV_ARGS(&properties))))
        {
            return;
        }

        uint8_t* dst = static_cast<uint8_t*>(m_data_gpu);
        memset(dst, 0, m_object_size);

        const uint32_t handle_size = RHI_Device::PropertyGetShaderGroupHandleSize();

        void* raygen_id = properties->GetShaderIdentifier(L"RayGen");
        void* miss_id   = properties->GetShaderIdentifier(L"Miss");
        void* hit_id    = properties->GetShaderIdentifier(L"HitGroup");

        if (raygen_id)
        {
            memcpy(dst + m_raygen_offset, raygen_id, handle_size);
        }
        if (miss_id)
        {
            memcpy(dst + m_miss_offset,   miss_id,   handle_size);
        }
        if (hit_id)
        {
            memcpy(dst + m_hit_offset,    hit_id,    handle_size);
        }
    }

    RHI_StridedDeviceAddressRegion RHI_Buffer::GetRegion(const RHI_Shader_Type group_type, const uint32_t stride_extra) const
    {
        uint64_t offset = 0;
        if (group_type == RHI_Shader_Type::RayGeneration)
        {
            offset = m_raygen_offset;
        }
        else if (group_type == RHI_Shader_Type::RayMiss)
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
