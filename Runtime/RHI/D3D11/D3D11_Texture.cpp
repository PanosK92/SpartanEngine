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

//= INCLUDES ========================
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../../Rendering/Renderer.h"
#include "../RHI_Texture.h"
//===================================

//= NAMESPAECES ====
using namespace std;
//==================

namespace Directus
{
	RHI_Texture::~RHI_Texture()
	{
		SafeRelease((ID3D11ShaderResourceView*)m_shaderResource);
		ClearTextureBytes();
	}

	bool RHI_Texture::CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const std::vector<std::byte>& data, bool generateMimaps /*= false*/)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("D3D11_Texture::CreateShaderResource: Invalid device.");
			return false;
		}

		if (data.empty())
		{
			LOG_ERROR("D3D11_Texture::CreateShaderResource: Invalid parameters.");
			return false;
		}

		unsigned int mipLevels = generateMimaps ? 7 : 1;

		// ID3D11Texture2D
		D3D11_TEXTURE2D_DESC textureDesc;
		ZeroMemory(&textureDesc, sizeof(textureDesc));
		textureDesc.Width				= width;
		textureDesc.Height				= height;
		textureDesc.MipLevels			= mipLevels;
		textureDesc.ArraySize			= (unsigned int)1;
		textureDesc.Format				= d3d11_dxgi_format[format];
		textureDesc.SampleDesc.Count	= (unsigned int)1;
		textureDesc.SampleDesc.Quality	= (unsigned int)0;
		textureDesc.Usage				= D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags			= D3D11_BIND_SHADER_RESOURCE;
		textureDesc.BindFlags			|= generateMimaps ? D3D11_BIND_RENDER_TARGET : 0; // Requirement by the D3D11_RESOURCE_MISC_GENERATE_MIPS flag
		textureDesc.MiscFlags			= generateMimaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		textureDesc.CPUAccessFlags		= 0;

		ID3D11Texture2D* texture = nullptr;

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource;
		shader_resource.Format						= textureDesc.Format;
		shader_resource.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource.Texture2D.MostDetailedMip	= 0;
		shader_resource.Texture2D.MipLevels			= mipLevels;

		// Describe sub resource
		D3D11_SUBRESOURCE_DATA sub_resource;
		ZeroMemory(&sub_resource, sizeof(sub_resource));
		sub_resource.pSysMem			= &data[0];
		sub_resource.SysMemPitch		= (width * channels) * sizeof(std::byte);
		sub_resource.SysMemSlicePitch	= (width * height * channels) * sizeof(std::byte);

		// Create texture
		{
			auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&textureDesc, generateMimaps ? nullptr : &sub_resource, &texture);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Texture::CreateShaderResource: Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
				return false;
			}
		}

		// Create shader resource
		{		
			auto ptr	= (ID3D11ShaderResourceView**)&m_shaderResource;
			auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &shader_resource, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Texture::CreateShaderResource: Failed to create the ID3D11ShaderResourceView.");
				return false;
			}

			if (generateMimaps)
			{
				// Copy data from memory to the sub-resource created in non-mappable memory
				m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->UpdateSubresource(texture, 0, nullptr, sub_resource.pSysMem, sub_resource.SysMemPitch, 0);

				// Create mipmaps based on ID3D11ShaderResourveView
				m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->GenerateMips((ID3D11ShaderResourceView*)m_shaderResource);
			}
		}

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(std::byte) * data.size());

		return true;
	}

	bool RHI_Texture::CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, Texture_Format format, const vector<vector<std::byte>>& data)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("D3D11_Texture::CreateFromMipmaps: Invalid device.");
			return false;
		}

		auto mipLevels = (unsigned int)data.size();

		vector<D3D11_SUBRESOURCE_DATA> subresourceData;
		vector<D3D11_TEXTURE2D_DESC> textureDescs;
		for (unsigned int i = 0; i < mipLevels; i++)
		{
			if (data[i].empty())
			{
				LOG_ERROR("D3D11_Texture::CreateShaderResource: Aborting creation of ID3D11Texture2D. Provided bits for mip level \"" + to_string(i) + "\" are empty.");
				return false;
			}

			// SUBRESROUCE DATA
			subresourceData.push_back(D3D11_SUBRESOURCE_DATA{});
			subresourceData.back().pSysMem			= &data[i][0];
			subresourceData.back().SysMemPitch		= (width * channels) * sizeof(std::byte);
			subresourceData.back().SysMemSlicePitch = (width * height * channels) * sizeof(std::byte);

			// ID3D11Texture2D
			textureDescs.push_back(D3D11_TEXTURE2D_DESC{});
			textureDescs.back().Width				= width;
			textureDescs.back().Height				= height;
			textureDescs.back().MipLevels			= mipLevels;
			textureDescs.back().ArraySize			= (unsigned int)1;
			textureDescs.back().Format				= d3d11_dxgi_format[format];
			textureDescs.back().SampleDesc.Count	= (unsigned int)1;
			textureDescs.back().SampleDesc.Quality	= (unsigned int)0;
			textureDescs.back().Usage				= D3D11_USAGE_IMMUTABLE;
			textureDescs.back().BindFlags			= D3D11_BIND_SHADER_RESOURCE;
			textureDescs.back().MiscFlags			= 0;
			textureDescs.back().CPUAccessFlags		= 0;

			width = max(width / 2, 1);
			height = max(height / 2, 1);

			// Compute memory usage
			m_memoryUsage += (unsigned int)(sizeof(std::byte) * data[i].size());
		}
		ID3D11Texture2D* texture = nullptr;
		HRESULT result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(textureDescs.data(), subresourceData.data(), &texture);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Texture::CreateShaderResource: Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters.");
			return false;
		}

		// SHADER RESOURCE VIEW
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format						= d3d11_dxgi_format[format];
		srvDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip	= 0;
		srvDesc.Texture2D.MipLevels			= mipLevels;

		auto ptr = (ID3D11ShaderResourceView**)&m_shaderResource;
		result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView(texture, &srvDesc, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Texture::CreateShaderResource: Failed to create the ID3D11ShaderResourceView.");
			return false;
		}

		return true;
	}
}