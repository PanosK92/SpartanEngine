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
#include "../RHI_CommandList.h"
#include "../../Math/MathHelper.h"
#include "../../Math/Vector4.h"
#include "../../Core/Settings.h"
//================================

//= NAMESPAECES ==============
using namespace std;
using namespace Spartan::Math;
using namespace Helper;
//============================

namespace Spartan
{
	RHI_Texture2D::~RHI_Texture2D()
	{
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_resource_texture));
		m_resource_texture = nullptr;
	}

	inline bool CreateTexture(
		void*& texture,
		unsigned int width,
		unsigned int height,
		unsigned int channels,
		unsigned int bpc,
		unsigned int mip_levels,
		unsigned int array_size,
		RHI_Format format,
		std::vector<std::vector<std::byte>>& data,
		bool generate_mipmaps,
		bool is_render_target,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
		D3D11_TEXTURE2D_DESC texture_desc	= {};
		texture_desc.Width					= static_cast<UINT>(width);
		texture_desc.Height					= static_cast<UINT>(height);
		texture_desc.MipLevels				= static_cast<UINT>(mip_levels);
		texture_desc.ArraySize				= static_cast<UINT>(array_size);
		texture_desc.Format					= d3d11_format[format];
		texture_desc.SampleDesc.Count		= 1;
		texture_desc.SampleDesc.Quality		= 0;
		texture_desc.Usage					= is_render_target ? D3D11_USAGE_DEFAULT : (generate_mipmaps ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE);
		texture_desc.BindFlags				= D3D11_BIND_SHADER_RESOURCE;
		texture_desc.BindFlags				|= generate_mipmaps ? D3D11_BIND_RENDER_TARGET : 0; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
		texture_desc.BindFlags				|= is_render_target ? D3D11_BIND_RENDER_TARGET : 0;
		texture_desc.MiscFlags				= generate_mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		texture_desc.CPUAccessFlags			= 0;

		// Fill subresource data
		vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
		auto mip_width	= width;
		auto mip_height = height;
		for (unsigned int i = 0; i < static_cast<unsigned int>(data.size()); i++)
		{
			if (data[i].empty())
			{
				LOGF_ERROR("Mipmap %d has invalid data.", i);
				return false;
			}

			auto& subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
			subresource_data.pSysMem			= data[i].data();					// Data pointer		
			subresource_data.SysMemPitch		= mip_width * channels * (bpc / 8);	// Line width in bytes
			subresource_data.SysMemSlicePitch	= 0;								// This is only used for 3D textures

			// Compute size of next mip-map
			mip_width	= Max(mip_width / 2, static_cast<unsigned int>(1));
			mip_height	= Max(mip_height / 2, static_cast<unsigned int>(1));
		}

		// Create
		auto ptr = reinterpret_cast<ID3D11Texture2D**>(&texture);
		auto result = rhi_device->GetContext()->device->CreateTexture2D(&texture_desc, generate_mipmaps ? nullptr : vec_subresource_data.data(), ptr);
		if (FAILED(result))
		{
			LOGF_ERROR("Invalid parameters, failed to create ID3D11Texture2D, %s", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		return true;
	}

	inline bool CreateRenderTargetView(void* resource, void*& resource_render_target, RHI_Format format, unsigned array_size, const shared_ptr<RHI_Device>& rhi_device)
	{
		D3D11_RENDER_TARGET_VIEW_DESC view_desc		= {};
		view_desc.Format							= d3d11_format[format];
		view_desc.ViewDimension						= (array_size == 1) ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		view_desc.Texture2DArray.MipSlice			= 0;
		view_desc.Texture2DArray.ArraySize			= array_size;
		view_desc.Texture2DArray.FirstArraySlice	= 0;

		auto ptr = reinterpret_cast<ID3D11RenderTargetView**>(&resource_render_target);
		if (FAILED(rhi_device->GetContext()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(resource), &view_desc, ptr)))
		{
			LOG_ERROR("CreateRenderTargetView() failed.");
			return false;
		}

		return true;
	}

	inline bool CreateDepthStencilView(
		void*& shader_resouce_view,
		vector<void*>& depth_stencil_views,
		unsigned int width,
		unsigned int height,
		unsigned int array_size,
		RHI_Format format, 
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
		RHI_Format format_buffer	= Format_R32_FLOAT_TYPELESS;
		RHI_Format format_dsv		= Format_D32_FLOAT;
		RHI_Format format_srv		= Format_R32_FLOAT;

		if (format == Format_D32_FLOAT)
		{
			format_buffer	= Format_R32_FLOAT_TYPELESS;
			format_dsv		= Format_D32_FLOAT;
			format_srv		= Format_R32_FLOAT;
		}

		// TEX
		D3D11_TEXTURE2D_DESC depth_buffer_desc	= {};
		depth_buffer_desc.Width					= static_cast<UINT>(width);
		depth_buffer_desc.Height				= static_cast<UINT>(height);
		depth_buffer_desc.MipLevels				= 1;
		depth_buffer_desc.ArraySize				= static_cast<UINT>(array_size);
		depth_buffer_desc.Format				= d3d11_format[format_buffer];
		depth_buffer_desc.SampleDesc.Count		= 1;
		depth_buffer_desc.SampleDesc.Quality	= 0;
		depth_buffer_desc.Usage					= D3D11_USAGE_DEFAULT;
		depth_buffer_desc.BindFlags				= D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		depth_buffer_desc.CPUAccessFlags		= 0;
		depth_buffer_desc.MiscFlags				= 0;

		ID3D11Texture2D* depth_stencil_texture = nullptr;
		auto result = rhi_device->GetContext()->device->CreateTexture2D(&depth_buffer_desc, nullptr, &depth_stencil_texture);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create depth stencil texture, %s.", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc	= {};
		dsv_desc.Format							= d3d11_format[format_dsv];
		dsv_desc.ViewDimension					= (array_size == 1) ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsv_desc.Texture2DArray.MipSlice		= 0;
		dsv_desc.Texture2DArray.ArraySize		= 1;
		dsv_desc.Texture2DArray.FirstArraySlice	= 0;

		for (unsigned int i = 0; i < array_size; i++)
		{
			dsv_desc.Texture2DArray.FirstArraySlice = i;
			auto ptr = reinterpret_cast<ID3D11DepthStencilView**>(&depth_stencil_views.emplace_back(nullptr));
			result = rhi_device->GetContext()->device->CreateDepthStencilView(static_cast<ID3D11Resource*>(depth_stencil_texture), &dsv_desc, ptr);
			if (FAILED(result))
			{
				LOGF_ERROR("CreateDepthStencilView() failed, %s.", D3D11_Common::dxgi_error_to_string(result));
				safe_release(depth_stencil_texture);
				return false;
			}
		}

		// SRV
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
			srv_desc.Format							= d3d11_format[format_srv];
			srv_desc.ViewDimension					= (array_size == 1) ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srv_desc.Texture2DArray.FirstArraySlice = 0;
			srv_desc.Texture2DArray.MostDetailedMip = 0;
			srv_desc.Texture2DArray.MipLevels		= 1;
			srv_desc.Texture2DArray.ArraySize		= array_size;

			auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&shader_resouce_view);
			auto result = rhi_device->GetContext()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(depth_stencil_texture), &srv_desc, ptr);
			if (FAILED(result))
			{
				LOGF_ERROR("CreateShaderResourceView() failed, %s.", D3D11_Common::dxgi_error_to_string(result));
				safe_release(depth_stencil_texture);
				return false;
			}
		}

		safe_release(depth_stencil_texture);
		return true;
	}

	inline bool CreateShaderResourceView(
		void* resource,
		void*& shader_resource_view,
		RHI_Format format,
		unsigned int width,
		unsigned int channels,
		unsigned int bpc,
		unsigned int array_size,
		unsigned int mip_levels,
		std::vector<std::vector<std::byte>>& data,
		bool generate_mipmaps,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
		// Describe
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc	= {};
		shader_resource_view_desc.Format							= d3d11_format[format];
		shader_resource_view_desc.ViewDimension						= (array_size == 1) ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		shader_resource_view_desc.Texture2DArray.FirstArraySlice	= 0;
		shader_resource_view_desc.Texture2DArray.MostDetailedMip	= 0;
		shader_resource_view_desc.Texture2DArray.MipLevels			= mip_levels;
		shader_resource_view_desc.Texture2DArray.ArraySize			= array_size;

		// Create
		auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&shader_resource_view);
		auto result = rhi_device->GetContext()->device->CreateShaderResourceView(static_cast<ID3D11Resource*>(resource), &shader_resource_view_desc, ptr);
		if (SUCCEEDED(result))
		{
			if (generate_mipmaps)
			{
				rhi_device->GetContext()->device_context->UpdateSubresource(static_cast<ID3D11Resource*>(resource), 0, nullptr, data.front().data(), width * channels * (bpc / 8), 0);
				rhi_device->GetContext()->device_context->GenerateMips(*ptr);
			}
		}
		else
		{
			LOGF_ERROR("Failed to create the ID3D11ShaderResourceView, %s", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		return true;
	}

	bool RHI_Texture2D::CreateResourceGpu()
	{
		if (!m_rhi_device->GetContext()->device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Determine if this is a regular, render target and/or depth stencil texture
		bool is_depth_stencil	= false;
		bool is_render_target	= false;
		if (m_is_render_texture)
		{
			is_depth_stencil = (m_format == Format_D32_FLOAT);
			is_render_target = !is_depth_stencil;
		}

		bool result_tex = true;
		bool result_srv = true;
		bool result_rt	= true;

		if (is_depth_stencil)
		{
			result_rt = CreateDepthStencilView
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
		else // regular and/or render target texture
		{
			// Regular texture: needs data to be initialized from
			if (!m_is_render_texture && m_data.empty())
			{
				LOG_ERROR_INVALID_PARAMETER();
				return false;
			}

			// Regular texture: deduce mipmap requirements
			UINT mip_levels = 1;
			bool generate_mipmaps = false;
			if (!m_is_render_texture)
			{
				bool generate_mipmaps = m_has_mipmaps && (m_data.size() == 1);
				if (generate_mipmaps)
				{
					if (m_width < 4 || m_height < 4)
					{
						LOGF_WARNING("Mipmaps won't be generated as dimension %dx%d is too small", m_width, m_height);
						generate_mipmaps = false;
					}
				}
				mip_levels = generate_mipmaps ? 7 : static_cast<UINT>(m_data.size());
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
				mip_levels,
				m_array_size,
				m_format,
				m_data,
				generate_mipmaps,
				m_is_render_texture,
				m_rhi_device
			);

			// SHADER RESOURCE VIEW
			result_srv = CreateShaderResourceView(
				texture,
				m_resource_texture,
				m_format,
				m_width,
				m_channels,
				m_bpc,
				m_array_size,
				mip_levels,
				m_data,
				generate_mipmaps,
				m_rhi_device
			);

			// RENDER TARGET VIEW
			if (is_render_target)
			{
				result_rt = CreateRenderTargetView
				(
					texture,
					m_resource_render_target,
					m_format,
					m_array_size,
					m_rhi_device
				);
			}

			safe_release(static_cast<ID3D11Texture2D*>(texture));
		}
		
	
		return result_tex && result_srv && result_rt;
	}
}
#endif