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
#include "D3D12_Internal.h"
#include <mutex>
//================================

using namespace std;
using namespace spartan::math;

namespace spartan
{
    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT_MSG(RHI_Context::device != nullptr, "D3D12 device is null");

        if (m_width == 0 || m_height == 0)
        {
            SP_LOG_ERROR("Invalid texture dimensions: %dx%d", m_width, m_height);
            return false;
        }

        uint32_t format_index = rhi_format_to_index(m_format);
        if (format_index >= _countof(d3d12_format))
        {
            SP_LOG_ERROR("Invalid format index: %d", format_index);
            return false;
        }
        DXGI_FORMAT dxgi_format = d3d12_format[format_index];
        if (dxgi_format == DXGI_FORMAT_UNKNOWN && m_format != RHI_Format::Max)
        {
            if (m_format == RHI_Format::ASTC)
            {
                SP_LOG_ERROR("ASTC is not supported on D3D12, use BC1/BC3/BC5/BC7");
            }
            else
            {
                SP_LOG_ERROR("Unknown DXGI format for RHI_Format: %d", static_cast<int>(m_format));
            }
            return false;
        }

        bool is_depth_format = (m_format == RHI_Format::D16_Unorm ||
                                m_format == RHI_Format::D32_Float ||
                                m_format == RHI_Format::D32_Float_S8X24_Uint);

        DXGI_FORMAT resource_format = dxgi_format;
        DXGI_FORMAT dsv_format      = dxgi_format;
        DXGI_FORMAT srv_format      = dxgi_format;
        DXGI_FORMAT rtv_format      = dxgi_format;

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

        D3D12_HEAP_PROPERTIES heap_props = {};
        heap_props.Type                  = D3D12_HEAP_TYPE_DEFAULT;
        heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask      = 1;
        heap_props.VisibleNodeMask       = 1;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Format              = resource_format;
        resource_desc.Width               = m_width;
        resource_desc.Height              = m_height;
        resource_desc.MipLevels           = static_cast<UINT16>(m_mip_count);
        resource_desc.SampleDesc.Count    = 1;
        resource_desc.SampleDesc.Quality  = 0;
        resource_desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

        uint32_t array_length = GetArrayLength();

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

        // mirrors vulkan concurrent sharing, required when graphics and compute queues touch the same texture
        // incompatible with depth stencil resources per the d3d12 spec
        if ((m_flags & RHI_Texture_ConcurrentSharing) && !is_depth_format)
        {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        }

        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (!HasData())
        {
            initial_state = D3D12_RESOURCE_STATE_COMMON;
        }

        D3D12_CLEAR_VALUE clear_value = {};
        D3D12_CLEAR_VALUE* clear_value_ptr = nullptr;
        if (is_depth_format && (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        {
            clear_value.Format               = dsv_format;
            clear_value.DepthStencil.Depth   = 1.0f;
            clear_value.DepthStencil.Stencil = 0;
            clear_value_ptr                  = &clear_value;
            initial_state                    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        else if (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        {
            clear_value.Format   = dxgi_format;
            clear_value.Color[0] = 0.0f;
            clear_value.Color[1] = 0.0f;
            clear_value.Color[2] = 0.0f;
            clear_value.Color[3] = 1.0f;
            clear_value_ptr      = &clear_value;
        }

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

        if (!m_object_name.empty())
        {
            d3d12_utility::debug::set_name(texture, m_object_name.c_str());
        }

        // seed the d3d12 state tracker so future barriers know the StateBefore
        d3d12_state::SetState(texture, initial_state);
        // buffers and simultaneous-access textures decay to common after every executecommandlists
        d3d12_state::SetDecaysToCommon(texture, (m_flags & RHI_Texture_ConcurrentSharing) && !is_depth_format);
        {
            const uint32_t slices = (m_type == RHI_Texture_Type::Type3D) ? 1u : static_cast<uint32_t>(resource_desc.DepthOrArraySize);
            d3d12_state::SetSubresourceCount(texture, m_mip_count * slices);
        }

        // set initial layout matching the initial state
        {
            RHI_Image_Layout layout = RHI_Image_Layout::General;
            if (initial_state == D3D12_RESOURCE_STATE_DEPTH_WRITE)
            {
                layout = RHI_Image_Layout::Attachment;
            }
            else if (initial_state == D3D12_RESOURCE_STATE_COPY_DEST)
            {
                layout = RHI_Image_Layout::Transfer_Destination;
            }
            SetLayoutDirect(0, m_mip_count, layout);
        }

        // upload initial data via a staging buffer
        if (HasData())
        {
            static mutex upload_mutex;
            lock_guard<mutex> upload_lock(upload_mutex);

            uint32_t subresource_count = m_mip_count * array_length;

            uint64_t upload_buffer_size = 0;
            RHI_Context::device->GetCopyableFootprints(&resource_desc, 0, subresource_count, 0, nullptr, nullptr, nullptr, &upload_buffer_size);

            ID3D12Resource* upload_buffer = static_cast<ID3D12Resource*>(RHI_Device::StagingBufferAcquire(upload_buffer_size));
            if (upload_buffer)
            {
                void* mapped_data = nullptr;
                D3D12_RANGE read_range = { 0, 0 };
                hr = upload_buffer->Map(0, &read_range, &mapped_data);

                if (SUCCEEDED(hr) && mapped_data)
                {
                    vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresource_count);
                    vector<UINT> num_rows(subresource_count);
                    vector<UINT64> row_sizes(subresource_count);
                    RHI_Context::device->GetCopyableFootprints(&resource_desc, 0, subresource_count, 0,
                        layouts.data(), num_rows.data(), row_sizes.data(), nullptr);

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

                    ID3D12CommandAllocator* temp_allocator = nullptr;
                    ID3D12GraphicsCommandList* temp_cmd_list = nullptr;

                    if (SUCCEEDED(RHI_Context::device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&temp_allocator))))
                    {
                        if (SUCCEEDED(RHI_Context::device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, temp_allocator, nullptr, IID_PPV_ARGS(&temp_cmd_list))))
                        {
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

                            // pixel|non_pixel so bindless material sampling works in pixel shaders without a per-texture settexture upgrade
                            D3D12_RESOURCE_STATES post_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                            RHI_Image_Layout post_layout     = RHI_Image_Layout::Shader_Read;
                            if (m_flags & RHI_Texture_Uav)
                            {
                                post_state  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                                post_layout = RHI_Image_Layout::General;
                            }

                            D3D12_RESOURCE_BARRIER barrier = {};
                            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            barrier.Transition.pResource   = texture;
                            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                            barrier.Transition.StateAfter  = post_state;
                            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            temp_cmd_list->ResourceBarrier(1, &barrier);

                            temp_cmd_list->Close();

                            ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Graphics));
                            SP_ASSERT_MSG(queue != nullptr, "Graphics queue is null - device not fully initialized");
                            ID3D12CommandList* cmd_lists[] = { temp_cmd_list };
                            queue->ExecuteCommandLists(1, cmd_lists);

                            RHI_Device::QueueWaitAll();

                            // upload finished, reflect the post-copy state in both the rhi layout map and the d3d12 state tracker
                            SetLayoutDirect(0, m_mip_count, post_layout);
                            // simultaneous-access textures decay to common after executecommandlists
                            const D3D12_RESOURCE_STATES tracked = (m_flags & RHI_Texture_ConcurrentSharing)
                                ? D3D12_RESOURCE_STATE_COMMON
                                : post_state;
                            d3d12_state::SetState(texture, tracked);

                            temp_cmd_list->Release();
                        }
                        temp_allocator->Release();
                    }
                }

                RHI_Device::StagingBufferRelease(upload_buffer);
            }
        }

        // create srv (in cpu-only staging heap; gets copied into the bindless zone by UpdateBindlessMaterials)
        if (m_flags & RHI_Texture_Srv)
        {
            uint32_t srv_index = d3d12_descriptors::AllocateCbvSrvUavCpu();
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = d3d12_descriptors::GetCbvSrvUavCpuHandle(srv_index);

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

            RHI_Context::device->CreateShaderResourceView(texture, &srv_desc, cpu_handle);

            // store the cpu handle ptr - command list and bindless updaters know how to use it
            m_rhi_srv = reinterpret_cast<void*>(cpu_handle.ptr);

            // per-layer 2d srvs for array textures, lets compute passes sample individual layers
            if (m_type == RHI_Texture_Type::Type2DArray && array_length > 1)
            {
                const uint32_t layer_count = min<uint32_t>(array_length, rhi_max_render_target_count);
                for (uint32_t layer = 0; layer < layer_count; layer++)
                {
                    uint32_t layer_srv_index               = d3d12_descriptors::AllocateCbvSrvUavCpu();
                    D3D12_CPU_DESCRIPTOR_HANDLE layer_handle = d3d12_descriptors::GetCbvSrvUavCpuHandle(layer_srv_index);

                    D3D12_SHADER_RESOURCE_VIEW_DESC layer_desc = {};
                    layer_desc.Format                          = is_depth_format ? srv_format : dxgi_format;
                    layer_desc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    layer_desc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    layer_desc.Texture2DArray.MostDetailedMip  = 0;
                    layer_desc.Texture2DArray.MipLevels        = m_mip_count;
                    layer_desc.Texture2DArray.FirstArraySlice  = layer;
                    layer_desc.Texture2DArray.ArraySize        = 1;

                    RHI_Context::device->CreateShaderResourceView(texture, &layer_desc, layer_handle);
                    m_rhi_srv_layers[layer] = reinterpret_cast<void*>(layer_handle.ptr);
                }
            }
        }

        // create rtvs, one per array slice plus a multiview view covering all slices, mirrors the vulkan view layout
        if ((m_flags & RHI_Texture_Rtv) && !is_depth_format)
        {
            if (m_type == RHI_Texture_Type::Type2D)
            {
                uint32_t rtv_index                     = d3d12_descriptors::AllocateRtv();
                D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle  = d3d12_descriptors::GetRtvHandle(rtv_index);

                D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                rtv_desc.Format                        = rtv_format;
                rtv_desc.ViewDimension                 = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtv_desc.Texture2D.MipSlice            = 0;

                RHI_Context::device->CreateRenderTargetView(texture, &rtv_desc, rtv_handle);
                m_rhi_rtv[0] = reinterpret_cast<void*>(rtv_handle.ptr);
            }
            else if (m_type == RHI_Texture_Type::Type2DArray || m_type == RHI_Texture_Type::TypeCube)
            {
                const uint32_t slice_count = min<uint32_t>(array_length, rhi_max_render_target_count);
                for (uint32_t slice = 0; slice < slice_count; slice++)
                {
                    uint32_t rtv_index                     = d3d12_descriptors::AllocateRtv();
                    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle  = d3d12_descriptors::GetRtvHandle(rtv_index);

                    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc  = {};
                    rtv_desc.Format                         = rtv_format;
                    rtv_desc.ViewDimension                  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtv_desc.Texture2DArray.MipSlice        = 0;
                    rtv_desc.Texture2DArray.FirstArraySlice = slice;
                    rtv_desc.Texture2DArray.ArraySize       = 1;

                    RHI_Context::device->CreateRenderTargetView(texture, &rtv_desc, rtv_handle);
                    m_rhi_rtv[slice] = reinterpret_cast<void*>(rtv_handle.ptr);
                }

                // multiview rtv covering all slices, used when render_target_array_index is unset
                if (array_length > 1)
                {
                    uint32_t rtv_index                     = d3d12_descriptors::AllocateRtv();
                    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle  = d3d12_descriptors::GetRtvHandle(rtv_index);

                    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc  = {};
                    rtv_desc.Format                         = rtv_format;
                    rtv_desc.ViewDimension                  = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtv_desc.Texture2DArray.MipSlice        = 0;
                    rtv_desc.Texture2DArray.FirstArraySlice = 0;
                    rtv_desc.Texture2DArray.ArraySize       = array_length;

                    RHI_Context::device->CreateRenderTargetView(texture, &rtv_desc, rtv_handle);
                    m_rhi_rtv_multiview = reinterpret_cast<void*>(rtv_handle.ptr);
                }
            }
            else if (m_type == RHI_Texture_Type::Type3D)
            {
                uint32_t rtv_index                     = d3d12_descriptors::AllocateRtv();
                D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle  = d3d12_descriptors::GetRtvHandle(rtv_index);

                D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                rtv_desc.Format                        = rtv_format;
                rtv_desc.ViewDimension                 = D3D12_RTV_DIMENSION_TEXTURE3D;
                rtv_desc.Texture3D.MipSlice            = 0;
                rtv_desc.Texture3D.FirstWSlice         = 0;
                rtv_desc.Texture3D.WSize               = m_depth;

                RHI_Context::device->CreateRenderTargetView(texture, &rtv_desc, rtv_handle);
                m_rhi_rtv[0] = reinterpret_cast<void*>(rtv_handle.ptr);
            }
        }

        // create dsvs, one per array slice plus a multiview view covering all slices, cube faces count as slices
        if ((m_flags & RHI_Texture_Rtv) && is_depth_format)
        {
            if (m_type == RHI_Texture_Type::Type2D)
            {
                uint32_t dsv_index                     = d3d12_descriptors::AllocateDsv();
                D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle  = d3d12_descriptors::GetDsvHandle(dsv_index);

                D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
                dsv_desc.Format                        = dsv_format;
                dsv_desc.ViewDimension                 = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsv_desc.Texture2D.MipSlice            = 0;

                RHI_Context::device->CreateDepthStencilView(texture, &dsv_desc, dsv_handle);
                m_rhi_dsv[0] = reinterpret_cast<void*>(dsv_handle.ptr);
            }
            else if (m_type == RHI_Texture_Type::Type2DArray || m_type == RHI_Texture_Type::TypeCube)
            {
                const uint32_t slice_count = min<uint32_t>(array_length, rhi_max_render_target_count);
                for (uint32_t slice = 0; slice < slice_count; slice++)
                {
                    uint32_t dsv_index                     = d3d12_descriptors::AllocateDsv();
                    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle  = d3d12_descriptors::GetDsvHandle(dsv_index);

                    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc  = {};
                    dsv_desc.Format                         = dsv_format;
                    dsv_desc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsv_desc.Texture2DArray.MipSlice        = 0;
                    dsv_desc.Texture2DArray.FirstArraySlice = slice;
                    dsv_desc.Texture2DArray.ArraySize       = 1;

                    RHI_Context::device->CreateDepthStencilView(texture, &dsv_desc, dsv_handle);
                    m_rhi_dsv[slice] = reinterpret_cast<void*>(dsv_handle.ptr);
                }

                // multiview dsv covering all slices, used when render_target_array_index is unset
                if (array_length > 1)
                {
                    uint32_t dsv_index                     = d3d12_descriptors::AllocateDsv();
                    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle  = d3d12_descriptors::GetDsvHandle(dsv_index);

                    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc  = {};
                    dsv_desc.Format                         = dsv_format;
                    dsv_desc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsv_desc.Texture2DArray.MipSlice        = 0;
                    dsv_desc.Texture2DArray.FirstArraySlice = 0;
                    dsv_desc.Texture2DArray.ArraySize       = array_length;

                    RHI_Context::device->CreateDepthStencilView(texture, &dsv_desc, dsv_handle);
                    m_rhi_dsv_multiview = reinterpret_cast<void*>(dsv_handle.ptr);
                }
            }
        }

        // create uav at slot 0 for uav textures
        if (m_flags & RHI_Texture_Uav)
        {
            uint32_t uav_index = d3d12_descriptors::AllocateCbvSrvUavCpu();
            D3D12_CPU_DESCRIPTOR_HANDLE uav_handle = d3d12_descriptors::GetCbvSrvUavCpuHandle(uav_index);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.Format = dxgi_format;
            if (m_type == RHI_Texture_Type::Type2D)
            {
                uav_desc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Texture2D.MipSlice = 0;
            }
            else if (m_type == RHI_Texture_Type::Type2DArray || m_type == RHI_Texture_Type::TypeCube)
            {
                uav_desc.ViewDimension                  = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                uav_desc.Texture2DArray.MipSlice        = 0;
                uav_desc.Texture2DArray.FirstArraySlice = 0;
                uav_desc.Texture2DArray.ArraySize       = array_length;
            }
            else if (m_type == RHI_Texture_Type::Type3D)
            {
                uav_desc.ViewDimension         = D3D12_UAV_DIMENSION_TEXTURE3D;
                uav_desc.Texture3D.MipSlice    = 0;
                uav_desc.Texture3D.FirstWSlice = 0;
                uav_desc.Texture3D.WSize       = m_depth;
            }

            RHI_Context::device->CreateUnorderedAccessView(texture, nullptr, &uav_desc, uav_handle);
            // reuse the srv mips slot 0 to stash the uav cpu handle for SetTexture(uav=true)
            m_rhi_srv_mips[0] = reinterpret_cast<void*>(uav_handle.ptr);
        }

        return true;
    }

    void RHI_Texture::RHI_DestroyResource()
    {
        // evict cached descriptor sets keyed by this texture pointer
        RHI_Device::DescriptorSetInvalidateReferencingResource(this);

        // defer release, open command lists may still reference this resource
        if (m_rhi_resource)
        {
            ClearLayouts();
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Image, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_rhi_srv = nullptr;
        for (auto& v : m_rhi_srv_mips) v = nullptr;
        for (auto& v : m_rhi_srv_layers) v = nullptr;
        for (auto& v : m_rhi_rtv) v = nullptr;
        for (auto& v : m_rhi_dsv) v = nullptr;
        m_rhi_rtv_multiview = nullptr;
        m_rhi_dsv_multiview = nullptr;
    }

    void RHI_Texture::DestroyResourceImmediate()
    {
        RHI_Device::DescriptorSetInvalidateReferencingResource(this);

        if (m_rhi_resource)
        {
            d3d12_state::RemoveState(static_cast<ID3D12Resource*>(m_rhi_resource));
            static_cast<ID3D12Resource*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        m_rhi_srv = nullptr;
        for (auto& v : m_rhi_srv_mips) v = nullptr;
        for (auto& v : m_rhi_srv_layers) v = nullptr;
        for (auto& v : m_rhi_rtv) v = nullptr;
        for (auto& v : m_rhi_dsv) v = nullptr;
        m_rhi_rtv_multiview = nullptr;
        m_rhi_dsv_multiview = nullptr;
    }
}
