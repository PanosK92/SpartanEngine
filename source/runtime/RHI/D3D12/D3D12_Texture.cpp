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
#include "../RHI_Texture.h"
#include "../RHI_Device.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

// forward declarations from d3d12_device.cpp
namespace spartan::d3d12_descriptors
{
    ID3D12DescriptorHeap* GetCbvSrvUavHeap();
    uint32_t GetCbvSrvUavDescriptorSize();
    uint32_t AllocateCbvSrvUav();
}

namespace spartan
{
    bool RHI_Texture::RHI_CreateResource()
    {
        SP_LOG_INFO("Creating texture '%s' (%dx%d, format=%d, mips=%d, flags=0x%x)", 
            m_object_name.c_str(), m_width, m_height, static_cast<int>(m_format), m_mip_count, m_flags);

        SP_ASSERT_MSG(RHI_Context::device != nullptr, "D3D12 device is null");

        // validate texture dimensions
        if (m_width == 0 || m_height == 0)
        {
            SP_LOG_ERROR("Invalid texture dimensions: %dx%d", m_width, m_height);
            return false;
        }

        // get dxgi format
        uint32_t format_index = rhi_format_to_index(m_format);
        if (format_index >= _countof(d3d12_format))
        {
            SP_LOG_ERROR("Invalid format index: %d", format_index);
            return false;
        }
        DXGI_FORMAT dxgi_format = d3d12_format[format_index];
        if (dxgi_format == DXGI_FORMAT_UNKNOWN && m_format != RHI_Format::Max)
        {
            SP_LOG_ERROR("Unknown DXGI format for RHI_Format: %d", static_cast<int>(m_format));
            return false;
        }

        // check if this is a depth format
        bool is_depth_format = (m_format == RHI_Format::D16_Unorm || 
                                m_format == RHI_Format::D32_Float || 
                                m_format == RHI_Format::D32_Float_S8X24_Uint);

        // for depth textures that also need srv, use typeless format for the resource
        // this allows creating both dsv and srv views on the same resource
        DXGI_FORMAT resource_format = dxgi_format;
        DXGI_FORMAT dsv_format      = dxgi_format;
        DXGI_FORMAT srv_format      = dxgi_format;
        
        if (is_depth_format && (m_flags & RHI_Texture_Srv))
        {
            switch (m_format)
            {
                case RHI_Format::D16_Unorm:
                    resource_format = DXGI_FORMAT_R16_TYPELESS;
                    dsv_format      = DXGI_FORMAT_D16_UNORM;
                    srv_format      = DXGI_FORMAT_R16_UNORM;
                    break;
                case RHI_Format::D32_Float:
                    resource_format = DXGI_FORMAT_R32_TYPELESS;
                    dsv_format      = DXGI_FORMAT_D32_FLOAT;
                    srv_format      = DXGI_FORMAT_R32_FLOAT;
                    break;
                case RHI_Format::D32_Float_S8X24_Uint:
                    resource_format = DXGI_FORMAT_R32G8X24_TYPELESS;
                    dsv_format      = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                    srv_format      = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                    break;
                default:
                    break;
            }
        }

        // heap properties for default heap
        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type                  = D3D12_HEAP_TYPE_DEFAULT;
        heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask      = 1;
        heap_props.VisibleNodeMask       = 1;

        // resource description
        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Format              = resource_format;
        resource_desc.Width               = m_width;
        resource_desc.Height              = m_height;
        resource_desc.MipLevels           = static_cast<UINT16>(m_mip_count);
        resource_desc.SampleDesc.Count    = 1;
        resource_desc.SampleDesc.Quality  = 0;
        resource_desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

        // get array length
        uint32_t array_length = GetArrayLength();

        // determine dimension
        if (m_type == RHI_Texture_Type::Type2D || m_type == RHI_Texture_Type::Type2DArray)
        {
            resource_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            resource_desc.DepthOrArraySize = static_cast<UINT16>(array_length);
        }
        else if (m_type == RHI_Texture_Type::Type3D)
        {
            resource_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            resource_desc.DepthOrArraySize = static_cast<UINT16>(m_depth);
        }
        else if (m_type == RHI_Texture_Type::TypeCube)
        {
            resource_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            resource_desc.DepthOrArraySize = 6;
        }

        // add flags based on usage
        // note: depth textures use ALLOW_DEPTH_STENCIL instead of ALLOW_RENDER_TARGET
        // these flags are mutually exclusive in d3d12
        if (m_flags & RHI_Texture_Uav)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (is_depth_format)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        else if (m_flags & RHI_Texture_Rtv)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }

        // initial state
        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (!HasData())
        {
            initial_state = D3D12_RESOURCE_STATE_COMMON;
        }

        // for depth textures, we need a clear value
        D3D12_CLEAR_VALUE clear_value = {};
        D3D12_CLEAR_VALUE* clear_value_ptr = nullptr;
        if (is_depth_format && (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        {
            clear_value.Format               = dsv_format; // use dsv format, not typeless
            clear_value.DepthStencil.Depth   = 1.0f;
            clear_value.DepthStencil.Stencil = 0;
            clear_value_ptr                  = &clear_value;
            initial_state                    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        // for render targets, we can optionally provide a clear value
        else if (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        {
            clear_value.Format   = dxgi_format;
            clear_value.Color[0] = 0.0f;
            clear_value.Color[1] = 0.0f;
            clear_value.Color[2] = 0.0f;
            clear_value.Color[3] = 1.0f;
            clear_value_ptr      = &clear_value;
        }

        // create the texture resource
        ID3D12Resource* texture = nullptr;
        HRESULT hr = RHI_Context::device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            initial_state,
            clear_value_ptr,
            IID_PPV_ARGS(&texture)
        );

        if (FAILED(hr))
        {
            SP_LOG_ERROR("Failed to create texture resource '%s' (%dx%d, format=%d, mips=%d): %s", 
                m_object_name.c_str(), m_width, m_height, static_cast<int>(m_format), m_mip_count,
                d3d12_utility::error::dxgi_error_to_string(hr));
            return false;
        }

        m_rhi_resource = texture;

        // set debug name
        if (!m_object_name.empty())
        {
            d3d12_utility::debug::set_name(texture, m_object_name.c_str());
        }

        // upload texture data if provided
        if (HasData())
        {
            uint32_t subresource_count = m_mip_count * array_length;

            // create upload buffer
            uint64_t upload_buffer_size = 0;
            RHI_Context::device->GetCopyableFootprints(&resource_desc, 0, subresource_count, 0, nullptr, nullptr, nullptr, &upload_buffer_size);

            D3D12_HEAP_PROPERTIES upload_heap_props = {};
            upload_heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC upload_buffer_desc = {};
            upload_buffer_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            upload_buffer_desc.Width              = upload_buffer_size;
            upload_buffer_desc.Height             = 1;
            upload_buffer_desc.DepthOrArraySize   = 1;
            upload_buffer_desc.MipLevels          = 1;
            upload_buffer_desc.Format             = DXGI_FORMAT_UNKNOWN;
            upload_buffer_desc.SampleDesc.Count   = 1;
            upload_buffer_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ID3D12Resource* upload_buffer = nullptr;
            hr = RHI_Context::device->CreateCommittedResource(
                &upload_heap_props,
                D3D12_HEAP_FLAG_NONE,
                &upload_buffer_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload_buffer)
            );

            if (SUCCEEDED(hr))
            {
                // map upload buffer and copy data
                void* mapped_data = nullptr;
                D3D12_RANGE read_range = { 0, 0 };
                hr = upload_buffer->Map(0, &read_range, &mapped_data);

                if (SUCCEEDED(hr) && mapped_data)
                {
                    // get layout info for all subresources
                    vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresource_count);
                    vector<UINT> num_rows(subresource_count);
                    vector<UINT64> row_sizes(subresource_count);
                    RHI_Context::device->GetCopyableFootprints(&resource_desc, 0, subresource_count, 0, 
                        layouts.data(), num_rows.data(), row_sizes.data(), nullptr);

                    // copy texture data
                    for (uint32_t slice = 0; slice < array_length; slice++)
                    {
                        for (uint32_t mip = 0; mip < m_mip_count; mip++)
                        {
                            uint32_t subresource = slice * m_mip_count + mip;
                            
                            if (slice < m_slices.size() && mip < m_slices[slice].mips.size())
                            {
                                const auto& mip_data = m_slices[slice].mips[mip].bytes;
                                if (!mip_data.empty())
                                {
                                    uint8_t* dest = static_cast<uint8_t*>(mapped_data) + layouts[subresource].Offset;
                                    const uint8_t* src = reinterpret_cast<const uint8_t*>(mip_data.data());
                                    
                                    for (uint32_t row = 0; row < num_rows[subresource]; row++)
                                    {
                                        memcpy(dest + row * layouts[subresource].Footprint.RowPitch,
                                               src + row * row_sizes[subresource],
                                               static_cast<size_t>(row_sizes[subresource]));
                                    }
                                }
                            }
                        }
                    }

                    upload_buffer->Unmap(0, nullptr);

                    // create temporary command list for upload
                    ID3D12CommandAllocator* temp_allocator = nullptr;
                    ID3D12GraphicsCommandList* temp_cmd_list = nullptr;

                    hr = RHI_Context::device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&temp_allocator));
                    if (SUCCEEDED(hr))
                    {
                        hr = RHI_Context::device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, temp_allocator, nullptr, IID_PPV_ARGS(&temp_cmd_list));
                        if (SUCCEEDED(hr))
                        {
                            // copy from upload buffer to texture
                            for (uint32_t subresource = 0; subresource < subresource_count; subresource++)
                            {
                                D3D12_TEXTURE_COPY_LOCATION dest_location = {};
                                dest_location.pResource        = texture;
                                dest_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                dest_location.SubresourceIndex = subresource;

                                D3D12_TEXTURE_COPY_LOCATION src_location = {};
                                src_location.pResource       = upload_buffer;
                                src_location.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                src_location.PlacedFootprint = layouts[subresource];

                                temp_cmd_list->CopyTextureRegion(&dest_location, 0, 0, 0, &src_location, nullptr);
                            }

                            // transition to shader resource
                            D3D12_RESOURCE_BARRIER barrier = {};
                            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            barrier.Transition.pResource   = texture;
                            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            temp_cmd_list->ResourceBarrier(1, &barrier);

                            temp_cmd_list->Close();

                            // execute
                            ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Graphics));
                            SP_ASSERT_MSG(queue != nullptr, "Graphics queue is null - device not fully initialized");
                            ID3D12CommandList* cmd_lists[] = { temp_cmd_list };
                            queue->ExecuteCommandLists(1, cmd_lists);

                            // wait for completion
                            RHI_Device::QueueWaitAll();

                            temp_cmd_list->Release();
                        }
                        temp_allocator->Release();
                    }
                }

                upload_buffer->Release();
            }
        }

        // create shader resource view if needed
        if (m_flags & RHI_Texture_Srv)
        {
            ID3D12DescriptorHeap* srv_heap = d3d12_descriptors::GetCbvSrvUavHeap();
            SP_ASSERT_MSG(srv_heap != nullptr, "SRV descriptor heap is null - device may not be initialized");
            
            uint32_t descriptor_size = d3d12_descriptors::GetCbvSrvUavDescriptorSize();
            SP_ASSERT_MSG(descriptor_size > 0, "Invalid descriptor size");
            
            uint32_t srv_index = d3d12_descriptors::AllocateCbvSrvUav();

            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format                  = is_depth_format ? srv_format : dxgi_format;
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            if (m_type == RHI_Texture_Type::Type2D)
            {
                srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MostDetailedMip = 0;
                srv_desc.Texture2D.MipLevels       = m_mip_count;
            }
            else if (m_type == RHI_Texture_Type::Type2DArray)
            {
                srv_desc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                srv_desc.Texture2DArray.MostDetailedMip = 0;
                srv_desc.Texture2DArray.MipLevels       = m_mip_count;
                srv_desc.Texture2DArray.FirstArraySlice = 0;
                srv_desc.Texture2DArray.ArraySize       = array_length;
            }
            else if (m_type == RHI_Texture_Type::TypeCube)
            {
                srv_desc.ViewDimension               = D3D12_SRV_DIMENSION_TEXTURECUBE;
                srv_desc.TextureCube.MostDetailedMip = 0;
                srv_desc.TextureCube.MipLevels       = m_mip_count;
            }
            else if (m_type == RHI_Texture_Type::Type3D)
            {
                srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE3D;
                srv_desc.Texture3D.MostDetailedMip = 0;
                srv_desc.Texture3D.MipLevels       = m_mip_count;
            }

            // get cpu handle for srv
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = srv_heap->GetCPUDescriptorHandleForHeapStart();
            cpu_handle.ptr += srv_index * descriptor_size;

            // get gpu handle for srv
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = srv_heap->GetGPUDescriptorHandleForHeapStart();
            gpu_handle.ptr += srv_index * descriptor_size;

            RHI_Context::device->CreateShaderResourceView(texture, &srv_desc, cpu_handle);

            // store the gpu handle as our srv
            m_rhi_srv = reinterpret_cast<void*>(gpu_handle.ptr);
        }

        return true;
    }

    void RHI_Texture::RHI_DestroyResource()
    {
        if (m_rhi_resource)
        {
            static_cast<ID3D12Resource*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        m_rhi_srv = nullptr;
    }
}
