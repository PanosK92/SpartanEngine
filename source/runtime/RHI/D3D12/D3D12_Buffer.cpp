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
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    void RHI_Buffer::RHI_DestroyResource()
    {
        // unmap if mapped
        if (m_data_gpu && m_rhi_resource)
        {
            static_cast<ID3D12Resource*>(m_rhi_resource)->Unmap(0, nullptr);
            m_data_gpu = nullptr;
        }

        // release the resource
        if (m_rhi_resource)
        {
            static_cast<ID3D12Resource*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }
    }

    void RHI_Buffer::RHI_CreateResource(const void* data)
    {
        SP_ASSERT(RHI_Context::device != nullptr);

        // determine heap type based on usage
        D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;

        // for buffers that need cpu access (vertex/index buffers with dynamic data), use upload heap
        bool is_mappable = (m_type == RHI_Buffer_Type::Vertex || m_type == RHI_Buffer_Type::Index);
        if (is_mappable)
        {
            heap_type = D3D12_HEAP_TYPE_UPLOAD;
            initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        // create heap properties
        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type                  = heap_type;
        heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask      = 1;
        heap_props.VisibleNodeMask       = 1;

        // create resource desc
        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Alignment          = 0;
        resource_desc.Width              = m_object_size;
        resource_desc.Height             = 1;
        resource_desc.DepthOrArraySize   = 1;
        resource_desc.MipLevels          = 1;
        resource_desc.Format             = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc.Count   = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        // create the buffer resource
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
            SP_LOG_ERROR("Failed to create buffer resource: %s", d3d12_utility::error::dxgi_error_to_string(hr));
            return;
        }

        m_rhi_resource = buffer;

        // set debug name
        if (!m_object_name.empty())
        {
            d3d12_utility::debug::set_name(buffer, m_object_name.c_str());
        }

        // map the buffer for cpu access if it's an upload heap
        if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
        {
            D3D12_RANGE read_range = { 0, 0 }; // we don't read from this buffer
            hr = buffer->Map(0, &read_range, &m_data_gpu);
            if (FAILED(hr))
            {
                SP_LOG_ERROR("Failed to map buffer: %s", d3d12_utility::error::dxgi_error_to_string(hr));
                m_data_gpu = nullptr;
            }
        }

        // copy initial data if provided
        if (data && m_data_gpu)
        {
            memcpy(m_data_gpu, data, m_object_size);
        }
        else if (data && !m_data_gpu)
        {
            // for default heap buffers, we would need to use a staging buffer
            // todo: implement staging buffer upload for default heap resources
            SP_LOG_WARNING("Initial data provided but buffer is not mappable - data not copied");
        }
    }

    void RHI_Buffer::UploadSubRegion(const void* data, uint64_t offset_bytes, uint64_t size_bytes)
    {
        SP_ASSERT(data != nullptr);
        SP_ASSERT(offset_bytes + size_bytes <= m_object_size);

        if (m_data_gpu)
        {
            memcpy(static_cast<uint8_t*>(m_data_gpu) + offset_bytes, data, size_bytes);
        }
        else
        {
            SP_LOG_WARNING("UploadSubRegion: buffer is not mapped, cannot upload");
        }
    }

    void RHI_Buffer::Update(RHI_CommandList* cmd_list, void* data_cpu, const uint32_t size)
    {
        if (!data_cpu || size == 0)
            return;

        // for mapped buffers, just copy directly
        if (m_data_gpu)
        {
            memcpy(m_data_gpu, data_cpu, min(static_cast<uint64_t>(size), m_object_size));
        }
        else
        {
            // todo: implement upload via command list for non-mapped buffers
            SP_LOG_WARNING("Buffer update not implemented for non-mapped buffers");
        }
    }

    void RHI_Buffer::UpdateHandles(RHI_CommandList* cmd_list)
    {
        // nothing special needed for d3d12
    }
}
