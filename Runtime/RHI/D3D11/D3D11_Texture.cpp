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
	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<std::byte>>& mipChain)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>() || mipChain.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// D3D11_TEXTURE2D_DESC
		D3D11_TEXTURE2D_DESC textureDesc;
		textureDesc.Width				= width;
		textureDesc.Height				= height;
		textureDesc.MipLevels			= (UINT)mipChain.size();
		textureDesc.ArraySize			= 1;
		textureDesc.Format				= d3d11_dxgi_format[format];
		textureDesc.SampleDesc.Count	= 1;
		textureDesc.SampleDesc.Quality	= 0;
		textureDesc.Usage				= D3D11_USAGE_IMMUTABLE;
		textureDesc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		textureDesc.MiscFlags			= 0;
		textureDesc.CPUAccessFlags		= 0;

		// D3D11_SUBRESOURCE_DATA
		vector<D3D11_SUBRESOURCE_DATA> vec_subresourceData;	
		unsigned int mipWidth	= width;
		unsigned int mipHeight	= height;

		for (unsigned int i = 0; i < (unsigned int)mipChain.size(); i++)
		{
			if (mipChain[i].empty())
			{
				LOGF_ERROR("Mip level %d has invalid data.", i);
				continue;
			}

			UINT rowBytes = mipWidth * channels * (m_bpc / 8);

			D3D11_SUBRESOURCE_DATA& subresourceData = vec_subresourceData.emplace_back(D3D11_SUBRESOURCE_DATA{});
			subresourceData.pSysMem				= mipChain[i].data();	// Data pointer		
			subresourceData.SysMemPitch			= rowBytes;				// Line width in bytes
			subresourceData.SysMemSlicePitch	= 0;					// This is only used for 3D textures

			// Compute size of next mip-map
			mipWidth	= Max(mipWidth / 2, (unsigned int)1);
			mipHeight	= Max(mipHeight / 2, (unsigned int)1);

			// Compute memory usage (rough estimation)
			m_memoryUsage += (unsigned int)mipChain[i].size() * (m_bpc / 8);
		}

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc;
		shaderResourceDesc.Format						= d3d11_dxgi_format[format];
		shaderResourceDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shaderResourceDesc.Texture2D.MostDetailedMip	= 0;
		shaderResourceDesc.Texture2D.MipLevels			= textureDesc.MipLevels;

		// Create texture
		ID3D11Texture2D* texture = nullptr;
		bool result = SUCCEEDED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&textureDesc, vec_subresourceData.data(), &texture));
		if (!result)
		{
			LOG_ERROR("Invalid parameters, failed to create ID3D11Texture2D.");
			return false;
		}

		// Create shader resource
		ID3D11ShaderResourceView* shaderResourceView = nullptr;
		result = SUCCEEDED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &shaderResourceDesc, &shaderResourceView));
		if (!result)
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_shaderResource = shaderResourceView;
		SafeRelease(texture);
		return result;
	}

	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<std::byte>& data, bool generateMipChain /*= false*/)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>() || data.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (generateMipChain)
		{
			if (width < 4 || height < 4)
			{
				LOGF_WARNING("Mipchain won't be generated as dimension %dx%d is too small", width, height);
				generateMipChain = false;
			}
		}

		// D3D11_TEXTURE2D_DESC
		D3D11_TEXTURE2D_DESC textureDesc;
		textureDesc.Width				= width;
		textureDesc.Height				= height;
		textureDesc.MipLevels			= generateMipChain ? 7 : 1;
		textureDesc.ArraySize			= 1;
		textureDesc.Format				= d3d11_dxgi_format[format];
		textureDesc.SampleDesc.Count	= 1;
		textureDesc.SampleDesc.Quality	= 0;
		textureDesc.Usage				= D3D11_USAGE_IMMUTABLE;
		textureDesc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		textureDesc.MiscFlags			= 0;
		textureDesc.CPUAccessFlags		= 0;

		// Adjust flags in case we are generating the mip-maps
		if (generateMipChain)
		{
			textureDesc.Usage		= D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags	|= D3D11_BIND_RENDER_TARGET; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
			textureDesc.MiscFlags	|= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}

		D3D11_SUBRESOURCE_DATA subresourceData;
		subresourceData.pSysMem				= data.data();						// Data pointer		
		subresourceData.SysMemPitch			= width * channels * (m_bpc / 8);	// Line width in bytes
		subresourceData.SysMemSlicePitch	= 0;								// This is only used for 3D textures

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc;
		shaderResourceDesc.Format						= d3d11_dxgi_format[format];
		shaderResourceDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shaderResourceDesc.Texture2D.MostDetailedMip	= 0;
		shaderResourceDesc.Texture2D.MipLevels			= textureDesc.MipLevels;

		// Create texture
		ID3D11Texture2D* texture = nullptr;
		bool result = SUCCEEDED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&textureDesc, generateMipChain ? nullptr : &subresourceData, &texture));
		if (!result)
		{
			LOG_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// Create shader resource
		ID3D11ShaderResourceView* shaderResourceView = nullptr;
		result = SUCCEEDED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &shaderResourceDesc, &shaderResourceView));
		if (result)
		{
			// Generate mip-maps
			if (generateMipChain)
			{
				m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->UpdateSubresource(texture, 0, nullptr, data.data(), width * channels * (m_bpc / 8), 0);
				m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->GenerateMips(shaderResourceView);
			}
		}
		else
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_shaderResource = shaderResourceView;
		SafeRelease(texture);
		return result;
	}

	bool RHI_Texture::ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<vector<std::byte>>>& data)
	{
		if (data.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		vector<D3D11_SUBRESOURCE_DATA> vec_subresourceData;
		vector<D3D11_TEXTURE2D_DESC> vec_textureDesc;
		ID3D11Texture2D* texture						= nullptr;
		ID3D11ShaderResourceView* shaderResourceView	= nullptr;
		UINT mipLevels									= (UINT)data[0].size();

		for (const auto& side : data)
		{
			if (side.empty())
			{
				LOG_ERROR("A side contains invalid data.");
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
					LOG_ERROR("A mip-map contains invalid data.");
					continue;
				}

				UINT rowBytes = mipWidth * channels * (m_bpc / 8);

				// D3D11_SUBRESOURCE_DATA
				D3D11_SUBRESOURCE_DATA& subresourceData = vec_subresourceData.emplace_back(D3D11_SUBRESOURCE_DATA{});
				subresourceData.pSysMem					= mip.data();	// Data pointer		
				subresourceData.SysMemPitch				= rowBytes;		// Line width in bytes
				subresourceData.SysMemSlicePitch		= 0;			// This is only used for 3D textures

				// Compute size of next mip-map
				mipWidth	= Max(mipWidth / 2, (unsigned int)1);
				mipHeight	= Max(mipHeight / 2, (unsigned int)1);

				// Compute memory usage (rough estimation)
				m_memoryUsage += (unsigned int)mip.size() * (m_bpc / 8);
			}

			vec_textureDesc.emplace_back(textureDesc);
		}

		// The Shader Resource view description
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc;
		shaderResourceDesc.Format						= d3d11_dxgi_format[format];
		shaderResourceDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURECUBE;
		shaderResourceDesc.TextureCube.MipLevels		= mipLevels;
		shaderResourceDesc.TextureCube.MostDetailedMip	= 0;

		// Validate device before usage
		if (!m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("Invalid RHI device.");
			return false;
		}

		// Create the Texture Resource
		bool result = SUCCEEDED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(vec_textureDesc.data(), vec_subresourceData.data(), &texture));
		if (!result)
		{
			LOG_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		result = SUCCEEDED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &shaderResourceDesc, &shaderResourceView));
		if (!result)
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_shaderResource = shaderResourceView;
		SafeRelease(texture);
		return result;
	}

	void RHI_Texture::ShaderResource_Release()
	{
		SafeRelease((ID3D11ShaderResourceView*)m_shaderResource);
	}
}