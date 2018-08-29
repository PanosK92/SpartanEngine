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
			LOG_ERROR("D3D11_Texture::ShaderResource_Create: Invalid device.");
			return false;
		}

		if (data.empty())
		{
			LOG_ERROR("D3D11_Texture::ShaderResource_Create: Invalid data.");
			return false;
		}

		unsigned int mipLevels			= generateMimaps ? 7 : (unsigned int)data.size();
		unsigned int descriptorCount	= generateMimaps ? 1 : (unsigned int)data.size();
		vector<D3D11_SUBRESOURCE_DATA> vec_subresourceData;
		vector<D3D11_TEXTURE2D_DESC> vec_textureDesc;

		ID3D11Texture2D* texture						= nullptr;
		ID3D11ShaderResourceView* shaderResourceView	= nullptr;

		for (unsigned int i = 0; i < descriptorCount; i++)
		{
			if (data[i].empty())
			{
				LOGF_ERROR("D3D11_Texture::ShaderResource_Create: Mip level %d has invalid data.", i);
				continue;
			}

			// D3D11_SUBRESOURCE_DATA
			vec_subresourceData.push_back(D3D11_SUBRESOURCE_DATA{});
			vec_subresourceData.back().pSysMem			= data[i].data();
			vec_subresourceData.back().SysMemPitch		= (width * channels) * sizeof(std::byte);
			vec_subresourceData.back().SysMemSlicePitch = (width * height * channels) * sizeof(std::byte);

			// D3D11_TEXTURE2D_DESC
			vec_textureDesc.push_back(D3D11_TEXTURE2D_DESC{});
			vec_textureDesc.back().Width				= width;
			vec_textureDesc.back().Height				= height;
			vec_textureDesc.back().MipLevels			= mipLevels;
			vec_textureDesc.back().ArraySize			= 1;
			vec_textureDesc.back().Format				= d3d11_dxgi_format[format];
			vec_textureDesc.back().SampleDesc.Count		= 1;
			vec_textureDesc.back().SampleDesc.Quality	= 0;
			vec_textureDesc.back().Usage				= D3D11_USAGE_IMMUTABLE;
			vec_textureDesc.back().BindFlags			= D3D11_BIND_SHADER_RESOURCE;
			vec_textureDesc.back().MiscFlags			= 0;
			vec_textureDesc.back().CPUAccessFlags		= 0;

			// Adjust flags in case we are generating the mip-maps now
			if (generateMimaps)
			{
				vec_textureDesc.back().Usage		= D3D11_USAGE_DEFAULT;
				vec_textureDesc.back().BindFlags	|= D3D11_BIND_RENDER_TARGET; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
				vec_textureDesc.back().MiscFlags	|= D3D11_RESOURCE_MISC_GENERATE_MIPS;
			}

			// Compute size of next mip-map
			width	= Max((int)width / 2, 1);
			height	= Max((int)height / 2, 1);

			// Compute memory usage (rough estimation)
			m_memoryUsage += (unsigned int)(sizeof(std::byte) * data[i].size());
		}

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource;
		shader_resource.Format						= d3d11_dxgi_format[format];
		shader_resource.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource.Texture2D.MostDetailedMip	= 0;
		shader_resource.Texture2D.MipLevels			= mipLevels;

		// Create texture
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(vec_textureDesc.data(), generateMimaps ? nullptr : vec_subresourceData.data(), &texture);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Texture::ShaderResource_Create: Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// Create shader resource
		result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &shader_resource, &shaderResourceView);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Texture::ShaderResource_Create: Failed to create the ID3D11ShaderResourceView.");
			return false;
		}

		// Generate mip-maps
		if (generateMimaps)
		{
			// Copy data from memory to the sub-resource created in non-mappable memory	
			m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->UpdateSubresource(texture, 0, nullptr, vec_subresourceData.front().pSysMem, vec_subresourceData.front().SysMemPitch, 0);

			// Create mip-maps based on ID3D11ShaderResourveView		
			m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->GenerateMips(shaderResourceView);
		}

		m_shaderResource = shaderResourceView;
		return true;
	}

	bool RHI_Texture::ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const std::vector<std::vector<std::byte>>& data)
	{
		ID3D11Texture2D* cubeTexture					= nullptr;
		ID3D11ShaderResourceView* shaderResourceView	= nullptr;

		// Description of each face
		D3D11_TEXTURE2D_DESC texDesc;
		texDesc.Width				= width;
		texDesc.Height				= height;
		texDesc.MipLevels			= 1;
		texDesc.ArraySize			= 6;
		texDesc.Format				= d3d11_dxgi_format[format];
		texDesc.CPUAccessFlags		= 0;
		texDesc.SampleDesc.Count	= 1;
		texDesc.SampleDesc.Quality	= 0;
		texDesc.Usage				= D3D11_USAGE_DEFAULT;
		texDesc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		texDesc.CPUAccessFlags		= 0;
		texDesc.MiscFlags			= D3D11_RESOURCE_MISC_TEXTURECUBE;

		// The Shader Resource view description
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc;
		shaderResourceDesc.Format						= texDesc.Format;
		shaderResourceDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURECUBE;
		shaderResourceDesc.TextureCube.MipLevels		= texDesc.MipLevels;
		shaderResourceDesc.TextureCube.MostDetailedMip	= 0;

		// Array to fill which we will use to point D3D at our loaded CPU images.
		D3D11_SUBRESOURCE_DATA pData[6];
		for (int cubeMapFaceIndex = 0; cubeMapFaceIndex < 6; cubeMapFaceIndex++)
		{
			// Pointer to the pixel data
			pData[cubeMapFaceIndex].pSysMem				= data[cubeMapFaceIndex].data();
			// Line width in bytes
			pData[cubeMapFaceIndex].SysMemPitch			= (width * channels) * sizeof(std::byte);
			// This is only used for 3d textures.
			pData[cubeMapFaceIndex].SysMemSlicePitch	= 0;
		}

		// Create the Texture Resource
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&texDesc, &pData[0], &cubeTexture);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Texture::ShaderResource_Create: Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(cubeTexture, &shaderResourceDesc, &shaderResourceView);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Texture::ShaderResource_Create: Failed to create the ID3D11ShaderResourceView.");
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