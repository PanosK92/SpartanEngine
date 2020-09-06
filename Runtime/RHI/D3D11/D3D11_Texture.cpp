/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../RHI_TextureCube.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    inline UINT GetBindFlags(uint16_t flags)
    {
        UINT flags_d3d11 = 0;

        flags_d3d11 |= (flags & RHI_Texture_Sampled)        ? D3D11_BIND_SHADER_RESOURCE    : 0;
        flags_d3d11 |= (flags & RHI_Texture_Storage)        ? D3D11_BIND_UNORDERED_ACCESS   : 0;
        flags_d3d11 |= (flags & RHI_Texture_DepthStencil)   ? D3D11_BIND_DEPTH_STENCIL      : 0;
        flags_d3d11 |= (flags & RHI_Texture_RenderTarget)   ? D3D11_BIND_RENDER_TARGET      : 0;

        return flags_d3d11;
    }

    inline DXGI_FORMAT GetDepthFormat(RHI_Format format)
    {
        if (format == RHI_Format_D32_Float_S8X24_Uint)
        {
            return DXGI_FORMAT_R32G8X24_TYPELESS;
        }
        else if (format == RHI_Format_D32_Float)
        {
            return DXGI_FORMAT_R32_TYPELESS;
        }

        return d3d11_format[format];
    }

    inline DXGI_FORMAT GetDepthFormatDsv(RHI_Format format)
    {
        if (format == RHI_Format_D32_Float_S8X24_Uint)
        {
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        }
        else if (format == RHI_Format_D32_Float)
        {
            return DXGI_FORMAT_D32_FLOAT;
        }

        return d3d11_format[format];
    }

    inline DXGI_FORMAT GetDepthFormatSrv(RHI_Format format)
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

    // TEXTURE 2D

    inline bool CreateTexture2d(
        void*& texture,
        const uint32_t width,
        const uint32_t height,
        const uint32_t channels,
        const uint32_t bits_per_channel,
        const uint32_t array_size,
        const uint8_t mip_count,
        const DXGI_FORMAT format,
        const UINT bind_flags,
        vector<vector<std::byte>>& data,
        const shared_ptr<RHI_Device>& rhi_device
    )
    {
        const bool has_initial_data = data.size() == mip_count;

        // Describe
        D3D11_TEXTURE2D_DESC texture_desc   = {};
        texture_desc.Width                  = static_cast<UINT>(width);
        texture_desc.Height                 = static_cast<UINT>(height);
        texture_desc.MipLevels              = mip_count;
        texture_desc.ArraySize              = static_cast<UINT>(array_size);
        texture_desc.Format                 = format;
        texture_desc.SampleDesc.Count       = 1;
        texture_desc.SampleDesc.Quality     = 0;
        texture_desc.Usage                  = has_initial_data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags              = bind_flags;
        texture_desc.MiscFlags              = 0;
        texture_desc.CPUAccessFlags         = 0;

        // Set initial data
        vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
        if (has_initial_data)
        { 
            for (uint8_t i = 0; i < mip_count; i++)
            {
                D3D11_SUBRESOURCE_DATA& subresource_data    = vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
                subresource_data.pSysMem                    = i < data.size()? data[i].data() : nullptr;        // Data pointer
                subresource_data.SysMemPitch                = (width >> i) * channels * (bits_per_channel / 8); // Line width in bytes
                subresource_data.SysMemSlicePitch           = 0;                                                // This is only used for 3D textures
            }
        }

        // Create
         return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateTexture2D(&texture_desc, vec_subresource_data.data(), reinterpret_cast<ID3D11Texture2D**>(&texture)));
    }

    inline bool CreateRenderTargetView2d(void* texture, array<void*, rhi_max_render_target_count>& views, const DXGI_FORMAT format, const unsigned array_size, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_RENDER_TARGET_VIEW_DESC view_desc     = {};
        view_desc.Format                            = format;
        view_desc.ViewDimension                     = (array_size == 1) ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray.MipSlice           = 0;
        view_desc.Texture2DArray.ArraySize          = 1;
        view_desc.Texture2DArray.FirstArraySlice    = 0;

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            view_desc.Texture2DArray.FirstArraySlice = i;
            const auto result = rhi_device->GetContextRhi()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(texture), &view_desc, reinterpret_cast<ID3D11RenderTargetView**>(&views[i]));
            if (FAILED(result))
            {
                LOG_ERROR("Failed, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }
        }

        return true;
    }

    inline bool CreateDepthStencilView2d(void* texture, array<void*, rhi_max_render_target_count>& views, const uint32_t array_size, const DXGI_FORMAT format, bool read_only, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc  = {};
        dsv_desc.Format                         = format;
        dsv_desc.ViewDimension                  = (array_size == 1) ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv_desc.Texture2DArray.MipSlice        = 0;
        dsv_desc.Texture2DArray.ArraySize       = 1;
        dsv_desc.Texture2DArray.FirstArraySlice = 0;
        dsv_desc.Flags                          = read_only ? D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL : 0;

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            dsv_desc.Texture2DArray.FirstArraySlice = i;
            const auto result = rhi_device->GetContextRhi()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(texture), &dsv_desc, reinterpret_cast<ID3D11DepthStencilView**>(&views[i]));
            if (FAILED(result))
            {
                LOG_ERROR("Failed, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }
        }

        return true;
    }

    inline bool CreateShaderResourceView2d(void* texture, void*& view, DXGI_FORMAT format, uint32_t array_size, vector<vector<std::byte>>& data, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc   = {};
        shader_resource_view_desc.Format                            = format;
        shader_resource_view_desc.ViewDimension                     = (array_size == 1) ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        shader_resource_view_desc.Texture2DArray.FirstArraySlice    = 0;
        shader_resource_view_desc.Texture2DArray.MostDetailedMip    = 0;
        shader_resource_view_desc.Texture2DArray.MipLevels          = data.empty() ? 1 : static_cast<UINT>(data.size());
        shader_resource_view_desc.Texture2DArray.ArraySize          = array_size;

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(texture), &shader_resource_view_desc, reinterpret_cast<ID3D11ShaderResourceView**>(&view)));
    }

    inline bool CreateUnorderedAccessView2d(void* texture, void*& view, DXGI_FORMAT format, int32_t array_size, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_UNORDERED_ACCESS_VIEW_DESC unorderd_access_view_desc  = {};
        unorderd_access_view_desc.Format                            = format;
        unorderd_access_view_desc.ViewDimension                     = (array_size == 1) ? D3D11_UAV_DIMENSION_TEXTURE2D : D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        unorderd_access_view_desc.Texture2DArray.FirstArraySlice    = 0;
        unorderd_access_view_desc.Texture2DArray.ArraySize          = array_size;

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(texture), &unorderd_access_view_desc, reinterpret_cast<ID3D11UnorderedAccessView**>(&view)));
    }

    RHI_Texture2D::~RHI_Texture2D()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11ShaderResourceView**>(&m_resource_view[0]));
        d3d11_utility::release(*reinterpret_cast<ID3D11UnorderedAccessView**>(&m_resource_view_unorderedAccess));
        d3d11_utility::release(*reinterpret_cast<ID3D11Texture2D**>(&m_resource));
        for (void*& render_target : m_resource_view_renderTarget)
        {
            d3d11_utility::release(*reinterpret_cast<ID3D11RenderTargetView**>(&render_target));
        }
        for (void*& depth_stencil : m_resource_view_depthStencil)
        {
            d3d11_utility::release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
        for (void*& depth_stencil : m_resource_view_depthStencilReadOnly)
        {
            d3d11_utility::release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
    }

    void RHI_Texture::SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* command_list /*= nullptr*/)
    {
        m_layout = new_layout;
    }

    bool RHI_Texture2D::CreateResourceGpu()
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }
    
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

        // TEXTURE
        result_tex = CreateTexture2d
        (
            m_resource,
            m_width,
            m_height,
            m_channel_count,
            m_bits_per_channel,
            m_array_size,
            m_mip_count,
            format,
            flags,
            m_data,
            m_rhi_device
        );

        // RESOURCE VIEW
        if (IsSampled())
        {
            result_srv = CreateShaderResourceView2d(
                m_resource,
                m_resource_view[0],
                format_srv,
                m_array_size,
                m_data,
                m_rhi_device
            );
        }

        // UNORDERED ACCESS VIEW
        if (IsStorage())
        {
            result_uav = CreateUnorderedAccessView2d(m_resource, m_resource_view_unorderedAccess, format, m_array_size, m_rhi_device);
        }

        // DEPTH-STENCIL VIEW
        if (IsDepthStencil())
        {
            result_ds = CreateDepthStencilView2d
            (
                m_resource,
                m_resource_view_depthStencil,
                m_array_size,
                format_dsv,
                false,
                m_rhi_device
            );

            if (m_flags & RHI_Texture_DepthStencilReadOnly)
            {
                result_ds = CreateDepthStencilView2d
                (
                    m_resource,
                    m_resource_view_depthStencilReadOnly,
                    m_array_size,
                    format_dsv,
                    true,
                    m_rhi_device
                );
            }
        }

        // RENDER TARGET VIEW
        if (IsRenderTarget())
        {
            result_rt = CreateRenderTargetView2d
            (
                m_resource,
                m_resource_view_renderTarget,
                format,
                m_array_size,
                m_rhi_device
            );
        }

        d3d11_utility::release(*reinterpret_cast<ID3D11Texture2D**>(&m_resource));

        return result_tex && result_srv && result_uav && result_rt && result_ds;
    }

    // TEXTURE CUBE

    inline bool CreateTextureCube(
        void*& texture,
        const uint32_t width,
        const uint32_t height,
        uint32_t channels,
        uint32_t array_size,
        uint32_t bits_per_channel,
        DXGI_FORMAT format,
        const UINT flags,
        vector<vector<vector<std::byte>>>& data,
        const shared_ptr<RHI_Device>& rhi_device
    )
    {
        // Describe
        D3D11_TEXTURE2D_DESC texture_desc   = {};
        texture_desc.Width                  = static_cast<UINT>(width);
        texture_desc.Height                 = static_cast<UINT>(height);
        texture_desc.MipLevels              = static_cast<UINT>(data.empty() || data[0].empty() ? 1 : data[0].size());
        texture_desc.ArraySize              = array_size;
        texture_desc.Format                 = format;
        texture_desc.SampleDesc.Count       = 1;
        texture_desc.SampleDesc.Quality     = 0;
        texture_desc.Usage                  = (flags & D3D11_BIND_RENDER_TARGET) || (flags & D3D11_BIND_DEPTH_STENCIL) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
        texture_desc.BindFlags              = flags;
        texture_desc.MiscFlags              = D3D11_RESOURCE_MISC_TEXTURECUBE;
        texture_desc.CPUAccessFlags         = 0;

        vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
        vector<D3D11_TEXTURE2D_DESC> vec_texture_desc;

        if (!data.empty())
        { 
            for (uint32_t face_index = 0; face_index < array_size; face_index++)
            {
                const auto& face_data = data[face_index];

                if (face_data.empty())
                {
                    LOG_ERROR("A side contains no data");
                    continue;
                }

                for (uint32_t mip_level = 0; mip_level < static_cast<uint32_t>(face_data.size()); mip_level++)
                {
                    const auto& mip_data = face_data[mip_level];

                    if (mip_data.empty())
                    {
                        LOG_ERROR("A mip-map contains invalid data.");
                        continue;
                    }

                    // D3D11_SUBRESOURCE_DATA
                    auto & subresource_data             = vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
                    subresource_data.pSysMem            = mip_data.data();                                          // Data pointer
                    subresource_data.SysMemPitch        = (width >> mip_level) * channels * (bits_per_channel / 8); // Line width in bytes
                    subresource_data.SysMemSlicePitch   = 0;                                                        // This is only used for 3D textures
                }

                vec_texture_desc.emplace_back(texture_desc);
            }
        }
        else
        {
            vec_texture_desc.emplace_back(texture_desc);
        }

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateTexture2D(vec_texture_desc.data(), vec_subresource_data.data(), reinterpret_cast<ID3D11Texture2D**>(&texture)));
    }

    inline bool CreateShaderResourceViewCube(void* texture, void*& view, DXGI_FORMAT format, uint32_t array_size, vector<vector<vector<std::byte>>>& data, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc    = {};
        srv_desc.Format                             = format;
        srv_desc.ViewDimension                      = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv_desc.TextureCube.MipLevels              = static_cast<UINT>(data.empty() || data[0].empty() ? 1 : data[0].size());
        srv_desc.TextureCube.MostDetailedMip        = 0;
        srv_desc.Texture2DArray.FirstArraySlice     = 0;
        srv_desc.Texture2DArray.MostDetailedMip     = 0;
        srv_desc.Texture2DArray.MipLevels           = 1;
        srv_desc.Texture2DArray.ArraySize           = array_size;

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(texture), &srv_desc, reinterpret_cast<ID3D11ShaderResourceView**>(&view)));
    }

    inline bool CreateDepthStencilViewCube(void* texture, array<void*, rhi_max_render_target_count>& views, const uint32_t array_size, const DXGI_FORMAT format, bool read_only, const shared_ptr<RHI_Device>& rhi_device)
    {
        // DSV
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc  = {};
        dsv_desc.Format                         = format;
        dsv_desc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv_desc.Texture2DArray.MipSlice        = 0;
        dsv_desc.Texture2DArray.ArraySize       = 1;
        dsv_desc.Texture2DArray.FirstArraySlice = 0;

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            dsv_desc.Texture2DArray.FirstArraySlice = i;
            const auto result = rhi_device->GetContextRhi()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(texture), &dsv_desc, reinterpret_cast<ID3D11DepthStencilView**>(&views[i]));
            if (FAILED(result))
            {
                LOG_ERROR("Failed, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }
        }

        return true;
    }

    inline bool CreateRenderTargetViewCube(void* texture, array<void*, rhi_max_render_target_count>& views, const DXGI_FORMAT format, const unsigned array_size, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_RENDER_TARGET_VIEW_DESC view_desc     = {};
        view_desc.Format                            = format;
        view_desc.ViewDimension                     = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        view_desc.Texture2DArray.MipSlice           = 0;
        view_desc.Texture2DArray.ArraySize          = 1;
        view_desc.Texture2DArray.FirstArraySlice    = 0;

        // Create
        for (uint32_t i = 0; i < array_size; i++)
        {
            view_desc.Texture2DArray.FirstArraySlice = i;
            const auto result = rhi_device->GetContextRhi()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(texture), &view_desc, reinterpret_cast<ID3D11RenderTargetView**>(&views[i]));
            if (FAILED(result))
            {
                LOG_ERROR("Failed, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }
        }

        return true;
    }

    inline bool CreateUnorderedAccessViewCube(void* texture, void*& view, DXGI_FORMAT format, const shared_ptr<RHI_Device>& rhi_device)
    {
        // Describe
        D3D11_UNORDERED_ACCESS_VIEW_DESC unorderd_access_view_desc  = {};
        unorderd_access_view_desc.ViewDimension                     = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        unorderd_access_view_desc.Format                            = format;

        // Create
        return d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(texture), &unorderd_access_view_desc, reinterpret_cast<ID3D11UnorderedAccessView**>(&view)));
    }

    RHI_TextureCube::~RHI_TextureCube()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11ShaderResourceView**>(&m_resource_view));
        d3d11_utility::release(*reinterpret_cast<ID3D11UnorderedAccessView**>(&m_resource_view_unorderedAccess));
        d3d11_utility::release(*reinterpret_cast<ID3D11Texture2D**>(&m_resource));
        for (void*& render_target : m_resource_view_renderTarget)
        {
            d3d11_utility::release(*reinterpret_cast<ID3D11RenderTargetView**>(&render_target));
        }
        for (void*& depth_stencil : m_resource_view_depthStencil)
        {
            d3d11_utility::release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
        for (void*& depth_stencil : m_resource_view_depthStencilReadOnly)
        {
            d3d11_utility::release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
    }

    bool RHI_TextureCube::CreateResourceGpu()
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        bool result_tex = true;
        bool result_srv = true;
        bool result_uav = true;
        bool result_rt  = true;
        bool result_ds  = true;

        // Get texture flags
        const UINT flags = GetBindFlags(m_flags);

        // Resolve formats
        const DXGI_FORMAT format      = GetDepthFormat(m_format);
        const DXGI_FORMAT format_dsv  = GetDepthFormatDsv(m_format);
        const DXGI_FORMAT format_srv  = GetDepthFormatSrv(m_format);

        // TEXTURE
        result_tex = CreateTextureCube
        (
            m_resource,
            m_width,
            m_height,
            m_channel_count,
            m_bits_per_channel,
            m_array_size,
            format,
            flags,
            m_data_cube,
            m_rhi_device
        );

        // RESOURCE VIEW
        if (IsSampled())
        {
            result_srv = CreateShaderResourceViewCube(
                m_resource,
                m_resource_view[0],
                format_srv,
                m_array_size,
                m_data_cube,
                m_rhi_device
            );
        }

        // UNORDERED ACCESS VIEW
        if (IsStorage())
        {
            result_uav = CreateUnorderedAccessViewCube(m_resource, m_resource_view_unorderedAccess, format, m_rhi_device);
        }

        // DEPTH-STENCIL VIEW
        if (IsDepthStencil())
        {
            result_ds = CreateDepthStencilViewCube
            (
                m_resource,
                m_resource_view_depthStencil,
                m_array_size,
                format_dsv,
                false,
                m_rhi_device
            );

            if (m_flags & RHI_Texture_DepthStencilReadOnly)
            {
                result_ds = CreateDepthStencilViewCube
                (
                    m_resource,
                    m_resource_view_depthStencilReadOnly,
                    m_array_size,
                    format_dsv,
                    true,
                    m_rhi_device
                );
            }
        }

        // RENDER TARGET VIEW
        if (IsRenderTarget())
        {
            result_rt = CreateRenderTargetViewCube
            (
                m_resource,
                m_resource_view_renderTarget,
                format,
                m_array_size,
                m_rhi_device
            );
        }

        d3d11_utility::release(*reinterpret_cast<ID3D11Texture2D**>(&m_resource));

        return result_tex && result_srv && result_uav && result_rt && result_ds;
    }
}
