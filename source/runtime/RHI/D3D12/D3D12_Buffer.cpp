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
            d3d12_state::RemoveState(static_cast<ID3D12Resource*>(m_rhi_resource));
            static_cast<ID3D12Resource*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        m_device_address = 0;
    }

    void RHI_Buffer::DestroyResourceImmediate()
    {
        RHI_DestroyResource();
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

        D3D12_HEAP_TYPE heap_type = m_mappable ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_STATES initial_state = m_mappable ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

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

        // d3d12 forbids combining allow_unordered_access with the upload or readback heaps,
        // so uav is restricted to non-mappable storage buffers which live on the default heap,
        // mappable storage buffers are bound as srvs and written from the cpu via the persistent map
        if (m_type == RHI_Buffer_Type::Storage && !m_mappable)
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

        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create buffer resource '%s' (%llu bytes): %s",
                m_object_name.c_str(), size, d3d12_utility::error::dxgi_error_to_string(hr));
            return;
        }

        m_rhi_resource   = buffer;
        m_device_address = buffer->GetGPUVirtualAddress();

        // seed the global state tracker so subsequent barrier transitions know the resource's starting state
        d3d12_state::SetState(buffer, initial_state);

        if (!m_object_name.empty())
        {
            d3d12_utility::debug::set_name(buffer, m_object_name.c_str());
        }

        if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
        {
            D3D12_RANGE read_range = { 0, 0 };
            hr = buffer->Map(0, &read_range, &m_data_gpu);
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
                            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            list->ResourceBarrier(1, &b);

                            list->CopyBufferRegion(buffer, 0, staging, 0, m_object_size);

                            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
                            list->ResourceBarrier(1, &b);
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
            return;

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
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = state_before;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                cmd_list->ResourceBarrier(1, &barrier);
            }

            cmd_list->CopyBufferRegion(dst, offset_bytes, staging, 0, size_bytes);

            if (state_before != D3D12_RESOURCE_STATE_COPY_DEST)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource   = dst;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter  = state_before;
                cmd_list->ResourceBarrier(1, &barrier);
            }

            RHI_CommandList::ImmediateExecutionEnd(cmd_list_rhi);
        }

        RHI_Device::StagingBufferRelease(staging);
    }

    void RHI_Buffer::Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size)
    {
        if (!data_cpu || size == 0)
            return;

        SP_ASSERT_MSG(m_mappable, "Can't update unmapped buffer");

        // constant buffers are ring-allocated by advancing m_offset by the aligned stride per update
        if (m_type == RHI_Buffer_Type::Constant)
        {
            if (!first_update)
            {
                m_offset += m_stride;
                if (m_offset + m_stride > m_object_size)
                {
                    m_offset = 0;
                }
            }
            first_update = false;

            if (m_data_gpu)
            {
                memcpy(static_cast<uint8_t*>(m_data_gpu) + m_offset, data_cpu, min(static_cast<uint64_t>(size), static_cast<uint64_t>(m_stride)));
            }
            return;
        }

        if (m_data_gpu)
        {
            memcpy(m_data_gpu, data_cpu, min(static_cast<uint64_t>(size), m_object_size));
        }
    }

    void RHI_Buffer::UpdateHandles(RHI_CommandList* cmd_list)
    {
        // no-op in d3d12; addresses are stable for persistently-mapped upload buffers
    }

    RHI_StridedDeviceAddressRegion RHI_Buffer::GetRegion(const RHI_Shader_Type group_type, const uint32_t stride_extra) const
    {
        RHI_StridedDeviceAddressRegion region = {};
        return region;
    }
}
