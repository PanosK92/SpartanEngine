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
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../../Math/MathHelper.h"
//================================

//= NAMESPAECES =======================
using namespace std;
using namespace Spartan::Math::Helper;
//=====================================

namespace Spartan
{
	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<std::byte>>& mip_chain)
	{
		if (!m_rhi_device->GetContext()->device || mip_chain.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// D3D11_TEXTURE2D_DESC
		D3D11_TEXTURE2D_DESC texture_desc;
		texture_desc.Width				= width;
		texture_desc.Height				= height;
		texture_desc.MipLevels			= static_cast<UINT>(mip_chain.size());
		texture_desc.ArraySize			= 1;
		texture_desc.Format				= d3d11_format[format];
		texture_desc.SampleDesc.Count	= 1;
		texture_desc.SampleDesc.Quality	= 0;
		texture_desc.Usage				= D3D11_USAGE_IMMUTABLE;
		texture_desc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		texture_desc.MiscFlags			= 0;
		texture_desc.CPUAccessFlags		= 0;

		// D3D11_SUBRESOURCE_DATA
		vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
		auto mip_width	= width;
		auto mip_height	= height;

		for (unsigned int i = 0; i < static_cast<unsigned int>(mip_chain.size()); i++)
		{
			if (mip_chain[i].empty())
			{
				LOGF_ERROR("Mip level %d has invalid data.", i);
				continue;
			}

			auto row_bytes = mip_width * channels * (m_bpc / 8);

			auto& subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
			subresource_data.pSysMem			= mip_chain[i].data();	// Data pointer		
			subresource_data.SysMemPitch		= row_bytes;			// Line width in bytes
			subresource_data.SysMemSlicePitch	= 0;					// This is only used for 3D textures

			// Compute size of next mip-map
			mip_width	= Max(mip_width / 2, static_cast<unsigned int>(1));
			mip_height	= Max(mip_height / 2, static_cast<unsigned int>(1));

			// Compute memory usage (rough estimation)
			m_memory_usage += static_cast<unsigned int>(mip_chain[i].size()) * (m_bpc / 8);
		}

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc;
		shader_resource_desc.Format						= d3d11_format[format];
		shader_resource_desc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource_desc.Texture2D.MostDetailedMip	= 0;
		shader_resource_desc.Texture2D.MipLevels		= texture_desc.MipLevels;

		// Create texture
		ID3D11Texture2D* texture = nullptr;
		auto result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateTexture2D(&texture_desc, vec_subresource_data.data(), &texture));
		if (!result)
		{
			LOG_ERROR("Invalid parameters, failed to create ID3D11Texture2D.");
			return false;
		}

		// Create shader resource
		ID3D11ShaderResourceView* shader_resource_view = nullptr;
		result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateShaderResourceView(texture, &shader_resource_desc, &shader_resource_view));
		if (!result)
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_shader_resource = shader_resource_view;
		safe_release(texture);
		return result;
	}

	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<std::byte>& data, bool generate_mip_chain /*= false*/)
	{
		if (!m_rhi_device->GetContext()->device || data.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (generate_mip_chain)
		{
			if (width < 4 || height < 4)
			{
				LOGF_WARNING("Mipchain won't be generated as dimension %dx%d is too small", width, height);
				generate_mip_chain = false;
			}
		}

		// D3D11_TEXTURE2D_DESC
		D3D11_TEXTURE2D_DESC texture_desc;
		texture_desc.Width				= width;
		texture_desc.Height				= height;
		texture_desc.MipLevels			= generate_mip_chain ? 7 : 1;
		texture_desc.ArraySize			= 1;
		texture_desc.Format				= d3d11_format[format];
		texture_desc.SampleDesc.Count	= 1;
		texture_desc.SampleDesc.Quality	= 0;
		texture_desc.Usage				= D3D11_USAGE_IMMUTABLE;
		texture_desc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		texture_desc.MiscFlags			= 0;
		texture_desc.CPUAccessFlags		= 0;

		// Adjust flags in case we are generating the mip-maps
		if (generate_mip_chain)
		{
			texture_desc.Usage		= D3D11_USAGE_DEFAULT;
			texture_desc.BindFlags	|= D3D11_BIND_RENDER_TARGET; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
			texture_desc.MiscFlags	|= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}

		D3D11_SUBRESOURCE_DATA subresource_data;
		subresource_data.pSysMem			= data.data();						// Data pointer		
		subresource_data.SysMemPitch		= width * channels * (m_bpc / 8);	// Line width in bytes
		subresource_data.SysMemSlicePitch	= 0;								// This is only used for 3D textures

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc;
		shader_resource_desc.Format						= d3d11_format[format];
		shader_resource_desc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource_desc.Texture2D.MostDetailedMip	= 0;
		shader_resource_desc.Texture2D.MipLevels		= texture_desc.MipLevels;

		// Create texture
		ID3D11Texture2D* texture = nullptr;
		auto result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateTexture2D(&texture_desc, generate_mip_chain ? nullptr : &subresource_data, &texture));
		if (!result)
		{
			LOG_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// Create shader resource
		ID3D11ShaderResourceView* shader_resource_view = nullptr;
		result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateShaderResourceView(texture, &shader_resource_desc, &shader_resource_view));
		if (result)
		{
			// Generate mip-maps
			if (generate_mip_chain)
			{
				m_rhi_device->GetContext()->device_context->UpdateSubresource(texture, 0, nullptr, data.data(), width * channels * (m_bpc / 8), 0);
				m_rhi_device->GetContext()->device_context->GenerateMips(shader_resource_view);
			}
		}
		else
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_shader_resource = shader_resource_view;
		safe_release(texture);
		return result;
	}

	bool RHI_Texture::ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<vector<std::byte>>>& data)
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
		auto mip_levels									= static_cast<UINT>(data[0].size());

		for (const auto& side : data)
		{
			if (side.empty())
			{
				LOG_ERROR("A side contains invalid data.");
				continue;
			}

			// D3D11_TEXTURE2D_DESC	
			D3D11_TEXTURE2D_DESC texture_desc;
			texture_desc.Width				= width;
			texture_desc.Height				= height;
			texture_desc.MipLevels			= mip_levels;
			texture_desc.ArraySize			= 6;
			texture_desc.Format				= d3d11_format[format];
			texture_desc.SampleDesc.Count	= 1;
			texture_desc.SampleDesc.Quality	= 0;
			texture_desc.Usage				= D3D11_USAGE_IMMUTABLE;
			texture_desc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
			texture_desc.MiscFlags			= D3D11_RESOURCE_MISC_TEXTURECUBE;
			texture_desc.CPUAccessFlags		= 0;

			auto mip_width	= width;
			auto mip_height	= height;

			for (const auto& mip : side)
			{
				if (mip.empty())
				{
					LOG_ERROR("A mip-map contains invalid data.");
					continue;
				}

				auto row_bytes = mip_width * channels * (m_bpc / 8);

				// D3D11_SUBRESOURCE_DATA
				auto& subresource_data				= vec_subresource_data.emplace_back(D3D11_SUBRESOURCE_DATA{});
				subresource_data.pSysMem			= mip.data();	// Data pointer		
				subresource_data.SysMemPitch		= row_bytes;		// Line width in bytes
				subresource_data.SysMemSlicePitch	= 0;			// This is only used for 3D textures

				// Compute size of next mip-map
				mip_width	= Max(mip_width / 2, static_cast<unsigned int>(1));
				mip_height	= Max(mip_height / 2, static_cast<unsigned int>(1));

				// Compute memory usage (rough estimation)
				m_memory_usage += static_cast<unsigned int>(mip.size()) * (m_bpc / 8);
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
		if (!m_rhi_device->GetContext()->device)
		{
			LOG_ERROR("Invalid RHI device.");
			return false;
		}

		// Create the Texture Resource
		auto result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateTexture2D(vec_texture_desc.data(), vec_subresource_data.data(), &texture));
		if (!result)
		{
			LOG_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		result = SUCCEEDED(m_rhi_device->GetContext()->device->CreateShaderResourceView(texture, &shader_resource_desc, &shader_resource_view));
		if (!result)
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_shader_resource = shader_resource_view;
		safe_release(texture);
		return result;
	}

	void RHI_Texture::ShaderResource_Release() const
	{
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_shader_resource));
	}
}
#endif