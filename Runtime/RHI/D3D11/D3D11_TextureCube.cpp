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
#include "../RHI_TextureCube.h"
#include "../../Math/MathHelper.h"
//================================

//= NAMESPAECES ======================
using namespace std;
using namespace Spartan::Math::Helper;
//====================================

namespace Spartan
{
	RHI_TextureCube::~RHI_TextureCube()
	{
		safe_release(static_cast<ID3D11ShaderResourceView*>(m_resource_texture));
		m_resource_texture = nullptr;
	}

	inline bool CreateFromData
	(
		void*& resource_texture,
		unsigned int width,
		unsigned int height,
		unsigned int channels,
		unsigned int array_size,
		unsigned int bpc,
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
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* shader_resource_view = nullptr;
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
			texture_desc.Width					= static_cast<UINT>(width);
			texture_desc.Height					= static_cast<UINT>(height);
			texture_desc.MipLevels				= static_cast<UINT>(mip_levels);
			texture_desc.ArraySize				= array_size;
			texture_desc.Format					= d3d11_format[format];
			texture_desc.SampleDesc.Count		= 1;
			texture_desc.SampleDesc.Quality		= 0;
			texture_desc.Usage					= D3D11_USAGE_IMMUTABLE;
			texture_desc.BindFlags				= D3D11_BIND_SHADER_RESOURCE;
			texture_desc.MiscFlags				= D3D11_RESOURCE_MISC_TEXTURECUBE;
			texture_desc.CPUAccessFlags			= 0;

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
				mip_width	= Max(mip_width / 2, static_cast<unsigned int>(1));
				mip_height	= Max(mip_height / 2, static_cast<unsigned int>(1));
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
		if (!rhi_device->GetContext()->device)
		{
			LOG_ERROR("Invalid RHI device.");
			return false;
		}

		// Create the Texture Resource
		auto result = rhi_device->GetContext()->device->CreateTexture2D(vec_texture_desc.data(), vec_subresource_data.data(), &texture);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create ID3D11Texture2D. Invalid CreateTexture2D() parameters, %s", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// If we have created the texture resource for the six faces we create the Shader Resource View to use in our shaders.
		auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&resource_texture);
		result = rhi_device->GetContext()->device->CreateShaderResourceView(texture, &shader_resource_desc, ptr);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create the ID3D11ShaderResourceView, %s", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		safe_release(texture);
		return true;
	}

	inline bool CreateAsDepthStencil
	(
		void*& resource_texture,
		vector<void*>& resource_depth_stencils,
		unsigned int width,
		unsigned int height,
		unsigned int array_size,
		RHI_Format format,
		const shared_ptr<RHI_Device>& rhi_device
	)
	{
		RHI_Format format_buffer = Format_R32_FLOAT_TYPELESS;
		RHI_Format format_dsv = Format_D32_FLOAT;
		RHI_Format format_srv = Format_R32_FLOAT;

		if (format == Format_D32_FLOAT)
		{
			format_buffer = Format_R32_FLOAT_TYPELESS;
			format_dsv = Format_D32_FLOAT;
			format_srv = Format_R32_FLOAT;
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
		depth_buffer_desc.CPUAccessFlags = 0;

		ID3D11Texture2D* depth_stencil_texture = nullptr;
		auto result = rhi_device->GetContext()->device->CreateTexture2D(&depth_buffer_desc, nullptr, &depth_stencil_texture);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create depth stencil texture, %s.", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// DSV
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
		dsv_desc.Format							= d3d11_format[format_dsv];
		dsv_desc.ViewDimension					= D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsv_desc.Texture2DArray.MipSlice		= 0;
		dsv_desc.Texture2DArray.ArraySize		= 1;
		dsv_desc.Texture2DArray.FirstArraySlice = 0;

		for (unsigned int i = 0; i < array_size; i++)
		{
			dsv_desc.Texture2DArray.FirstArraySlice = i;
			auto ptr = reinterpret_cast<ID3D11DepthStencilView**>(&resource_depth_stencils.emplace_back(nullptr));
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
			srv_desc.ViewDimension					= D3D11_SRV_DIMENSION_TEXTURECUBE;
			srv_desc.Texture2DArray.FirstArraySlice = 0;
			srv_desc.Texture2DArray.MostDetailedMip = 0;
			srv_desc.Texture2DArray.MipLevels		= 1;
			srv_desc.Texture2DArray.ArraySize		= array_size;

			auto ptr = reinterpret_cast<ID3D11ShaderResourceView**>(&resource_texture);
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

	bool RHI_TextureCube::CreateResourceGpu()
	{
		bool is_depth_stencil = (m_format == Format_D32_FLOAT);

		bool result = true;
		if (is_depth_stencil)
		{
			result = CreateAsDepthStencil
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
			result = CreateFromData
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