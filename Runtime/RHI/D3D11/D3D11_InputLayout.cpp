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
#include "../RHI_InputLayout.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
//================================

//==================
using namespace std;
//==================

namespace Directus
{
	RHI_InputLayout::RHI_InputLayout(shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice = rhiDevice;
	}

	RHI_InputLayout::~RHI_InputLayout()
	{
		SafeRelease((ID3D11InputLayout*)m_buffer);
	}

	bool RHI_InputLayout::Create(void* vsBlob, unsigned long input_layout)
	{
		if (!vsBlob)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_inputLayout = input_layout;
		
		vector<D3D11_INPUT_ELEMENT_DESC> layoutDescs;
		if (m_inputLayout & Input_Position2D)
		{
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_inputLayout & Input_Position3D)
		{
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_inputLayout & Input_Texture)
		{
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_inputLayout & Input_Color8)
		{
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_inputLayout & Input_Color32)
		{
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		if (m_inputLayout & Input_NormalTangent)
		{
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "NORMAL",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
			layoutDescs.emplace_back(D3D11_INPUT_ELEMENT_DESC{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 });
		}

		auto buffer = (ID3D10Blob*)vsBlob;
		if (FAILED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateInputLayout
		(
			layoutDescs.data(),
			(unsigned int)layoutDescs.size(),
			buffer->GetBufferPointer(),
			buffer->GetBufferSize(),
			(ID3D11InputLayout**)&m_buffer
		)))
		{
			LOG_ERROR("Failed to create input layout");
			return false;
		}

		return true;
	}
}