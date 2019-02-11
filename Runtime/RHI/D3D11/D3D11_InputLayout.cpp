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
	namespace D3D11_InputLayout
	{
		inline bool Create(ID3D11Device* device, ID3D11InputLayout** buffer, ID3D10Blob* VSBlob, D3D11_INPUT_ELEMENT_DESC* vertexInputLayout, unsigned int elementCount)
		{
			auto result = device->CreateInputLayout(
				vertexInputLayout,
				elementCount,
				VSBlob->GetBufferPointer(),
				VSBlob->GetBufferSize(),
				buffer
			);

			return SUCCEEDED(result);
		}

		inline void CreatePosDesc(ID3D10Blob* VSBlob, vector<any>* layout)
		{
			D3D11_INPUT_ELEMENT_DESC positionDesc;
			positionDesc.SemanticName			= "POSITION";
			positionDesc.SemanticIndex			= 0;
			positionDesc.Format					= DXGI_FORMAT_R32G32B32_FLOAT;
			positionDesc.InputSlot				= 0;
			positionDesc.AlignedByteOffset		= 0;
			positionDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			positionDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(positionDesc);
		}

		inline void CreatePosColDesc(ID3D10Blob* VSBlob, vector<any>* layout)
		{
			D3D11_INPUT_ELEMENT_DESC positionDesc;
			positionDesc.SemanticName			= "POSITION";
			positionDesc.SemanticIndex			= 0;
			positionDesc.Format					= DXGI_FORMAT_R32G32B32_FLOAT;
			positionDesc.InputSlot				= 0;
			positionDesc.AlignedByteOffset		= 0;
			positionDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			positionDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(positionDesc);

			D3D11_INPUT_ELEMENT_DESC colorDesc;
			colorDesc.SemanticName			= "COLOR";
			colorDesc.SemanticIndex			= 0;
			colorDesc.Format				= DXGI_FORMAT_R32G32B32A32_FLOAT;
			colorDesc.InputSlot				= 0;
			colorDesc.AlignedByteOffset		= D3D11_APPEND_ALIGNED_ELEMENT;
			colorDesc.InputSlotClass		= D3D11_INPUT_PER_VERTEX_DATA;
			colorDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(colorDesc);
		}

		inline void CreatePosTexDesc(ID3D10Blob* VSBlob, vector<any>* layout)
		{
			D3D11_INPUT_ELEMENT_DESC positionDesc;
			positionDesc.SemanticName			= "POSITION";
			positionDesc.SemanticIndex			= 0;
			positionDesc.Format					= DXGI_FORMAT_R32G32B32_FLOAT;
			positionDesc.InputSlot				= 0;
			positionDesc.AlignedByteOffset		= 0;
			positionDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			positionDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(positionDesc);

			D3D11_INPUT_ELEMENT_DESC texCoordDesc;
			texCoordDesc.SemanticName			= "TEXCOORD";
			texCoordDesc.SemanticIndex			= 0;
			texCoordDesc.Format					= DXGI_FORMAT_R32G32_FLOAT;
			texCoordDesc.InputSlot				= 0;
			texCoordDesc.AlignedByteOffset		= D3D11_APPEND_ALIGNED_ELEMENT;
			texCoordDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			texCoordDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(texCoordDesc);
		}

		inline void CreatePosNTDesc(ID3D10Blob* VSBlob, vector<any>* layout)
		{
			D3D11_INPUT_ELEMENT_DESC positionDesc;
			positionDesc.SemanticName			= "POSITION";
			positionDesc.SemanticIndex			= 0;
			positionDesc.Format					= DXGI_FORMAT_R32G32B32_FLOAT;
			positionDesc.InputSlot				= 0;
			positionDesc.AlignedByteOffset		= 0;
			positionDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			positionDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(positionDesc);

			D3D11_INPUT_ELEMENT_DESC texCoordDesc;
			texCoordDesc.SemanticName			= "TEXCOORD";
			texCoordDesc.SemanticIndex			= 0;
			texCoordDesc.Format					= DXGI_FORMAT_R32G32_FLOAT;
			texCoordDesc.InputSlot				= 0;
			texCoordDesc.AlignedByteOffset		= D3D11_APPEND_ALIGNED_ELEMENT;
			texCoordDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			texCoordDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(texCoordDesc);

			D3D11_INPUT_ELEMENT_DESC normalDesc;
			normalDesc.SemanticName				= "NORMAL";
			normalDesc.SemanticIndex			= 0;
			normalDesc.Format					= DXGI_FORMAT_R32G32B32_FLOAT;
			normalDesc.InputSlot				= 0;
			normalDesc.AlignedByteOffset		= D3D11_APPEND_ALIGNED_ELEMENT;
			normalDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			normalDesc.InstanceDataStepRate		= 0;
			layout->emplace_back(normalDesc);

			D3D11_INPUT_ELEMENT_DESC tangentDesc;
			tangentDesc.SemanticName			= "TANGENT";
			tangentDesc.SemanticIndex			= 0;
			tangentDesc.Format					= DXGI_FORMAT_R32G32B32_FLOAT;
			tangentDesc.InputSlot				= 0;
			tangentDesc.AlignedByteOffset		= D3D11_APPEND_ALIGNED_ELEMENT;
			tangentDesc.InputSlotClass			= D3D11_INPUT_PER_VERTEX_DATA;
			tangentDesc.InstanceDataStepRate	= 0;
			layout->emplace_back(tangentDesc);
		}
	}

	RHI_InputLayout::RHI_InputLayout(shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice		= rhiDevice;
		m_buffer		= nullptr;
		m_inputLayout	= Input_PositionTextureNormalTangent;
	}

	RHI_InputLayout::~RHI_InputLayout()
	{
		SafeRelease((ID3D11InputLayout*)m_buffer);
	}

	bool RHI_InputLayout::Create(void* vsBlob, RHI_Input_Layout layout)
	{
		if (!vsBlob || layout == Input_NotAssigned)
		{
			LOG_ERROR("RHI_InputLayout::Create: Invalid parameters");
			return false;
		}

		m_inputLayout = layout;

		if (m_inputLayout == Input_Position)
		{
			D3D11_InputLayout::CreatePosDesc((ID3D10Blob*)vsBlob, &m_layoutDesc);
		}

		if (m_inputLayout == Input_PositionColor)
		{
			D3D11_InputLayout::CreatePosColDesc((ID3D10Blob*)vsBlob, &m_layoutDesc);
		}

		if (m_inputLayout == Input_PositionTexture)
		{
			D3D11_InputLayout::CreatePosTexDesc((ID3D10Blob*)vsBlob, &m_layoutDesc);
		}

		if (m_inputLayout == Input_PositionTextureNormalTangent)
		{
			D3D11_InputLayout::CreatePosNTDesc((ID3D10Blob*)vsBlob, &m_layoutDesc);
		}

		std::vector<D3D11_INPUT_ELEMENT_DESC> layoutDesc;
		for (const auto& desc : m_layoutDesc)
		{
			layoutDesc.emplace_back(any_cast<D3D11_INPUT_ELEMENT_DESC>(desc));
		}

		return D3D11_InputLayout::Create(
			m_rhiDevice->GetDevice<ID3D11Device>(),
			(ID3D11InputLayout**)&m_buffer,
			(ID3D10Blob*)vsBlob,
			layoutDesc.data(),
			(unsigned int)m_layoutDesc.size()
		);
	}
}