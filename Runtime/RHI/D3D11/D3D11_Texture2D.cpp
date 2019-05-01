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
#include "../../Math/MathHelper.h"
//================================

//= NAMESPAECES ======================
using namespace std;
using namespace Spartan::Math::Helper;
//====================================

namespace Spartan
{
	RHI_Texture2D::~RHI_Texture2D()
	{
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_resource));
		m_resource = nullptr;
	}

	bool RHI_Texture2D::CreateResourceGpu()
	{
		if (!m_rhi_device->GetContext()->device || m_data.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Deduce mipmap generation requirement
		bool generate_mipmaps = m_has_mipmaps && (m_data.size() == 1);
		if (generate_mipmaps)
		{
			if (m_width < 4 || m_height < 4)
			{
				LOGF_WARNING("Mipmaps won't be generated as dimension %dx%d is too small", m_width, m_height);
				generate_mipmaps = false;
			}
		}
		UINT mip_levels = generate_mipmaps ? 7 : static_cast<UINT>(m_data.size());

		// TEXTURE
		ID3D11Texture2D* texture = nullptr;
		{
			D3D11_TEXTURE2D_DESC texture_desc;
			texture_desc.Width				= m_width;
			texture_desc.Height				= m_height;
			texture_desc.MipLevels			= mip_levels;
			texture_desc.ArraySize			= static_cast<UINT>(m_array_size);
			texture_desc.Format				= d3d11_format[m_format];
			texture_desc.SampleDesc.Count	= 1;
			texture_desc.SampleDesc.Quality = 0;
			texture_desc.Usage				= generate_mipmaps ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
			texture_desc.BindFlags			= generate_mipmaps ? D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET : D3D11_BIND_SHADER_RESOURCE; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
			texture_desc.MiscFlags			= generate_mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
			texture_desc.CPUAccessFlags		= 0;

			// Fill subresource data
			vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
			auto mip_width	= m_width;
			auto mip_height = m_height;
			for (unsigned int i = 0; i < static_cast<unsigned int>(m_data.size()); i++)
			{
				if (m_data[i].empty())
				{
					LOGF_ERROR("Mipmap %d has invalid data.", i);
					continue;
				}

				auto& subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
				subresource_data.pSysMem			= m_data[i].data();						// Data pointer		
				subresource_data.SysMemPitch		= mip_width * m_channels * (m_bpc / 8);	// Line width in bytes
				subresource_data.SysMemSlicePitch	= 0;									// This is only used for 3D textures

				// Compute size of next mip-map
				mip_width = Max(mip_width / 2, static_cast<unsigned int>(1));
				mip_height = Max(mip_height / 2, static_cast<unsigned int>(1));

				// Compute memory usage (rough estimation)
				m_size += static_cast<unsigned int>(m_data[i].size()) * (m_bpc / 8);
			}

			// Create
			if (FAILED(m_rhi_device->GetContext()->device->CreateTexture2D(&texture_desc, generate_mipmaps ? nullptr : vec_subresource_data.data(), &texture)))
			{
				LOG_ERROR("Invalid parameters, failed to create ID3D11Texture2D.");
				return false;
			}
		}

		// SHADER RESOURCE VIEW
		{
			// Describe
			D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc	= {};
			shader_resource_view_desc.Format							= d3d11_format[m_format];
			if (m_array_size == 1)
			{
				shader_resource_view_desc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
				shader_resource_view_desc.Texture2D.MostDetailedMip = 0;
				shader_resource_view_desc.Texture2D.MipLevels		= mip_levels;
			}
			else
			{
				shader_resource_view_desc.ViewDimension						= D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				shader_resource_view_desc.Texture2DArray.FirstArraySlice	= 0;
				shader_resource_view_desc.Texture2DArray.MostDetailedMip	= 0;
				shader_resource_view_desc.Texture2DArray.MipLevels			= mip_levels;
				shader_resource_view_desc.Texture2DArray.ArraySize			= m_array_size;
			}

			// Create
			auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&m_resource);
			if (SUCCEEDED(m_rhi_device->GetContext()->device->CreateShaderResourceView(texture, &shader_resource_view_desc, ptr)))
			{
				if (generate_mipmaps)
				{
					m_rhi_device->GetContext()->device_context->UpdateSubresource(texture, 0, nullptr, m_data.front().data(), m_width * m_channels * (m_bpc / 8), 0);
					m_rhi_device->GetContext()->device_context->GenerateMips(*ptr);
				}
			}
			else
			{
				safe_release(texture);
				LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
				return false;
			}

			// RENDER TARGET VIEW
			/*if (m_is_render_target)
			{
				D3D11_RENDER_TARGET_VIEW_DESC view_desc = {};
				view_desc.Format = d3d11_format[m_format];
				if (m_array_size == 1)
				{
					view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
					view_desc.Texture2D.MipSlice = 0;

					auto ptr = reinterpret_cast<ID3D11RenderTargetView**>(&m_buffer_render_target_views.emplace_back(nullptr));
					if (FAILED(m_rhi_device->GetContext()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(m_render_target_view), &view_desc, ptr)))
					{
						LOG_ERROR("CreateRenderTargetView() failed.");
						return;
					}
				}
				else
				{
					for (unsigned int i = 0; i < m_array_size; i++)
					{
						view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
						view_desc.Texture2DArray.MipSlice = 0;
						view_desc.Texture2DArray.ArraySize = 1;
						view_desc.Texture2DArray.FirstArraySlice = i;

						m_buffer_render_target_views.emplace_back(nullptr);
						auto ptr = reinterpret_cast<ID3D11RenderTargetView**>(&m_buffer_render_target_views[i]);
						if (FAILED(m_rhi_device->GetContext()->device->CreateRenderTargetView(static_cast<ID3D11Resource*>(m_render_target_view), &view_desc, ptr)))
						{
							LOG_ERROR("CreateRenderTargetView() failed.");
							return;
						}
					}
				}
			}*/

			// DEPTH-STENCIL BUFFER
			//if (m_is_depth_stencil_buffer)
			//{
			//	// DEPTH STENCIL VIEW
			//	ID3D11Texture2D* depth_stencil_texture = nullptr;
			//	auto depth_stencil_view = reinterpret_cast<ID3D11DepthStencilView**>(&m_depth_stencil_view);

			//	// Depth-stencil buffer
			//	D3D11_TEXTURE2D_DESC depth_buffer_desc = {};
			//	depth_buffer_desc.Width = static_cast<UINT>(m_viewport.GetWidth());
			//	depth_buffer_desc.Height = static_cast<UINT>(m_viewport.GetHeight());
			//	depth_buffer_desc.MipLevels = 1;
			//	depth_buffer_desc.ArraySize = 1;
			//	depth_buffer_desc.Format = d3d11_format[depth_format];
			//	depth_buffer_desc.SampleDesc.Count = 1;
			//	depth_buffer_desc.SampleDesc.Quality = 0;
			//	depth_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
			//	depth_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			//	depth_buffer_desc.CPUAccessFlags = 0;
			//	depth_buffer_desc.MiscFlags = 0;

			//	auto result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateTexture2D(&depth_buffer_desc, nullptr, &depth_stencil_texture));
			//	if (!result)
			//	{
			//		LOGF_ERROR("Failed to create depth stencil buffer, %s.", D3D11_Common::dxgi_error_to_string(result));
			//		return;
			//	}

			//	// Depth-stencil view
			//	D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc;
			//	ZeroMemory(&depth_stencil_view_desc, sizeof(depth_stencil_view_desc));
			//	depth_stencil_view_desc.Format = d3d11_format[depth_format];
			//	depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			//	depth_stencil_view_desc.Texture2D.MipSlice = 0;

			//	result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateDepthStencilView(depth_stencil_texture, &depth_stencil_view_desc, depth_stencil_view));
			//	if (!result)
			//	{
			//		LOGF_ERROR("Failed to create depth stencil view, %s.", D3D11_Common::dxgi_error_to_string(result));
			//	}

			//	safe_release(depth_stencil_texture);
			//}
		}

		safe_release(texture);
		return true;
	}
}
#endif