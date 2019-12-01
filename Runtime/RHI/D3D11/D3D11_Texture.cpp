/*
Copyright(c) 2016-2019 Panos Karabelas

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
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_resource_texture));
		m_resource_texture = nullptr;
	}

	inline bool CreateTexture(
		void*& texture,
		const uint32_t width,
		const uint32_t height,
		const uint32_t channels,
		const uint32_t bpc,
		const uint32_t array_size,
		const RHI_Format format,
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
		texture_desc.Format					= d3d11_format[format];
		texture_desc.SampleDesc.Count		= 1;
		texture_desc.SampleDesc.Quality		= 0;
		texture_desc.Usage					= (bind_flags & D3D11_BIND_RENDER_TARGET) || (bind_flags & D3D11_BIND_DEPTH_STENCIL) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
		texture_desc.BindFlags				= bind_flags;
		texture_desc.MiscFlags				= 0;
		texture_desc.CPUAccessFlags			= 0;

		// Fill subresource data
		vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
		auto mip_width	= width;
		auto mip_height = height;
		for (uint32_t i = 0; i < static_cast<uint32_t>(data.size()); i++)
		{
			if (data[i].empty())
			{
				LOG_ERROR("Mipmap %d has invalid data.", i);
				return false;
			}

			auto& subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
			subresource_data.pSysMem			= data[i].data();					// Data pointer		
			subresource_data.SysMemPitch		= mip_width * channels * (bpc / 8);	// Line width in bytes
			subresource_data.SysMemSlicePitch	= 0;								// This is only used for 3D textures

			// Compute size of next mip-map
			mip_width	= Max(mip_width / 2, static_cast<uint32_t>(1));
			mip_height	= Max(mip_height / 2, static_cast<uint32_t>(1));
		}

		// Create
		const auto ptr = reinterpret_cast<ID3D11Texture2D**>(&texture);
		const auto result = rhi_device->GetContextRhi()->device->CreateTexture2D(&texture_desc, vec_subresource_data.data(), ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Invalid parameters, failed to create ID3D11Texture2D, %s", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		return true;
	}

	inline bool CreateRenderTargetView(void* resource, void*& resource_render_target, const RHI_Format format, const unsigned array_size, const shared_ptr<RHI_Device>& rhi_device)
	{
		D3D11_RENDER_TARGET_VIEW_DESC view_desc		= {};
		view_desc.Format							= d3d11_format[format];
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

	inline bool CreateDepthStencilView(void* resource, vector<void*>& depth_stencil_views, const uint32_t array_size, const RHI_Format format, const shared_ptr<RHI_Device>& rhi_device)
	{
		// DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc	= {};
		dsv_desc.Format							= d3d11_format[format];
		dsv_desc.ViewDimension					= (array_size == 1) ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsv_desc.Texture2DArray.MipSlice		= 0;
		dsv_desc.Texture2DArray.ArraySize		= 1;
		dsv_desc.Texture2DArray.FirstArraySlice	= 0;

		for (uint32_t i = 0; i < array_size; i++)
		{
			dsv_desc.Texture2DArray.FirstArraySlice = i;
			const auto ptr = reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil_views.emplace_back(nullptr));
			const auto result = rhi_device->GetContextRhi()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(resource), &dsv_desc, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("CreateDepthStencilView() failed, %s.", D3D11_Common::dxgi_error_to_string(result));
				return false;
			}
		}

		return true;
	}

	inline bool CreateShaderResourceView(void* resource, void*& shader_resource_view, RHI_Format format, uint32_t array_size, std::vector<std::vector<std::byte>>& data, const shared_ptr<RHI_Device>& rhi_device)
	{
		// Describe
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc	= {};
		shader_resource_view_desc.Format							= d3d11_format[format];
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
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView, %s", D3D11_Common::dxgi_error_to_string(result));
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
	
		auto result_tex = true;
		auto result_srv = true;
		auto result_rt	= true;
		auto result_ds	= true;

		// Resolve bind flags
		UINT bind_flags = 0;
		{
			bind_flags |= (m_bind_flags & RHI_Texture_Sampled)		? D3D11_BIND_SHADER_RESOURCE	: 0;
			bind_flags |= (m_bind_flags & RHI_Texture_DepthStencil) ? D3D11_BIND_DEPTH_STENCIL		: 0;
			bind_flags |= (m_bind_flags & RHI_Texture_RenderTarget) ? D3D11_BIND_RENDER_TARGET		: 0;
		}

		// Resolve formats
		auto format		= m_format;
		auto format_dsv	= m_format;
		auto format_srv	= m_format;
		if (m_format == RHI_Format_D32_Float)
		{
			format			= RHI_Format_R32_Float_Typeless;
			format_dsv		= RHI_Format_D32_Float;
			format_srv		= RHI_Format_R32_Float;
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

        // SHADER RESOURCE VIEW
        if (m_bind_flags & RHI_Texture_Sampled)
        {
            result_srv = CreateShaderResourceView(
                texture,
                m_resource_texture,
                format_srv,
                m_array_size,
                m_data,
                m_rhi_device
            );
        }

        // DEPTH-STENCIL VIEW
        if (m_bind_flags & RHI_Texture_DepthStencil)
        {
            result_ds = CreateDepthStencilView
            (
                texture,
                m_resource_depth_stencils,
                m_array_size,
                format_dsv,
                m_rhi_device
            );
        }

		// RENDER TARGET VIEW
		if (m_bind_flags & RHI_Texture_RenderTarget)
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

		safe_release(reinterpret_cast<ID3D11Texture2D*>(texture));	
		return result_tex && result_srv && result_rt && result_ds;
	}

	// TEXTURE CUBE

	RHI_TextureCube::~RHI_TextureCube()
	{
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_resource_texture));
		m_resource_texture = nullptr;
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

			auto mip_width	= width;
			auto mip_height = height;
			for (const auto& mip : side)
			{
				if (mip.empty())
				{
					LOG_ERROR("A mip-map contains invalid data.");
					continue;
				}

				auto row_bytes = mip_width * channels * (bpc / 8);

				// D3D11_SUBRESOURCE_DATA
				auto & subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
				subresource_data.pSysMem			= mip.data();	// Data pointer		
				subresource_data.SysMemPitch		= row_bytes;	// Line width in bytes
				subresource_data.SysMemSlicePitch	= 0;			// This is only used for 3D textures

				// Compute size of next mip-map
				mip_width	= Max(mip_width / 2, static_cast<uint32_t>(1));
				mip_height	= Max(mip_height / 2, static_cast<uint32_t>(1));
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
			LOG_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters, %s", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&resource_texture);
		result = rhi_device->GetContextRhi()->device->CreateShaderResourceView(texture, &shader_resource_desc, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView, %s", D3D11_Common::dxgi_error_to_string(result));
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
		RHI_Format format,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
        if (!rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

		auto format_buffer	= RHI_Format_R32_Float_Typeless;
		auto format_dsv		= RHI_Format_D32_Float;
		auto format_srv		= RHI_Format_R32_Float;

		if (format == RHI_Format_D32_Float)
		{
			format_buffer	= RHI_Format_R32_Float_Typeless;
			format_dsv		 = RHI_Format_D32_Float;
			format_srv		 = RHI_Format_R32_Float;
		}

		// TEX
		D3D11_TEXTURE2D_DESC depth_buffer_desc	= {};
		depth_buffer_desc.Width					= static_cast<UINT>(width);
		depth_buffer_desc.Height				= static_cast<UINT>(height);
		depth_buffer_desc.MipLevels				= 1;
		depth_buffer_desc.ArraySize				= array_size;
		depth_buffer_desc.Format				= d3d11_format[format_buffer];
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
			LOG_ERROR("Failed to create depth stencil texture, %s.", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc	= {};
		dsv_desc.Format							= d3d11_format[format_dsv];
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
				LOG_ERROR("CreateDepthStencilView() failed, %s.", D3D11_Common::dxgi_error_to_string(result));
				safe_release(depth_stencil_texture);
				return false;
			}
		}

		// SRV
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
			srv_desc.Format							= d3d11_format[format_srv];
			srv_desc.ViewDimension					= D3D11_SRV_DIMENSION_TEXTURECUBE;
			srv_desc.Texture2DArray.FirstArraySlice = 0;
			srv_desc.Texture2DArray.MostDetailedMip = 0;
			srv_desc.Texture2DArray.MipLevels		= 1;
			srv_desc.Texture2DArray.ArraySize		= array_size;

			auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&resource_texture);
			result = rhi_device->GetContextRhi()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(depth_stencil_texture), &srv_desc, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("CreateShaderResourceView() failed, %s.", D3D11_Common::dxgi_error_to_string(result));
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

		if (m_bind_flags & RHI_Texture_DepthStencil)
		{
			result = TextureCube_DepthStencil
			(
				m_resource_texture,
				m_resource_depth_stencils,
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
				m_resource_texture,
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
