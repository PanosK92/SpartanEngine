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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES =====================
#include "../RHI_Texture2D.h"
#include "../RHI_TextureCube.h"
#include "../RHI_CommandList.h"
#include "../../Math/MathHelper.h"
#include "../../Math/Vector4.h"
#include "../../Core/Settings.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	// TEXTURE 2D

	RHI_Texture2D::~RHI_Texture2D()
	{
		safe_release(*reinterpret_cast<ID3D11ShaderResourceView**>(&m_resource_view));
        safe_release(*reinterpret_cast<ID3D11UnorderedAccessView**>(&m_resource_unordered_access_view));
        safe_release(*reinterpret_cast<ID3D11RenderTargetView**>(&m_resource_render_target));
        safe_release(*reinterpret_cast<ID3D11Texture2D**>(&m_resource_texture));
        for (void*& depth_stencil : m_resource_depth_stencil)
        {
            safe_release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
        for (void*& depth_stencil : m_resource_depth_stencil_read_only)
        {
            safe_release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
	}

	inline bool CreateTexture(
		void*& texture,
		const uint32_t width,
		const uint32_t height,
		const uint32_t channels,
		const uint32_t bpc,
		const uint32_t array_size,
		const DXGI_FORMAT format,
		const UINT bind_flags,
		std::vector<std::vector<std::byte>>& data,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
		D3D11_TEXTURE2D_DESC texture_desc	= {};
		texture_desc.Width					= static_cast<UINT>(width);
		texture_desc.Height					= static_cast<UINT>(height);
		texture_desc.MipLevels				= data.empty() ? 1 : static_cast<UINT>(data.size());
		texture_desc.ArraySize				= static_cast<UINT>(array_size);
		texture_desc.Format					= format;
		texture_desc.SampleDesc.Count		= 1;
		texture_desc.SampleDesc.Quality		= 0;
		texture_desc.Usage					= (bind_flags & D3D11_BIND_RENDER_TARGET) || (bind_flags & D3D11_BIND_DEPTH_STENCIL) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
		texture_desc.BindFlags				= bind_flags;
		texture_desc.MiscFlags				= 0;
		texture_desc.CPUAccessFlags			= 0;

		// Fill subresource data
		vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
		for (uint32_t mip_level = 0; mip_level < static_cast<uint32_t>(data.size()); mip_level++)
		{
			if (data[mip_level].empty())
			{
				LOG_ERROR("Mipmap %d has invalid data.", mip_level);
				return false;
			}

			auto& subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
			subresource_data.pSysMem			= data[mip_level].data();					    // Data pointer		
			subresource_data.SysMemPitch		= (width >> mip_level) * channels * (bpc / 8);	// Line width in bytes
			subresource_data.SysMemSlicePitch	= 0;								            // This is only used for 3D textures
		}

		// Create
		const auto ptr = reinterpret_cast<ID3D11Texture2D**>(&texture);
		const auto result = rhi_device->GetContextRhi()->device->CreateTexture2D(&texture_desc, vec_subresource_data.data(), ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Invalid parameters, failed to create ID3D11Texture2D, %s", d3d11_common::dxgi_error_to_string(result));
			return false;
		}

		return true;
	}

	inline bool CreateRenderTargetView(void* resource, void*& resource_render_target, const DXGI_FORMAT format, const unsigned array_size, const shared_ptr<RHI_Device>& rhi_device)
	{
		D3D11_RENDER_TARGET_VIEW_DESC view_desc		= {};
		view_desc.Format							= format;
		view_desc.ViewDimension						= (array_size == 1) ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		view_desc.Texture2DArray.MipSlice			= 0;
		view_desc.Texture2DArray.ArraySize			= array_size;
		view_desc.Texture2DArray.FirstArraySlice	= 0;

		const auto ptr = reinterpret_cast<ID3D11RenderTargetView**>(&resource_render_target);
		if (FAILED(rhi_device->GetContextRhi()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(resource), &view_desc, ptr)))
		{
			LOG_ERROR("CreateRenderTargetView() failed.");
			return false;
		}

		return true;
	}

	inline bool CreateDepthStencilView(void* resource, vector<void*>& depth_stencil_views, const uint32_t array_size, const DXGI_FORMAT format, bool read_only, const shared_ptr<RHI_Device>& rhi_device)
	{
		// DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc	= {};
		dsv_desc.Format							= format;
		dsv_desc.ViewDimension					= (array_size == 1) ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsv_desc.Texture2DArray.MipSlice		= 0;
		dsv_desc.Texture2DArray.ArraySize		= 1;
		dsv_desc.Texture2DArray.FirstArraySlice	= 0;
        dsv_desc.Flags                          = read_only ? D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL : 0;

		for (uint32_t i = 0; i < array_size; i++)
		{
			dsv_desc.Texture2DArray.FirstArraySlice = i;
			const auto ptr = reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil_views.emplace_back(nullptr));
			const auto result = rhi_device->GetContextRhi()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(resource), &dsv_desc, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("CreateDepthStencilView() failed, %s.", d3d11_common::dxgi_error_to_string(result));
				return false;
			}
		}

		return true;
	}

	inline bool CreateShaderResourceView(void* resource, void*& shader_resource_view, DXGI_FORMAT format, uint32_t array_size, std::vector<std::vector<std::byte>>& data, const shared_ptr<RHI_Device>& rhi_device)
	{
		// Describe
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc	= {};
		shader_resource_view_desc.Format							= format;
		shader_resource_view_desc.ViewDimension						= (array_size == 1) ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		shader_resource_view_desc.Texture2DArray.FirstArraySlice	= 0;
		shader_resource_view_desc.Texture2DArray.MostDetailedMip	= 0;
		shader_resource_view_desc.Texture2DArray.MipLevels			= data.empty() ? 1 : static_cast<UINT>(data.size());
		shader_resource_view_desc.Texture2DArray.ArraySize			= array_size;

		// Create
		auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&shader_resource_view);
		auto result = rhi_device->GetContextRhi()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(resource), &shader_resource_view_desc, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView, %s", d3d11_common::dxgi_error_to_string(result));
			return false;
		}

		return true;
	}

    inline bool CreateUnorderedAccessView(void* resource, void*& unorderd_accesss_view, DXGI_FORMAT format, const shared_ptr<RHI_Device>& rhi_device)
	{
		// Describe
        D3D11_UNORDERED_ACCESS_VIEW_DESC unorderd_access_view_desc	= {};
        unorderd_access_view_desc.ViewDimension                     = D3D11_UAV_DIMENSION_TEXTURE2D;
        unorderd_access_view_desc.Format                            = format;

		// Create
		auto ptr    = reinterpret_cast<ID3D11UnorderedAccessView**>(&unorderd_accesss_view);
		auto result = rhi_device->GetContextRhi()->device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(resource), &unorderd_access_view_desc, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the ID3D11UnorderedAccessView, %s", d3d11_common::dxgi_error_to_string(result));
			return false;
		}

		return true;
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
		bool result_rt	= true;
		bool result_ds	= true;

		// Resolve bind flags
		UINT bind_flags = 0;
		{
			bind_flags |= (m_bind_flags & RHI_Texture_Sampled)		                ? D3D11_BIND_SHADER_RESOURCE	: 0;
            bind_flags |= (m_bind_flags & RHI_Texture_RenderTarget_Compute)         ? D3D11_BIND_UNORDERED_ACCESS   : 0;
			bind_flags |= (m_bind_flags & RHI_Texture_RenderTarget_DepthStencil)    ? D3D11_BIND_DEPTH_STENCIL		: 0;
			bind_flags |= (m_bind_flags & RHI_Texture_RenderTarget_Color)           ? D3D11_BIND_RENDER_TARGET		: 0;
		}

		// Resolve formats
        DXGI_FORMAT format		= d3d11_format[m_format];
		DXGI_FORMAT format_dsv	= d3d11_format[m_format];
		DXGI_FORMAT format_srv	= d3d11_format[m_format];
        if (m_format == RHI_Format_D32_Float_S8X24_Uint)
        {
            format      = DXGI_FORMAT_R32G8X24_TYPELESS;
            format_srv  = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            format_dsv  = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        }
		else if (m_format == RHI_Format_D32_Float)
		{
			format		= DXGI_FORMAT_R32_TYPELESS;
            format_srv  = DXGI_FORMAT_R32_FLOAT;
			format_dsv	= DXGI_FORMAT_D32_FLOAT;
		}

		// TEXTURE
		void* texture = nullptr;
		result_tex = CreateTexture
		(
			texture,
			m_width,
			m_height,
			m_channels,
			m_bpc,
			m_array_size,
			format,
			bind_flags,
			m_data,
			m_rhi_device
		);

        // RESOURCE VIEW
        if (m_bind_flags & RHI_Texture_Sampled)
        {
            result_srv = CreateShaderResourceView(
                texture,
                m_resource_view,
                format_srv,
                m_array_size,
                m_data,
                m_rhi_device
            );
        }

        // UNORDERED ACCESS VIEW
        if (m_bind_flags & RHI_Texture_RenderTarget_Compute)
        {
            result_uav = CreateUnorderedAccessView(texture, m_resource_unordered_access_view, format, m_rhi_device);
        }

        // DEPTH-STENCIL VIEW
        if (m_bind_flags & RHI_Texture_RenderTarget_DepthStencil)
        {
            result_ds = CreateDepthStencilView
            (
                texture,
                m_resource_depth_stencil,
                m_array_size,
                format_dsv,
                false,
                m_rhi_device
            );

            if (m_bind_flags & RHI_Texture_ReadOnlyDepthStencil)
            {
                result_ds = CreateDepthStencilView
                (
                    texture,
                    m_resource_depth_stencil_read_only,
                    m_array_size,
                    format_dsv,
                    true,
                    m_rhi_device
                );
            }
        }

		// RENDER TARGET VIEW
		if (m_bind_flags & RHI_Texture_RenderTarget_Color)
		{
			result_rt = CreateRenderTargetView
			(
				texture,
				m_resource_render_target,
				format,
				m_array_size,
				m_rhi_device
			);
		}

		safe_release(*reinterpret_cast<ID3D11Texture2D**>(&texture));	
		return result_tex && result_srv && result_uav && result_rt && result_ds;
	}

	// TEXTURE CUBE

	RHI_TextureCube::~RHI_TextureCube()
	{
        safe_release(*reinterpret_cast<ID3D11ShaderResourceView**>(&m_resource_view));
        safe_release(*reinterpret_cast<ID3D11UnorderedAccessView**>(&m_resource_unordered_access_view));
        safe_release(*reinterpret_cast<ID3D11RenderTargetView**>(&m_resource_render_target));
        safe_release(*reinterpret_cast<ID3D11Texture2D**>(&m_resource_texture));
        for (void*& depth_stencil : m_resource_depth_stencil)
        {
            safe_release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
        for (void*& depth_stencil : m_resource_depth_stencil_read_only)
        {
            safe_release(*reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil));
        }
	}

	inline bool TextureCube_Sampled
	(
		void*& resource_texture,
		uint32_t width,
		uint32_t height,
		uint32_t channels,
		uint32_t array_size,
		uint32_t bpc,
		RHI_Format format,
		vector<vector<vector<std::byte>>>& data,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
		if (data.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
		vector<D3D11_TEXTURE2D_DESC> vec_texture_desc;
		ID3D11Texture2D* texture						= nullptr;
		ID3D11ShaderResourceView* shader_resource_view	= nullptr;
		auto mip_levels = static_cast<UINT>(data[0].size());

		for (const auto& side : data)
		{
			if (side.empty())
			{
				LOG_ERROR("A side contains invalid data.");
				continue;
			}

			// D3D11_TEXTURE2D_DESC	
			D3D11_TEXTURE2D_DESC texture_desc;
			texture_desc.Width				= static_cast<UINT>(width);
			texture_desc.Height				= static_cast<UINT>(height);
			texture_desc.MipLevels			= static_cast<UINT>(mip_levels);
			texture_desc.ArraySize			= array_size;
			texture_desc.Format				= d3d11_format[format];
			texture_desc.SampleDesc.Count	= 1;
			texture_desc.SampleDesc.Quality = 0;
			texture_desc.Usage				= D3D11_USAGE_IMMUTABLE;
			texture_desc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
			texture_desc.MiscFlags			= D3D11_RESOURCE_MISC_TEXTURECUBE;
			texture_desc.CPUAccessFlags		= 0;

			for (uint32_t mip_level = 0; mip_level < static_cast<uint32_t>(side.size()); mip_level++)
			{
                auto& mip = side[mip_level];

				if (mip.empty())
				{
					LOG_ERROR("A mip-map contains invalid data.");
					continue;
				}

				// D3D11_SUBRESOURCE_DATA
				auto & subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
				subresource_data.pSysMem			= mip.data();	                                // Data pointer		
				subresource_data.SysMemPitch		= (width >> mip_level) * channels * (bpc / 8);	// Line width in bytes
				subresource_data.SysMemSlicePitch	= 0;			                                // This is only used for 3D textures
			}

			vec_texture_desc.emplace_back(texture_desc);
		}

		// The Shader Resource view description
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc;
		shader_resource_desc.Format							= d3d11_format[format];
		shader_resource_desc.ViewDimension					= D3D11_SRV_DIMENSION_TEXTURECUBE;
		shader_resource_desc.TextureCube.MipLevels			= mip_levels;
		shader_resource_desc.TextureCube.MostDetailedMip	= 0;

		// Validate device before usage
		if (!rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR("Invalid RHI device.");
			return false;
		}

		// Create the Texture Resource
		auto result = rhi_device->GetContextRhi()->device->CreateTexture2D(vec_texture_desc.data(), vec_subresource_data.data(), &texture);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters, %s", d3d11_common::dxgi_error_to_string(result));
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&resource_texture);
		result = rhi_device->GetContextRhi()->device->CreateShaderResourceView(texture, &shader_resource_desc, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView, %s", d3d11_common::dxgi_error_to_string(result));
			return false;
		}

		safe_release(texture);
		return true;
	}

	inline bool TextureCube_DepthStencil
	(
		void*& resource_texture,
		vector<void*>& resource_depth_stencils,
		uint32_t width,
		uint32_t height,
		uint32_t array_size,
		RHI_Format _format,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
        if (!rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        // Resolve formats
        DXGI_FORMAT format      = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT format_dsv  = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT format_srv  = DXGI_FORMAT_UNKNOWN;
        if (_format == RHI_Format_D32_Float_S8X24_Uint)
        {
            format      = DXGI_FORMAT_R32G8X24_TYPELESS;
            format_srv  = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            format_dsv  = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        }
        else if (_format == RHI_Format_D32_Float)
        {
            format      = DXGI_FORMAT_R32_TYPELESS;
            format_srv  = DXGI_FORMAT_R32_FLOAT;
            format_dsv  = DXGI_FORMAT_D32_FLOAT;
        }

		// TEX
		D3D11_TEXTURE2D_DESC depth_buffer_desc	= {};
		depth_buffer_desc.Width					= static_cast<UINT>(width);
		depth_buffer_desc.Height				= static_cast<UINT>(height);
		depth_buffer_desc.MipLevels				= 1;
		depth_buffer_desc.ArraySize				= array_size;
		depth_buffer_desc.Format				= format;
		depth_buffer_desc.SampleDesc.Count		= 1;
		depth_buffer_desc.SampleDesc.Quality	= 0;
		depth_buffer_desc.Usage					= D3D11_USAGE_DEFAULT;
		depth_buffer_desc.BindFlags				= D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		depth_buffer_desc.MiscFlags				= D3D11_RESOURCE_MISC_TEXTURECUBE;
		depth_buffer_desc.CPUAccessFlags		= 0;

		ID3D11Texture2D* depth_stencil_texture = nullptr;
		auto result = rhi_device->GetContextRhi()->device->CreateTexture2D(&depth_buffer_desc, nullptr, &depth_stencil_texture);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create depth stencil texture, %s.", d3d11_common::dxgi_error_to_string(result));
			return false;
		}

		// DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc	= {};
		dsv_desc.Format							= format_dsv;
		dsv_desc.ViewDimension					= D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsv_desc.Texture2DArray.MipSlice		= 0;
		dsv_desc.Texture2DArray.ArraySize		= 1;
		dsv_desc.Texture2DArray.FirstArraySlice = 0;

		for (uint32_t i = 0; i < array_size; i++)
		{
			dsv_desc.Texture2DArray.FirstArraySlice = i;
			auto ptr = reinterpret_cast<ID3D11DepthStencilView**>(&resource_depth_stencils.emplace_back(nullptr));
			result = rhi_device->GetContextRhi()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(depth_stencil_texture), &dsv_desc, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("CreateDepthStencilView() failed, %s.", d3d11_common::dxgi_error_to_string(result));
				safe_release(depth_stencil_texture);
				return false;
			}
		}

		// SRV
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
			srv_desc.Format							= format_srv;
			srv_desc.ViewDimension					= D3D11_SRV_DIMENSION_TEXTURECUBE;
			srv_desc.Texture2DArray.FirstArraySlice = 0;
			srv_desc.Texture2DArray.MostDetailedMip = 0;
			srv_desc.Texture2DArray.MipLevels		= 1;
			srv_desc.Texture2DArray.ArraySize		= array_size;

			auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&resource_texture);
			result = rhi_device->GetContextRhi()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(depth_stencil_texture), &srv_desc, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("CreateShaderResourceView() failed, %s.", d3d11_common::dxgi_error_to_string(result));
				safe_release(depth_stencil_texture);
				return false;
			}
		}

		safe_release(depth_stencil_texture);
		return true;
	}

	bool RHI_TextureCube::CreateResourceGpu()
	{
		auto result = true;

		if (m_bind_flags & RHI_Texture_RenderTarget_DepthStencil)
		{
			result = TextureCube_DepthStencil
			(
				m_resource_view,
				m_resource_depth_stencil,
				m_width,
				m_height,
				m_array_size,
				m_format,
				m_rhi_device
			);
		}
		else
		{
			result = TextureCube_Sampled
			(
				m_resource_view,
				m_width,
				m_height,
				m_channels,
				m_array_size,
				m_bpc,
				m_format,
				m_data_cube,
				m_rhi_device
			);
		}

		return result;
	}
}
#endif
