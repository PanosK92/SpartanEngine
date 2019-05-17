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
#include "../RHI_InputLayout.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
//================================

//==================
using namespace std;
//==================

namespace Spartan
{
	RHI_InputLayout::RHI_InputLayout(const shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device = rhi_device;
	}

	RHI_InputLayout::~RHI_InputLayout()
	{
		safe_release(static_cast<ID3D11InputLayout*>(m_resource));
	}

	bool RHI_InputLayout::Create(void* vertex_shader_blob, const RHI_Vertex_Attribute_Type vertex_attributes)
	{
		if (!vertex_shader_blob)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}
		m_vertex_attributes = vertex_attributes;
		
		// Fill in attribute descriptions
		vector<D3D11_INPUT_ELEMENT_DESC> attribute_desc;
		if (m_vertex_attributes & Vertex_Attribute_Position2d)
		{
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_vertex_attributes & Vertex_Attribute_Position3d)
		{
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_vertex_attributes & Vertex_Attribute_Texture)
		{
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_vertex_attributes & Vertex_Attribute_Color8)
		{
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_vertex_attributes & Vertex_Attribute_Color32)
		{
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_vertex_attributes & Vertex_Attribute_NormalTangent)
		{
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
			attribute_desc.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		// Create input layout
		auto d3d_blob = static_cast<ID3D10Blob*>(vertex_shader_blob);
		if (FAILED(m_rhi_device->GetContext()->device->CreateInputLayout
		(
			attribute_desc.data(),
			static_cast<UINT>(attribute_desc.size()),
			d3d_blob->GetBufferPointer(),
			d3d_blob->GetBufferSize(),
			reinterpret_cast<ID3D11InputLayout**>(&m_resource)
		)))
		{
			LOG_ERROR("Failed to create input layout");
			return false;
		}

		return true;
	}
}
#endif