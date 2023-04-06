/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "../RHI_Texture2D.h"
#include "../RHI_Texture2DArray.h"
#include "../RHI_TextureCube.h"
#include "../RHI_CommandList.h"
#include "../Rendering/Renderer.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static UINT get_bind_flags(RHI_Texture* texture)
    {
        UINT flags_d3d11 = 0;

        flags_d3d11 |= texture->IsSrv()                      ? D3D11_BIND_SHADER_RESOURCE  : 0;
        flags_d3d11 |= texture->IsUav()                      ? D3D11_BIND_UNORDERED_ACCESS : 0;
        flags_d3d11 |= texture->IsRenderTargetDepthStencil() ? D3D11_BIND_DEPTH_STENCIL    : 0;
        flags_d3d11 |= texture->IsRenderTargetColor()        ? D3D11_BIND_RENDER_TARGET    : 0;

        return flags_d3d11;
    }

    static DXGI_FORMAT get_depth_format(RHI_Format format)
    {
        if (format == RHI_Format::D32_Float_S8X24_Uint)
            return DXGI_FORMAT_R32G8X24_TYPELESS;

        if (format == RHI_Format::D32_Float)
            return DXGI_FORMAT_R32_TYPELESS;

        return d3d11_format[rhi_format_to_index(format)];
    }

    static DXGI_FORMAT get_depth_format_dsv(RHI_Format format)
    {
        if (format == RHI_Format::D32_Float_S8X24_Uint)
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

        if (format == RHI_Format::D32_Float)
            return DXGI_FORMAT_D32_FLOAT;

        return d3d11_format[rhi_format_to_index(format)];
    }

    static DXGI_FORMAT get_depth_format_srv(RHI_Format format)
    {
        if (format == RHI_Format::D32_Float_S8X24_Uint)
        {
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        }
        else if (format == RHI_Format::D32_Float)
        {
            return DXGI_FORMAT_R32_FLOAT;
        }

        return d3d11_format[rhi_format_to_index(format)];
    }

    static bool create_texture(
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
        vector<RHI_Texture_Slice>& data
    )
    {
        SP_ASSERT(width != 0);
        SP_ASSERT(height != 0);
        SP_ASSERT(array_size != 0);
        SP_ASSERT(mip_count != 0);

        const bool has_data = !data.empty() && !data[0].mips.empty() && !data[0].mips[0].bytes.empty();

        // Describe
        D3D11_TEXTURE2D_DESC texture_desc = {};
        texture_desc.Width                = static_cast<UINT>(width);
        texture_desc.Height               = static_cast<UINT>(height);
        texture_desc.ArraySize            = static_cast<UINT>(array_size);
        texture_desc.MipLevels            = static_cast<UINT>(mip_count);
        texture_desc.Format               = format;
        texture_desc.SampleDesc.Count     = 1;
        texture_desc.SampleDesc.Quality   = 0;
        texture_desc.Usage                = (has_data && !(flags & D3D11_BIND_UNORDERED_ACCESS)) ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags            = flags;
        texture_desc.MiscFlags            = 0;
        texture_desc.CPUAccessFlags       = 0;

        if (resource_type == ResourceType::TextureCube)
        {
            bool is_rt            = flags & D3D11_BIND_RENDER_TARGET;
            bool is_depth_stencil = flags & D3D11_BIND_DEPTH_STENCIL;

            texture_desc.Usage     = (is_rt || is_depth_stencil || !has_data) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
            texture_desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        // Set initial data
        vector<D3D11_SUBRESOURCE_DATA> texture_data;
        if (has_data)
        {
            SP_ASSERT(channel_count != 0);
            SP_ASSERT(bits_per_channel != 0);

            for (uint32_t index_array = 0; index_array < array_size; index_array++)
            {
                for (uint32_t index_mip = 0; index_mip < mip_count; index_mip++)
                {
                    D3D11_SUBRESOURCE_DATA& subresource_data = texture_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
                    subresource_data.pSysMem                 = data[index_array].mips[index_mip].bytes.data();                // Data pointer
                    subresource_data.SysMemPitch             = (width >> index_mip) * channel_count * (bits_per_channel / 8); // Line width in bytes
                    subresource_data.SysMemSlicePitch        = 0;                                                             // This is only used for 3D textures
                }
            }
        }

        // Create
        return d3d11_utility::error_check(Renderer::GetRhiDevice()->GetRhiContext()->device->CreateTexture2D(&texture_desc, texture_data.data(), &texture));
    }

    static bool create_render_target_view(void* texture, array<void*, rhi_max_render_target_count>& views, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size)
    {
        SP_ASSERT(texture != nullptr);

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

            if (!d3d11_utility::error_check(Renderer::GetRhiDevice()->GetRhiContext()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11RenderTargetView**>(&views[i]))))
                return false;
        }

        return true;
    }

    static bool create_depth_stencil_view(void* texture, array<void*, rhi_max_render_target_count>& views, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const bool has_stencil, const bool read_only)
    {
        SP_ASSERT(texture != nullptr);

        // Describe
        D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
        desc.Format                        = format;
        desc.ViewDimension                 = resource_type == ResourceType::Texture2d ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice       = 0;
        desc.Texture2DArray.ArraySize      = 1;
        desc.Flags                         = 0;

        if (read_only)
        {
            desc.Flags |= D3D11_DSV_READ_ONLY_DEPTH;

            if (has_stencil)
            {
                desc.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
            }
        }

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            desc.Texture2DArray.FirstArraySlice = i;

            if(!d3d11_utility::error_check(Renderer::GetRhiDevice()->GetRhiContext()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11DepthStencilView**>(&views[i]))))
                return false;
        }

        return true;
    }

    static bool create_shader_resource_view(void* texture, void*& view, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const uint32_t mip_count, const uint32_t top_mip)
    {
        SP_ASSERT(texture != nullptr);

        // Describe
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format                          = format;

        if (resource_type == ResourceType::Texture2d)
        {
            desc.ViewDimension                  = D3D11_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.MostDetailedMip = top_mip;
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
        return d3d11_utility::error_check(Renderer::GetRhiDevice()->GetRhiContext()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11ShaderResourceView**>(&view)));
    }

    static bool create_unordered_access_view(void* texture, void*& view, const ResourceType resource_type, const DXGI_FORMAT format, const uint32_t array_size, const uint32_t mip)
    {
        SP_ASSERT(texture != nullptr);

        // Describe
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format                           = format;

        if (resource_type == ResourceType::Texture2d)
        {
            desc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = mip;
        }
        else if (resource_type == ResourceType::Texture2dArray)
        {
            desc.ViewDimension                  = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice        = mip;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize       = static_cast<UINT>(array_size);
        }

        // Create
        return d3d11_utility::error_check(Renderer::GetRhiDevice()->GetRhiContext()->device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(texture), &desc, reinterpret_cast<ID3D11UnorderedAccessView**>(&view)));
    }

    void RHI_Texture::RHI_SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* cmd_list, const uint32_t mip_index, const uint32_t mip_range)
    {

    }

    bool RHI_Texture::RHI_CreateResource()
    {
        bool result_tex = true;
        bool result_srv = true;
        bool result_uav = true;
        bool result_rt  = true;
        bool result_ds  = true;

        // Get texture flags
        const UINT flags = get_bind_flags(this);

        // Resolve formats
        const DXGI_FORMAT format     = get_depth_format(m_format);
        const DXGI_FORMAT format_dsv = get_depth_format_dsv(m_format);
        const DXGI_FORMAT format_srv = get_depth_format_srv(m_format);

        ID3D11Texture2D* resource = nullptr;

        // TEXTURE
        result_tex = create_texture
        (
            resource,
            m_resource_type,
            m_width,
            m_height,
            m_channel_count,
            m_array_length,
            m_mip_count,
            m_bits_per_channel,
            format,
            flags,
            m_data
        );

        SP_ASSERT(resource != nullptr);

        // RESOURCE VIEW
        if (IsSrv())
        {
            result_srv = create_shader_resource_view(
                resource,
                m_rhi_srv,
                m_resource_type,
                format_srv,
                m_array_length,
                m_mip_count,
                0
            );

            if (HasPerMipViews())
            {
                for (uint32_t i = 0; i < m_mip_count; i++)
                {
                    bool result = create_shader_resource_view(
                        resource,
                        m_rhi_srv_mips[i],
                        m_resource_type,
                        format_srv,
                        m_array_length,
                        1,
                        i
                    );

                    result_srv = !result ? false : result_srv;
                }
            }
        }

        // UNORDERED ACCESS VIEW
        if (IsUav())
        {
            result_uav = create_unordered_access_view(
                resource,
                m_rhi_uav,
                m_resource_type,
                format,
                m_array_length,
                0
            );

            if (HasPerMipViews())
            {
                for (uint32_t i = 0; i < m_mip_count; i++)
                {
                    bool result = create_unordered_access_view(
                        resource,
                        m_rhi_uav_mips[i],
                        m_resource_type,
                        format,
                        1,
                        i
                    );

                    result_uav = !result ? false : result_uav;
                }
            }
        }

        // DEPTH-STENCIL VIEW
        if (IsRenderTargetDepthStencil())
        {
            result_ds = create_depth_stencil_view
            (
                resource,
                m_rhi_dsv,
                m_resource_type,
                format_dsv,
                m_array_length,
                IsStencilFormat(),
                false
            );

            if (m_flags & RHI_Texture_RenderTarget_ReadOnly)
            {
                result_ds = create_depth_stencil_view
                (
                    resource,
                    m_rhi_dsv_read_only,
                    m_resource_type,
                    format_dsv,
                    m_array_length,
                    IsStencilFormat(),
                    true
                );
            }
        }

        // RENDER TARGET VIEW
        if (IsRenderTargetColor())
        {
            result_rt = create_render_target_view
            (
                resource,
                m_rhi_rtv,
                m_resource_type,
                format,
                m_array_length
            );
        }

        m_rhi_resource = static_cast<void*>(resource);

        return result_tex && result_srv && result_uav && result_rt && result_ds;
    }

    void RHI_Texture::RHI_DestroyResource(const bool destroy_main, const bool destroy_per_view)
    {
        if (destroy_main)
        {
            d3d11_utility::release<ID3D11Texture2D>(m_rhi_resource);
            d3d11_utility::release<ID3D11ShaderResourceView>(m_rhi_srv);
            d3d11_utility::release<ID3D11UnorderedAccessView>(m_rhi_uav);

            for (void*& resource : m_rhi_rtv)
            {
                d3d11_utility::release<ID3D11RenderTargetView>(resource);
            }

            for (void*& resource : m_rhi_dsv)
            {
                d3d11_utility::release<ID3D11DepthStencilView>(resource);
            }

            for (void*& resource : m_rhi_dsv_read_only)
            {
                d3d11_utility::release<ID3D11DepthStencilView>(resource);
            }
        }

        if (destroy_per_view)
        {
            for (uint32_t i = 0; i < m_mip_count; i++)
            {
                d3d11_utility::release<ID3D11ShaderResourceView>(m_rhi_srv_mips[i]);
                d3d11_utility::release<ID3D11UnorderedAccessView>(m_rhi_uav_mips[i]);
            }
        }
    }
}
