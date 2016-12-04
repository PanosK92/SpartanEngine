/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ==================
#include "D3D11InputLayout.h"
#include "D3D11GraphicsDevice.h"
#include "../../Core/Helper.h"
#include "../../Logging/Log.h"
//=============================

D3D11InputLayout::D3D11InputLayout()
{
	m_ID3D11InputLayout = nullptr;
	m_graphics = nullptr;
}

D3D11InputLayout::~D3D11InputLayout()
{
	SafeRelease(m_ID3D11InputLayout);
}

//= MISC ==================================================
void D3D11InputLayout::Initialize(D3D11GraphicsDevice* graphicsDevice)
{
	m_graphics = graphicsDevice;
}

void D3D11InputLayout::Set()
{
	m_graphics->GetDeviceContext()->IASetInputLayout(m_ID3D11InputLayout);
}

InputLayout D3D11InputLayout::GetInputLayout()
{
	return m_inputLayout;
}

//= LAYOUT CREATION ==================================================
bool D3D11InputLayout::Create(ID3D10Blob* VSBlob, D3D11_INPUT_ELEMENT_DESC* vertexInputLayout, UINT elementCount)
{
	HRESULT result = m_graphics->GetDevice()->CreateInputLayout(
		vertexInputLayout,
		elementCount,
		VSBlob->GetBufferPointer(),
		VSBlob->GetBufferSize(),
		&m_ID3D11InputLayout
	);

	if (FAILED(result))
		return false;

	return true;
}

bool D3D11InputLayout::Create(ID3D10Blob* VSBlob, InputLayout layout)
{
	m_inputLayout = layout;

	if (m_inputLayout == Position)
		return CreatePosDesc(VSBlob);

	if (m_inputLayout == PositionColor)
		return CreatePosColDesc(VSBlob);

	if (m_inputLayout == PositionTexture)
		return CreatePosTexDesc(VSBlob);

	if (m_inputLayout == PositionTextureNormalTangent)
		return CreatePosTexNorTanDesc(VSBlob);

	return false;
}

//= LAYOUTS DESCRIPTIONS =============================================
bool D3D11InputLayout::CreatePosDesc(ID3D10Blob* VSBlob)
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

	return Create(VSBlob, &m_layoutDesc[0], UINT(m_layoutDesc.size()));
}

bool D3D11InputLayout::CreatePosColDesc(ID3D10Blob* VSBlob)
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

	return Create(VSBlob, &m_layoutDesc[0], UINT(m_layoutDesc.size()));
}

bool D3D11InputLayout::CreatePosTexDesc(ID3D10Blob* VSBlob)
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

	return Create(VSBlob, &m_layoutDesc[0], UINT(m_layoutDesc.size()));
}

bool D3D11InputLayout::CreatePosTexNorTanDesc(ID3D10Blob* VSBlob)
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

	return Create(VSBlob, &m_layoutDesc[0], UINT(m_layoutDesc.size()));
}