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

//= INCLUDES ====================
#include "../RHI_RenderTexture.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "D3D11_Helper.h"
//===============================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	RHI_RenderTexture::RHI_RenderTexture(const shared_ptr<RHI_Device>& rhi_device, unsigned int width, unsigned int height, RHI_Format texture_format, bool depth, RHI_Format depth_format, unsigned int array_size)
	{
		m_rhi_device	= rhi_device;
		m_depth_enabled	= depth;
		m_format		= texture_format;
		m_viewport		= RHI_Viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
		m_width			= width;
		m_height		= height;
		m_array_size	= array_size;

		if (!m_rhi_device || !m_rhi_device->GetDevice<ID3D11Device>())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// RENDER TARGET TEXTURE
		{
			D3D11_TEXTURE2D_DESC texture_desc;
			ZeroMemory(&texture_desc, sizeof(texture_desc));
			texture_desc.Width				= static_cast<UINT>(m_viewport.GetWidth());
			texture_desc.Height				= static_cast<UINT>(m_viewport.GetHeight());
			texture_desc.MipLevels			= 1;
			texture_desc.ArraySize			= array_size;
			texture_desc.Format				= d3d11_dxgi_format[m_format];
			texture_desc.SampleDesc.Count	= 1;
			texture_desc.SampleDesc.Quality	= 0;
			texture_desc.Usage				= D3D11_USAGE_DEFAULT;
			texture_desc.BindFlags			= D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			texture_desc.CPUAccessFlags		= 0;
			texture_desc.MiscFlags			= 0;

			auto ptr = reinterpret_cast<ID3D11Texture2D**>(&m_render_target_texture);
			if (FAILED(m_rhi_device->GetDevice<ID3D11Device>()->CreateTexture2D(&texture_desc, nullptr, ptr)))
			{
				LOG_ERROR("CreateTexture2D() failed.");
				return;
			}
		}

		// RENDER TARGET VIEW
		{
			D3D11_RENDER_TARGET_VIEW_DESC view_desc;
			view_desc.Format	= d3d11_dxgi_format[m_format];
			if (array_size == 1)
			{
				view_desc.ViewDimension		= D3D11_RTV_DIMENSION_TEXTURE2D;
				view_desc.Texture2D.MipSlice = 0;

				auto ptr = reinterpret_cast<ID3D11RenderTargetView**>(&m_render_target_views.emplace_back(nullptr));
				if (FAILED(m_rhi_device->GetDevice<ID3D11Device>()->CreateRenderTargetView(static_cast<ID3D11Resource*>(m_render_target_texture), &view_desc, ptr)))
				{
					LOG_ERROR("CreateRenderTargetView() failed.");
					return;
				}
			}
			else
			{
				for (unsigned int i = 0; i < array_size; i++)
				{
					view_desc.ViewDimension					= D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					view_desc.Texture2DArray.MipSlice		= 0;
					view_desc.Texture2DArray.ArraySize		= 1;
					view_desc.Texture2DArray.FirstArraySlice = i;

					m_render_target_views.emplace_back(nullptr);
					auto ptr = reinterpret_cast<ID3D11RenderTargetView**>(&m_render_target_views[i]);
					if (FAILED(m_rhi_device->GetDevice<ID3D11Device>()->CreateRenderTargetView(static_cast<ID3D11Resource*>(m_render_target_texture), &view_desc, ptr)))
					{
						LOG_ERROR("CreateRenderTargetView() failed.");
						return;
					}
				}
			}		
		}

		// SHADER RESOURCE VIEW
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc;
			shader_resource_view_desc.Format = d3d11_dxgi_format[m_format];
			if (array_size == 1)
			{
				shader_resource_view_desc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
				shader_resource_view_desc.Texture2D.MostDetailedMip	= 0;
				shader_resource_view_desc.Texture2D.MipLevels		= 1;
			}
			else
			{
				shader_resource_view_desc.ViewDimension						= D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				shader_resource_view_desc.Texture2DArray.FirstArraySlice	= 0;
				shader_resource_view_desc.Texture2DArray.MostDetailedMip	= 0;
				shader_resource_view_desc.Texture2DArray.MipLevels			= 1;
				shader_resource_view_desc.Texture2DArray.ArraySize			= array_size;
			}
			
			auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&m_shader_resource_view);
			if (FAILED(m_rhi_device->GetDevice<ID3D11Device>()->CreateShaderResourceView(static_cast<ID3D11Texture2D*>(m_render_target_texture), &shader_resource_view_desc, ptr)))
			{
				safe_release(static_cast<ID3D11Texture2D*>(m_render_target_texture));
				LOG_ERROR("CreateShaderResourceView() failed.");
				return;
			}
			else
			{
				safe_release(static_cast<ID3D11Texture2D*>(m_render_target_texture));
			}
		}

		// DEPTH-STENCIL BUFFER
		if (m_depth_enabled)
		{
			// DEPTH STENCIL VIEW
			auto depth_stencil_texture	= reinterpret_cast<ID3D11Texture2D**>(&m_depth_stencil_texture);
			auto depth_stencil_view		= reinterpret_cast<ID3D11DepthStencilView**>(&m_depth_stencil_view);

			// Depth-stencil buffer
			D3D11_TEXTURE2D_DESC depth_buffer_desc;
			ZeroMemory(&depth_buffer_desc, sizeof(depth_buffer_desc));
			depth_buffer_desc.Width					= static_cast<UINT>(m_viewport.GetWidth());
			depth_buffer_desc.Height				= static_cast<UINT>(m_viewport.GetHeight());
			depth_buffer_desc.MipLevels				= 1;
			depth_buffer_desc.ArraySize				= 1;
			depth_buffer_desc.Format				= d3d11_dxgi_format[depth_format];
			depth_buffer_desc.SampleDesc.Count		= 1;
			depth_buffer_desc.SampleDesc.Quality	= 0;
			depth_buffer_desc.Usage					= D3D11_USAGE_DEFAULT;
			depth_buffer_desc.BindFlags				= D3D11_BIND_DEPTH_STENCIL;
			depth_buffer_desc.CPUAccessFlags		= 0;
			depth_buffer_desc.MiscFlags				= 0;

			auto result = SUCCEEDED(m_rhi_device->GetDevice<ID3D11Device>()->CreateTexture2D(&depth_buffer_desc, nullptr, depth_stencil_texture));
			if (!result)
			{
				LOGF_ERROR("Failed to create depth stencil buffer, %s.", D3D11_Helper::dxgi_error_to_string(result));
				return;
			}

			// Depth-stencil view
			D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc;
			ZeroMemory(&depth_stencil_view_desc, sizeof(depth_stencil_view_desc));
			depth_stencil_view_desc.Format				= d3d11_dxgi_format[depth_format];
			depth_stencil_view_desc.ViewDimension		= D3D11_DSV_DIMENSION_TEXTURE2D;
			depth_stencil_view_desc.Texture2D.MipSlice	= 0;

			result = SUCCEEDED(m_rhi_device->GetDevice<ID3D11Device>()->CreateDepthStencilView(*depth_stencil_texture, &depth_stencil_view_desc, depth_stencil_view));
			if (!result)
			{
				safe_release(static_cast<ID3D11Texture2D*>(m_depth_stencil_texture));
				LOGF_ERROR("Failed to create depth stencil view, %s.", D3D11_Helper::dxgi_error_to_string(result));
				return;
			}
			else
			{
				safe_release(static_cast<ID3D11Texture2D*>(m_depth_stencil_texture));
			}
		}
	}

	RHI_RenderTexture::~RHI_RenderTexture()
	{
		for (auto& render_target_view : m_render_target_views)
		{
			safe_release(static_cast<ID3D11RenderTargetView*>(render_target_view));
		}
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_shader_resource_view));
		safe_release(static_cast<ID3D11DepthStencilView*>(m_depth_stencil_view));
	}

	bool RHI_RenderTexture::Clear(const Vector4& clear_color)
	{
		if (!m_rhi_device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Clear back buffer
		for (auto& render_target_view : m_render_target_views)
		{ 
			m_rhi_device->ClearRenderTarget(render_target_view, clear_color); 
		}

		// Clear depth buffer
		if (m_depth_enabled)
		{
			if (!m_depth_stencil_view)
			{
				LOG_ERROR_INVALID_INTERNALS();
				return false;
			}

			const auto depth = Settings::Get().GetReverseZ() ? 1.0f - m_viewport.GetMaxDepth() : m_viewport.GetMaxDepth();
			m_rhi_device->ClearDepthStencil(m_depth_stencil_view, Clear_Depth, depth, 0);
		}

		return true;
	}

	bool RHI_RenderTexture::Clear(const float red, const float green, const float blue, const float alpha)
	{
		return Clear(Vector4(red, green, blue, alpha));
	}
}
#endif