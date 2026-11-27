/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Texture.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "D3D12_Internal.h"
#include <mutex>
//================================

using namespace std;
using namespace spartan::math;

namespace spartan
{
    bool RHI_Texture::RHI_UpdateRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data, bool /*defer_to_frame*/)
    {
        ID3D12Resource* texture = static_cast<ID3D12Resource*>(m_rhi_resource);
        if (!texture || !RHI_Context::device)
        {
            return false;
        }

        // buffer to texture copies want 256 byte rows, the source is tightly packed so rows are re-laid here
        const uint32_t bytes_per_pixel = GetBytesPerPixel();
        const uint32_t row_bytes       = width * bytes_per_pixel;
        const uint32_t row_pitch       = (row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        const uint64_t staging_size    = static_cast<uint64_t>(row_pitch) * height;
        if (row_bytes == 0)
        {
            return false;
        }

        static mutex update_mutex;
        lock_guard<mutex> update_lock(update_mutex);

        ID3D12Resource* staging = static_cast<ID3D12Resource*>(RHI_Device::StagingBufferAcquire(staging_size));
        if (!staging)
        {
            return false;
        }

        void* mapped = nullptr;
        D3D12_RANGE read_range = { 0, 0 };
        if (FAILED(staging->Map(0, &read_range, &mapped)) || !mapped)
        {
            RHI_Device::StagingBufferRelease(staging);
            return false;
        }

        for (uint32_t row = 0; row < height; row++)
        {
            memcpy(
                static_cast<uint8_t*>(mapped) + static_cast<size_t>(row) * row_pitch,
                static_cast<const uint8_t*>(data) + static_cast<size_t>(row) * row_bytes,
                row_bytes
            );
        }
        staging->Unmap(0, nullptr);

        bool copied = false;
        if (RHI_CommandList* cmd_list_rhi = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list_rhi->GetRhiResource());

            const D3D12_BARRIER_LAYOUT layout_before = d3d12_state::GetLayout(texture);
            if (layout_before != D3D12_BARRIER_LAYOUT_COPY_DEST)
            {
                d3d12_barriers::SubmitTexture(cmd_list, texture, layout_before, D3D12_BARRIER_LAYOUT_COPY_DEST);
            }

            D3D12_TEXTURE_COPY_LOCATION dest_location = {};
            dest_location.pResource        = texture;
            dest_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dest_location.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION src_location = {};
            src_location.pResource                   = staging;
            src_location.Type                        = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src_location.PlacedFootprint.Offset      = 0;
            src_location.PlacedFootprint.Footprint.Format   = texture->GetDesc().Format;
            src_location.PlacedFootprint.Footprint.Width    = width;
            src_location.PlacedFootprint.Footprint.Height   = height;
            src_location.PlacedFootprint.Footprint.Depth    = 1;
            src_location.PlacedFootprint.Footprint.RowPitch = row_pitch;

            cmd_list->CopyTextureRegion(&dest_location, x, y, 0, &src_location, nullptr);

            // return to prior layout; fresh textures settle in shader_resource or unordered_access
            D3D12_BARRIER_LAYOUT layout_after = layout_before;
            if (layout_after == D3D12_BARRIER_LAYOUT_COPY_DEST || layout_after == D3D12_BARRIER_LAYOUT_COMMON || layout_after == D3D12_BARRIER_LAYOUT_UNDEFINED)
            {
                layout_after = (m_flags & RHI_Texture_Uav)
                    ? D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS
                    : D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
            }

            if (layout_after != D3D12_BARRIER_LAYOUT_COPY_DEST)
            {
                d3d12_barriers::SubmitTexture(cmd_list, texture, D3D12_BARRIER_LAYOUT_COPY_DEST, layout_after);
            }

            RHI_CommandList::ImmediateExecutionEnd(cmd_list_rhi);

            d3d12_state::SetLayout(texture, layout_after);
            copied = true;
        }

        RHI_Device::StagingBufferRelease(staging);
        return copied;
    }

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

        DXGI_FORMAT castable_formats[3] = {};
        uint32_t castable_format_count  = 0;
        if (is_depth_format && (m_flags & RHI_Texture_Srv))
        {
            switch (m_format)
            {
                case RHI_Format::D16_Unorm:
                    dsv_format = DXGI_FORMAT_D16_UNORM;
                    srv_format = DXGI_FORMAT_R16_UNORM;
                    if (d3d12_caps::IsRelaxedFormatCastingSupported())
                    {
                        resource_format       = dsv_format;
                        castable_formats[0]   = dsv_format;
                        castable_formats[1]   = srv_format;
                        castable_format_count = 2;
                    }
                    else
                    {
                        resource_format = DXGI_FORMAT_R16_TYPELESS;
                    }
                    break;
                case RHI_Format::D32_Float:
                    dsv_format = DXGI_FORMAT_D32_FLOAT;
                    srv_format = DXGI_FORMAT_R32_FLOAT;
                    if (d3d12_caps::IsRelaxedFormatCastingSupported())
                    {
                        resource_format       = dsv_format;
                        castable_formats[0]   = dsv_format;
                        castable_formats[1]   = srv_format;
                        castable_format_count = 2;
                    }
                    else
                    {
                        resource_format = DXGI_FORMAT_R32_TYPELESS;
                    }
                    break;
                case RHI_Format::D32_Float_S8X24_Uint:
                    dsv_format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                    srv_format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                    if (d3d12_caps::IsRelaxedFormatCastingSupported())
                    {
                        resource_format       = dsv_format;
                        castable_formats[0]   = dsv_format;
                        castable_formats[1]   = srv_format;
                        castable_formats[2]   = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
                        castable_format_count = 3;
                    }
                    else
                    {
                        resource_format = DXGI_FORMAT_R32G8X24_TYPELESS;
                    }
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

        D3D12_RESOURCE_DESC1 resource_desc = {};
        resource_desc.Format               = resource_format;
        resource_desc.Width                = m_width;
        resource_desc.Height               = m_height;
        resource_desc.MipLevels            = static_cast<UINT16>(m_mip_count);
        resource_desc.SampleDesc.Count     = 1;
        resource_desc.SampleDesc.Quality   = 0;
        resource_desc.Layout               = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags                = D3D12_RESOURCE_FLAG_NONE;

        uint32_t array_length = GetArrayLength();
        m_rhi_rtv.resize(IsRtv() ? array_length : 0, nullptr);
        m_rhi_dsv.resize(IsDsv() ? array_length : 0, nullptr);

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

        // enhanced barriers allow Direct and Compute queues to share textures via queue-common layouts
        // (SHADER_RESOURCE, UNORDERED_ACCESS, COPY_SOURCE, COPY_DEST) without ALLOW_SIMULTANEOUS_ACCESS,
        // keeping full GPU color compression (DCC) and fast clears enabled

        D3D12_CLEAR_VALUE clear_value = {};
        D3D12_CLEAR_VALUE* clear_value_ptr = nullptr;
        if (is_depth_format && (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        {
            clear_value.Format               = dsv_format;
            clear_value.DepthStencil.Depth   = 1.0f;
            clear_value.DepthStencil.Stencil = 0;
            clear_value_ptr                  = &clear_value;
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

        // create via ID3D12Device10::CreateCommittedResource3 in D3D12_BARRIER_LAYOUT_COMMON with explicit castable formats
        ID3D12Resource* texture = nullptr;
        HRESULT hr = RHI_Context::device->CreateCommittedResource3(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_BARRIER_LAYOUT_COMMON,
            clear_value_ptr,
            nullptr,
            castable_format_count,
            castable_format_count > 0 ? castable_formats : nullptr,
            IID_PPV_ARGS(&texture)
        );

        if (FAILED(hr))
        {
            if (hr == E_OUTOFMEMORY || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
                d3d12_utility::error::fatal(hr, "Allocate texture (CreateCommittedResource3)", m_object_name.c_str());
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

        GpuMemoryDetail detail = {};
        detail.width     = m_width;
        detail.height    = m_height;
        detail.depth     = m_depth;
        detail.mip_count = m_mip_count;
        detail.format    = rhi_format_to_string(m_format);
        detail.path      = GetResourceFilePath().c_str();
        switch (m_type)
        {
            case RHI_Texture_Type::Type2D:      detail.type = "2D"; break;
            case RHI_Texture_Type::Type2DArray: detail.type = "2DArray"; break;
            case RHI_Texture_Type::Type3D:      detail.type = "3D"; break;
            case RHI_Texture_Type::TypeCube:    detail.type = "Cube"; break;
            default: break;
        }
        d3d12_gpu_memory::register_resource(
            texture,
            GpuMemoryKind::Texture,
            m_object_name.c_str(),
            detail
        );

        // seed the d3d12 enhanced layout tracker
        d3d12_state::SetLayout(texture, D3D12_BARRIER_LAYOUT_COMMON);
        d3d12_state::SetIsDepth(texture, is_depth_format);
        const D3D12_RESOURCE_FLAGS writable_flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        d3d12_state::SetReadOnly(texture, (resource_desc.Flags & writable_flags) == 0);
        d3d12_state::SetAllowsUav(texture, (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
        if (m_flags & RHI_Texture_ConcurrentSharing)
        {
            d3d12_state::SetCrossQueue(texture);
        }
        {
            const uint32_t slices = (m_type == RHI_Texture_Type::Type3D) ? 1u : static_cast<uint32_t>(resource_desc.DepthOrArraySize);
            d3d12_state::SetSubresourceCount(texture, m_mip_count * slices);
        }

        // set initial layout matching the initial state
        {
            SetLayoutDirect(0, m_mip_count, RHI_Image_Layout::General);
        }

        // upload initial data via a staging buffer
        if (HasData())
        {
            static mutex upload_mutex;
            lock_guard<mutex> upload_lock(upload_mutex);

            uint32_t subresource_count = m_mip_count * array_length;

            uint64_t upload_buffer_size = 0;
            RHI_Context::device->GetCopyableFootprints1(&resource_desc, 0, subresource_count, 0, nullptr, nullptr, nullptr, &upload_buffer_size);

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
                    RHI_Context::device->GetCopyableFootprints1(&resource_desc, 0, subresource_count, 0,
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
                                    const uint64_t rows = static_cast<uint64_t>(num_rows[subresource]) * layouts[subresource].Footprint.Depth;
                                    SP_ASSERT(rows * row_sizes[subresource] <= mip_data.size());
                                    for (uint64_t row = 0; row < rows; row++)
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

                    RHI_CommandList* upload = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics);
                    if (upload)
                    {
                        auto* temp_cmd_list = static_cast<ID3D12GraphicsCommandList*>(upload->GetRhiResource());
                        d3d12_barriers::SubmitTexture(temp_cmd_list, texture, D3D12_BARRIER_LAYOUT_COMMON, D3D12_BARRIER_LAYOUT_COPY_DEST);

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

                        // shader_resource is valid on both graphics and compute queues under enhanced barriers
                        const D3D12_BARRIER_LAYOUT post_layout_d3d = (m_flags & RHI_Texture_Uav)
                            ? D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS
                            : D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
                        d3d12_barriers::SubmitTexture(temp_cmd_list, texture, D3D12_BARRIER_LAYOUT_COPY_DEST, post_layout_d3d);

                        upload->RetainStagingBuffer(upload_buffer);
                        RHI_CommandList::ImmediateExecutionEnd(upload, false);
                        // The staging allocation must survive the copy fence.
                        upload_buffer = nullptr;

                        // reflect the queued post-copy layout in both the rhi layout map and the d3d12 tracker
                        SetLayoutDirect(0, m_mip_count, RHI_Image_Layout::General);
                        d3d12_state::SetLayout(texture, post_layout_d3d);
                    }
                }

                if (upload_buffer) RHI_Device::StagingBufferRelease(upload_buffer);
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
                const uint32_t layer_count = array_length;
                m_rhi_srv_layers.resize(layer_count, nullptr);
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
                const uint32_t slice_count = array_length;
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
                const uint32_t slice_count = array_length;
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
        // nothing to invalidate on d3d12 (no descriptor set cache)

        // defer release, open command lists may still reference this resource
        if (m_rhi_resource)
        {
            ClearLayouts();
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Image, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        d3d12_descriptors::FreeCbvSrvUavCpu(m_rhi_srv);
        m_rhi_srv = nullptr;
        for (auto& v : m_rhi_srv_mips) { d3d12_descriptors::FreeCbvSrvUavCpu(v); v = nullptr; }
        for (auto& v : m_rhi_srv_layers) { d3d12_descriptors::FreeCbvSrvUavCpu(v); v = nullptr; }
        for (auto& v : m_rhi_rtv) { d3d12_descriptors::FreeRtv(v); v = nullptr; }
        for (auto& v : m_rhi_dsv) { d3d12_descriptors::FreeDsv(v); v = nullptr; }
        m_rhi_srv_layers.clear();
        m_rhi_rtv.clear();
        m_rhi_dsv.clear();
        d3d12_descriptors::FreeRtv(m_rhi_rtv_multiview);
        m_rhi_rtv_multiview = nullptr;
        d3d12_descriptors::FreeDsv(m_rhi_dsv_multiview);
        m_rhi_dsv_multiview = nullptr;
    }

}
