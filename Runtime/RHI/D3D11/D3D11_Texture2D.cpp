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

		// D3D11_TEXTURE2D_DESC
		D3D11_TEXTURE2D_DESC texture_desc;
		texture_desc.Width				= m_width;
		texture_desc.Height				= m_height;
		texture_desc.MipLevels			= generate_mipmaps ? 7 : static_cast<UINT>(m_data.size());
		texture_desc.ArraySize			= 1;
		texture_desc.Format				= d3d11_format[m_format];
		texture_desc.SampleDesc.Count	= 1;
		texture_desc.SampleDesc.Quality	= 0;
		texture_desc.Usage				= generate_mipmaps ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
		texture_desc.BindFlags			= generate_mipmaps ? D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET : D3D11_BIND_SHADER_RESOURCE; // D3D11_RESOURCE_MISC_GENERATE_MIPS flag requires D3D11_BIND_RENDER_TARGET
		texture_desc.MiscFlags			= generate_mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		texture_desc.CPUAccessFlags		= 0;

		// D3D11_SUBRESOURCE_DATA
		vector<D3D11_SUBRESOURCE_DATA> vec_subresource_data;
		auto mip_width	= m_width;
		auto mip_height	= m_height;

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
			mip_width	= Max(mip_width / 2, static_cast<unsigned int>(1));
			mip_height	= Max(mip_height / 2, static_cast<unsigned int>(1));

			// Compute memory usage (rough estimation)
			m_size += static_cast<unsigned int>(m_data[i].size()) * (m_bpc / 8);
		}

		// Describe shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc;
		shader_resource_desc.Format						= d3d11_format[m_format];
		shader_resource_desc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource_desc.Texture2D.MostDetailedMip	= 0;
		shader_resource_desc.Texture2D.MipLevels		= texture_desc.MipLevels;

		// Create texture
		ID3D11Texture2D* texture = nullptr;
		if (FAILED(m_rhi_device->GetContext()->device->CreateTexture2D(&texture_desc, generate_mipmaps ? nullptr : vec_subresource_data.data(), &texture)))
		{
			LOG_ERROR("Invalid parameters, failed to create ID3D11Texture2D.");
			return false;
		}

		// Create shader resource
		ID3D11ShaderResourceView* shader_resource_view = nullptr;
		if (SUCCEEDED(m_rhi_device->GetContext()->device->CreateShaderResourceView(texture, &shader_resource_desc, &shader_resource_view)))
		{
			if (generate_mipmaps)
			{
				m_rhi_device->GetContext()->device_context->UpdateSubresource(texture, 0, nullptr, m_data.front().data(), m_width * m_channels * (m_bpc / 8), 0);
				m_rhi_device->GetContext()->device_context->GenerateMips(shader_resource_view);
			}
		}
		else
		{
			LOG_ERROR("Failed to create the ID3D11ShaderResourceView.");
		}

		m_resource = shader_resource_view;
		safe_release(texture);
		return true;
	}
}
#endif