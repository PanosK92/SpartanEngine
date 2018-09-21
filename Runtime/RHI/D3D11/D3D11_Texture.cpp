/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../../Math/MathHelper.h"
//================================

//= NAMESPAECES =======================
using namespace std;
using namespace Directus::Math::Helper;
//=====================================

namespace Directus
{
	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const vector<vector<std::byte>>& data, bool generateMimaps /*= false*/)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("RHI_Texture::ShaderResource_Create: Invalid device.");
			return false;
		}

		if (data.empty())
		{
			LOG_ERROR("RHI_Texture::ShaderResource_Create2D: Invalid data.");
			return false;
		}

		ID3D11Texture2D* texture						= nullptr;
		ID3D11ShaderResourceView* shaderResourceView	= nullptr;

		// D3D11_TEXTURE2D_DESC
		D3D11_TEXTURE2D_DESC textureDesc;
		textureDesc.Width				= width;
		textureDesc.Height				= height;
		textureDesc.MipLevels			= generateMimaps ? 7 : (UINT)data.size();
		textureDesc.ArraySize			= 1;
		textureDesc.Format				= d3d11_dxgi_format[format];
		textureDesc.SampleDesc.Count	= 1;
		textureDesc.SampleDesc.Quality	= 0;
		textureDesc.Usage				= D3D11_USAGE_IMMUTABLE;
		textureDesc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		textureDesc.MiscFlags			= 0;
		textureDesc.CPUAccessFlags		= 0;

		// Adjust flags in case we are generating the mip-maps now
		if (generateMimaps)
		{
			textureDesc.Usage		= D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags	|= D3D11_BIND_RENDER_TARGET; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
			textureDesc.MiscFlags	|= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}

		// D3D11_SUBRESOURCE_DATA
		vector<D3D11_SUBRESOURCE_DATA> vec_subresourceData;
		unsigned int mipWidth	= width;
		unsigned int mipHeight	= height;
		for (unsigned int i = 0; i < (generateMimaps ? 1 : (unsigned int)data.size()); i++)
		{
			if (data[i].empty())
			{
				LOGF_ERROR("RHI_Texture::ShaderResource_Create2D: Mip level %d has invalid data.", i);
				continue;
			}

			vec_subresourceData.emplace_back(D3D11_SUBRESOURCE_DATA{});
			vec_subresourceData.back().pSysMem			= data[i].data();
			vec_subresourceData.back().SysMemPitch		= (mipWidth * channels) * sizeof(std::byte);
			vec_subresourceData.back().SysMemSlicePitch = 0;

			// Compute size of next mip-map
			mipWidth	= Max(mipWidth / 2, (unsigned int)1);
			mipHeight	= Max(mipHeight / 2, (unsigned int)1);

			// Compute memory usage (rough estimation)
			m_memoryUsage += (unsigned int)(sizeof(std::byte) * data[i].size());
		}

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc;
		shaderResourceDesc.Format						= d3d11_dxgi_format[format];
		shaderResourceDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shaderResourceDesc.Texture2D.MostDetailedMip	= 0;
		shaderResourceDesc.Texture2D.MipLevels			= textureDesc.MipLevels;

		// Create texture
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&textureDesc, generateMimaps ? nullptr : vec_subresourceData.data(), &texture);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Texture::ShaderResource_Create2D: Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// Create shader resource
		result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &shaderResourceDesc, &shaderResourceView);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Texture::ShaderResource_Create2D: Failed to create the ID3D11ShaderResourceView.");
			return false;
		}

		// Generate mip-maps
		if (generateMimaps)
		{
			m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->UpdateSubresource(texture, 0, nullptr, data[0].data(), (width * channels) * sizeof(std::byte), 0);
			m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->GenerateMips(shaderResourceView);
		}

		m_shaderResource = shaderResourceView;
		return true;
	}

	bool RHI_Texture::ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const vector<vector<vector<std::byte>>>& data)
	{
		if (data.empty())
		{
			LOG_ERROR("RHI_Texture::ShaderResource_CreateCubemap: Invalid data.");
			return false;
		}

		vector<D3D11_SUBRESOURCE_DATA> vec_subresourceData;
		vector<D3D11_TEXTURE2D_DESC> vec_textureDesc;
		ID3D11Texture2D* cubeTexture					= nullptr;
		ID3D11ShaderResourceView* shaderResourceView	= nullptr;
		UINT mipLevels									= (UINT)data[0].size();

		for (const auto& side : data)
		{
			if (side.empty())
			{
				LOG_ERROR("RHI_Texture::ShaderResource_CreateCubemap: A side containts invalid data.");
				continue;
			}

			// D3D11_TEXTURE2D_DESC	
			D3D11_TEXTURE2D_DESC textureDesc;
			textureDesc.Width				= width;
			textureDesc.Height				= height;
			textureDesc.MipLevels			= mipLevels;
			textureDesc.ArraySize			= 6;
			textureDesc.Format				= d3d11_dxgi_format[format];
			textureDesc.SampleDesc.Count	= 1;
			textureDesc.SampleDesc.Quality	= 0;
			textureDesc.Usage				= D3D11_USAGE_IMMUTABLE;
			textureDesc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
			textureDesc.MiscFlags			= D3D11_RESOURCE_MISC_TEXTURECUBE;
			textureDesc.CPUAccessFlags		= 0;

			unsigned int mipWidth	= width;
			unsigned int mipHeight	= height;

			for (const auto& mip : side)
			{
				if (mip.empty())
				{
					LOG_ERROR("RHI_Texture::ShaderResource_CreateCubemap: A mip-map containts invalid data.");
					continue;
				}

				// D3D11_SUBRESOURCE_DATA
				vec_subresourceData.emplace_back(D3D11_SUBRESOURCE_DATA{});
				vec_subresourceData.back().pSysMem			= mip.data();									// Pointer to the pixel data			
				vec_subresourceData.back().SysMemPitch		= (mipWidth * channels) * sizeof(std::byte);	// Line width in bytes
				vec_subresourceData.back().SysMemSlicePitch = 0;											// This is only used for 3D textures.

				// Compute size of next mip-map
				mipWidth	= Max(mipWidth / 2, (unsigned int)1);
				mipHeight	= Max(mipHeight / 2, (unsigned int)1);

				// Compute memory usage (rough estimation)
				m_memoryUsage += (unsigned int)(sizeof(std::byte) * mip.size());
			}

			vec_textureDesc.emplace_back(textureDesc);
		}

		// The Shader Resource view description
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc;
		shaderResourceDesc.Format						= d3d11_dxgi_format[format];
		shaderResourceDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURECUBE;
		shaderResourceDesc.TextureCube.MipLevels		= mipLevels;
		shaderResourceDesc.TextureCube.MostDetailedMip	= 0;

		// Create the Texture Resource
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(vec_textureDesc.data(), vec_subresourceData.data(), &cubeTexture);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Texture::ShaderResource_CreateCubemap: Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(cubeTexture, &shaderResourceDesc, &shaderResourceView);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Texture::ShaderResource_CreateCubemap: Failed to create the ID3D11ShaderResourceView.");
			return false;
		}

		m_shaderResource = shaderResourceView;
		return true;
	}

	void RHI_Texture::ShaderResource_Release()
	{
		SafeRelease((ID3D11ShaderResourceView*)m_shaderResource);
	}
}