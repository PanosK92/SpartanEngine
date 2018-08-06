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

//= INCLUDES ======================
#include "D3D11_InputLayout.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
//=================================

namespace Directus
{
	D3D11_InputLayout::D3D11_InputLayout(std::shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice			= rhiDevice;
		m_ID3D11InputLayout	= nullptr;
		m_inputLayout		= Input_PositionTextureTBN;
	}

	D3D11_InputLayout::~D3D11_InputLayout()
	{
		SafeRelease(m_ID3D11InputLayout);
	}

	//= LAYOUT CREATION ==================================================
	bool D3D11_InputLayout::Create(ID3D10Blob* VSBlob, D3D11_INPUT_ELEMENT_DESC* vertexInputLayout, unsigned int elementCount)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("D3D11_InputLayout::Create: Graphics device is not present.");
			return false;
		}

		HRESULT result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateInputLayout(
			vertexInputLayout,
			elementCount,
			VSBlob->GetBufferPointer(),
			VSBlob->GetBufferSize(),
			&m_ID3D11InputLayout
		);

		return SUCCEEDED(result);
	}

	bool D3D11_InputLayout::Create(ID3D10Blob* VSBlob, Input_Layout layout)
	{
		m_inputLayout = layout;

		if (m_inputLayout == Input_Position)
			return CreatePosDesc(VSBlob);

		if (m_inputLayout == Input_PositionColor)
			return CreatePosColDesc(VSBlob);

		if (m_inputLayout == Input_PositionTexture)
			return CreatePosTexDesc(VSBlob);

		if (m_inputLayout == Input_PositionTextureTBN)
			return CreatePosTBNDesc(VSBlob);

		return false;
	}

	//= LAYOUTS DESCRIPTIONS =============================================
	bool D3D11_InputLayout::CreatePosDesc(ID3D10Blob* VSBlob)
	{
		D3D11_INPUT_ELEMENT_DESC positionDesc;
		positionDesc.SemanticName = "POSITION";
		positionDesc.SemanticIndex = 0;
		positionDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		positionDesc.InputSlot = 0;
		positionDesc.AlignedByteOffset = 0;
		positionDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		positionDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(positionDesc);

		return Create(VSBlob, &m_layoutDesc[0], unsigned int(m_layoutDesc.size()));
	}

	bool D3D11_InputLayout::CreatePosColDesc(ID3D10Blob* VSBlob)
	{
		D3D11_INPUT_ELEMENT_DESC positionDesc;
		positionDesc.SemanticName = "POSITION";
		positionDesc.SemanticIndex = 0;
		positionDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		positionDesc.InputSlot = 0;
		positionDesc.AlignedByteOffset = 0;
		positionDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		positionDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(positionDesc);

		D3D11_INPUT_ELEMENT_DESC colorDesc;
		colorDesc.SemanticName = "COLOR";
		colorDesc.SemanticIndex = 0;
		colorDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		colorDesc.InputSlot = 0;
		colorDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		colorDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		colorDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(colorDesc);

		return Create(VSBlob, &m_layoutDesc[0], unsigned int(m_layoutDesc.size()));
	}

	bool D3D11_InputLayout::CreatePosTexDesc(ID3D10Blob* VSBlob)
	{
		D3D11_INPUT_ELEMENT_DESC positionDesc;
		positionDesc.SemanticName = "POSITION";
		positionDesc.SemanticIndex = 0;
		positionDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		positionDesc.InputSlot = 0;
		positionDesc.AlignedByteOffset = 0;
		positionDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		positionDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(positionDesc);

		D3D11_INPUT_ELEMENT_DESC texCoordDesc;
		texCoordDesc.SemanticName = "TEXCOORD";
		texCoordDesc.SemanticIndex = 0;
		texCoordDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		texCoordDesc.InputSlot = 0;
		texCoordDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		texCoordDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		texCoordDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(texCoordDesc);

		return Create(VSBlob, &m_layoutDesc[0], unsigned int(m_layoutDesc.size()));
	}

	bool D3D11_InputLayout::CreatePosTBNDesc(ID3D10Blob* VSBlob)
	{
		D3D11_INPUT_ELEMENT_DESC positionDesc;
		positionDesc.SemanticName = "POSITION";
		positionDesc.SemanticIndex = 0;
		positionDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		positionDesc.InputSlot = 0;
		positionDesc.AlignedByteOffset = 0;
		positionDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		positionDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(positionDesc);

		D3D11_INPUT_ELEMENT_DESC texCoordDesc;
		texCoordDesc.SemanticName = "TEXCOORD";
		texCoordDesc.SemanticIndex = 0;
		texCoordDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		texCoordDesc.InputSlot = 0;
		texCoordDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		texCoordDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		texCoordDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(texCoordDesc);

		D3D11_INPUT_ELEMENT_DESC normalDesc;
		normalDesc.SemanticName = "NORMAL";
		normalDesc.SemanticIndex = 0;
		normalDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		normalDesc.InputSlot = 0;
		normalDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		normalDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		normalDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(normalDesc);

		D3D11_INPUT_ELEMENT_DESC tangentDesc;
		tangentDesc.SemanticName = "TANGENT";
		tangentDesc.SemanticIndex = 0;
		tangentDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		tangentDesc.InputSlot = 0;
		tangentDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		tangentDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		tangentDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(tangentDesc);

		D3D11_INPUT_ELEMENT_DESC bitangentDesc;
		bitangentDesc.SemanticName = "BITANGENT";
		bitangentDesc.SemanticIndex = 0;
		bitangentDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		bitangentDesc.InputSlot = 0;
		bitangentDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		bitangentDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		bitangentDesc.InstanceDataStepRate = 0;
		m_layoutDesc.push_back(bitangentDesc);

		return Create(VSBlob, &m_layoutDesc[0], unsigned int(m_layoutDesc.size()));
	}
}