/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Texture2D.h"
#include "../RHI_Texture2DArray.h"
#include "../RHI_TextureCube.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static UINT GetBindFlags(uint16_t flags)
    {
        UINT flags_d3d11 = 0;

        flags_d3d11 |= (flags & RHI_Texture_Sampled)        ? D3D11_BIND_SHADER_RESOURCE    : 0;
        flags_d3d11 |= (flags & RHI_Texture_Storage)        ? D3D11_BIND_UNORDERED_ACCESS   : 0;
        flags_d3d11 |= (flags & RHI_Texture_DepthStencil)   ? D3D11_BIND_DEPTH_STENCIL      : 0;
        flags_d3d11 |= (flags & RHI_Texture_RenderTarget)   ? D3D11_BIND_RENDER_TARGET      : 0;

        return flags_d3d11;
    }

    static DXGI_FORMAT GetDepthFormat(RHI_Format format)
    {
        if (format == RHI_Format_D32_Float_S8X24_Uint)
            return DXGI_FORMAT_R32G8X24_TYPELESS;

        if (format == RHI_Format_D32_Float)
            return DXGI_FORMAT_R32_TYPELESS;

        return d3d11_format[format];
    }

    static DXGI_FORMAT GetDepthFormatDsv(RHI_Format format)
    {
        if (format == RHI_Format_D32_Float_S8X24_Uint)
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

        if (format == RHI_Format_D32_Float)
            return DXGI_FORMAT_D32_FLOAT;

        return d3d11_format[format];
    }

    static DXGI_FORMAT GetDepthFormatSrv(RHI_Format format)
    {
        if (format == RHI_Format_D32_Float_S8X24_Uint)
        {
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        }
        else if (format == RHI_Format_D32_Float)
        {
            return DXGI_FORMAT_R32_FLOAT;
        }

        return d3d11_format[format];
    }

    static bool CreateTexture(
        ID3D11Texture2D*& texture,
        const ResourceType resource_type,
        const uint32_t width,
        const uint32_t height,
        const uint32_t channel_count,
        const uint32_t array_size,
        const uint32_t mip_count,
        const uint32_t bits_per_channel,
        const DXGI_FORMAT format,
        const UINT flags,
        vector<RHI_Texture_Slice>& data,
        const shared_ptr<RHI_Device>& rhi_device
    )
    {
        const bool has_initial_data = !data.empty() && !data[0].mips.empty() && !data[0].mips[0].bytes.empty();

        // Describe
        D3D11_TEXTURE2D_DESC texture_desc   = {};
        texture_desc.Width                  = static_cast<UINT>(width);
        texture_desc.Height                 = static_cast<UINT>(height);
        texture_desc.ArraySize              = static_cast<UINT>(array_size);
        texture_desc.MipLevels              = static_cast<UINT>(mip_count);
        texture_desc.Format                 = format;
        texture_desc.SampleDesc.Count       = 1;
        texture_desc.SampleDesc.Quality     = 0;
        texture_desc.Usage                  = has_initial_data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags              = flags;
        texture_desc.MiscFlags              = 0;
        texture_desc.CPUAccessFlags         = 0;

        if (resource_type == ResourceType::TextureCube)
        {
            texture_desc.Usage      = (flags & D3D11_BIND_RENDER_TARGET) || (flags & D3D11_BIND_DEPTH_STENCIL) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
            texture_desc.MiscFlags  = D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        // Set initial data
        vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
        if (has_initial_data)
        {
            for (uint32_t index_array = 0; index_array < array_size; index_array++)
            {
                for (uint32_t index_mip = 0; index_mip < mip_count; index_mip++)
                {
                    uint32_t mip_width = width >> index_mip;

                    D3D11_SUBRESOURCE_DATA& subresource_data    = vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
                    subresource_data.pSysMem                    = data[index_array].mips[index_mip].bytes.data();       // Data pointer
                    subresource_data.SysMemPitch                = mip_width * channel_count * (bits_per_channel / 8);   // Line width in bytes
                    subresource_data.SysMemSlicePitch           = 0;                                                    // This is only used for 3D textures
                }
            }
        }

        // Create
        bool result = d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateTexture2D(&texture_desc, vec_subresource_data.data(), &texture));
        return result;
    }

    static bool CreateRenderTargetView(void* texture, array<void*, rhi_max_render_target_count>& views, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format                        = format;
        desc.ViewDimension                 = resource_type == ResourceType::Texture2d ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice       = 0;
        desc.Texture2DArray.ArraySize      = 1;

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            desc.Texture2DArray.FirstArraySlice = i;

            if (!d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11RenderTargetView**>(&views[i]))))
                return false;
        }

        return true;
    }

    static bool CreateDepthStencilView(void* texture, array<void*, rhi_max_render_target_count>& views, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const bool read_only, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_DEPTH_STENCIL_VIEW_DESC desc  = {};
        desc.Format                         = format;
        desc.ViewDimension                  = resource_type == ResourceType::Texture2d ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice        = 0;
        desc.Texture2DArray.ArraySize       = 1;
        desc.Flags                          = read_only ? D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL : 0;

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            desc.Texture2DArray.FirstArraySlice = i;

            if(!d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11DepthStencilView**>(&views[i]))))
                return false;
        }

        return true;
    }

    static bool CreateShaderResourceView(void* texture, void*& view, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const uint32_t mip_count, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_SHADER_RESOURCE_VIEW_DESC desc   = {};
        desc.Format                            = format;

        if (resource_type == ResourceType::Texture2d)
        {
            desc.ViewDimension                  = D3D11_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.MostDetailedMip = 0;
            desc.Texture2DArray.MipLevels       = static_cast<UINT>(mip_count);
            desc.Texture2DArray.ArraySize       = static_cast<UINT>(array_size);
        }
        else if (resource_type == ResourceType::Texture2dArray)
        {
            desc.ViewDimension                  = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.MostDetailedMip = 0;
            desc.Texture2DArray.MipLevels       = static_cast<UINT>(mip_count);
            desc.Texture2DArray.ArraySize       = static_cast<UINT>(array_size);
        }
        else if (resource_type == ResourceType::TextureCube)
        {
            desc.ViewDimension               = D3D11_SRV_DIMENSION_TEXTURECUBE;
            desc.TextureCube.MipLevels       = static_cast<UINT>(mip_count);
            desc.TextureCube.MostDetailedMip = 0;
        }

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11ShaderResourceView**>(&view)));
    }

    static bool CreateUnorderedAccessView(void* texture, void*& view, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc  = {};
        desc.Format                            = format;
        desc.ViewDimension                     = resource_type == ResourceType::Texture2d ? D3D11_UAV_DIMENSION_TEXTURE2D : D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.FirstArraySlice    = 0;
        desc.Texture2DArray.ArraySize          = static_cast<UINT>(array_size);

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11UnorderedAccessView**>(&view)));
    }

    void RHI_Texture::SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* command_list /*= nullptr*/)
    {
        m_layout = new_layout;
    }

    bool RHI_Texture::CreateResourceGpu()
    {
        // Validate
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetContextRhi()->device != nullptr);
    
        bool result_tex = true;
        bool result_srv = true;
        bool result_uav = true;
        bool result_rt  = true;
        bool result_ds  = true;

        // Get texture flags
        const UINT flags = GetBindFlags(m_flags);

        // Resolve formats
        const DXGI_FORMAT format        = GetDepthFormat(m_format);
        const DXGI_FORMAT format_dsv    = GetDepthFormatDsv(m_format);
        const DXGI_FORMAT format_srv    = GetDepthFormatSrv(m_format);

        ID3D11Texture2D* resource = nullptr;

        // TEXTURE
        result_tex = CreateTexture
        (
            resource,
            m_resource_type,
            m_width,
            m_height,
            m_channel_count,
            m_array_size,
            m_mip_count,
            m_bits_per_channel,
            format,
            flags,
            m_data,
            m_rhi_device
        );

        // RESOURCE VIEW
        if (IsSampled())
        {
            result_srv = CreateShaderResourceView(
                resource,
                m_resource_view[0],
                m_resource_type,
                format_srv,
                m_array_size,
                m_mip_count,
                m_rhi_device
            );
        }

        // UNORDERED ACCESS VIEW
        if (IsStorage())
        {
            result_uav = CreateUnorderedAccessView(resource, m_resource_view_unorderedAccess, m_resource_type, format, m_array_size, m_rhi_device);
        }

        // DEPTH-STENCIL VIEW
        if (IsDepthStencil())
        {
            result_ds = CreateDepthStencilView
            (
                resource,
                m_resource_view_depthStencil,
                m_resource_type,
                format_dsv,
                m_array_size,
                false,
                m_rhi_device
            );

            if (m_flags & RHI_Texture_DepthStencilReadOnly)
            {
                result_ds = CreateDepthStencilView
                (
                    resource,
                    m_resource_view_depthStencilReadOnly,
                    m_resource_type,
                    format_dsv,
                    m_array_size,
                    true,
                    m_rhi_device
                );
            }
        }

        // RENDER TARGET VIEW
        if (IsRenderTarget())
        {
            result_rt = CreateRenderTargetView
            (
                resource,
                m_resource_view_renderTarget,
                m_resource_type,
                format,
                m_array_size,
                m_rhi_device
            );
        }

        m_resource = static_cast<void*>(resource);

        return result_tex && result_srv && result_uav && result_rt && result_ds;
    }

    void RHI_Texture::DestroyResourceGpu()
    {
        d3d11_utility::release(static_cast<ID3D11Texture2D*>(m_resource));
        d3d11_utility::release(static_cast<ID3D11ShaderResourceView*>(m_resource_view[0]));
        d3d11_utility::release(static_cast<ID3D11ShaderResourceView*>(m_resource_view[1]));
        d3d11_utility::release(static_cast<ID3D11UnorderedAccessView*>(m_resource_view_unorderedAccess));

        for (void*& resource : m_resource_view_renderTarget)
        {
            d3d11_utility::release(static_cast<ID3D11RenderTargetView*>(resource));
        }

        for (void*& resource : m_resource_view_depthStencil)
        {
            d3d11_utility::release(static_cast<ID3D11DepthStencilView*>(resource));
        }

        for (void*& resource : m_resource_view_depthStencilReadOnly)
        {
            d3d11_utility::release(static_cast<ID3D11DepthStencilView*>(resource));
        }
    }
}
