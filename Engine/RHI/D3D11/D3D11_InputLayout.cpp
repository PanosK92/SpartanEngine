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

namespace Directus
{
	RHI_InputLayout::RHI_InputLayout(const shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device = rhi_device;
	}

	RHI_InputLayout::~RHI_InputLayout()
	{
		safe_release(static_cast<ID3D11InputLayout*>(m_buffer));
	}

	bool RHI_InputLayout::Create(void* vsBlob, const unsigned long input_layout)
	{
		if (!vsBlob)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_input_layout = input_layout;
		
		vector<D3D11_INPUT_ELEMENT_DESC> layout_descs;
		if (m_input_layout & Input_Position2D)
		{
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_input_layout & Input_Position3D)
		{
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_input_layout & Input_Texture)
		{
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_input_layout & Input_Color8)
		{
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_input_layout & Input_Color32)
		{
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_input_layout & Input_NormalTangent)
		{
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "NORMAL",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
			layout_descs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		auto buffer = static_cast<ID3D10Blob*>(vsBlob);
		if (FAILED(m_rhi_device->GetDevicePhysical<ID3D11Device>()->CreateInputLayout
		(
			layout_descs.data(),
			static_cast<unsigned int>(layout_descs.size()),
			buffer->GetBufferPointer(),
			buffer->GetBufferSize(),
			reinterpret_cast<ID3D11InputLayout**>(&m_buffer)
		)))
		{
			LOG_ERROR("Failed to create input layout");
			return false;
		}

		return true;
	}
}
#endif