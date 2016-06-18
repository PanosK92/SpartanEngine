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
#include "D3D11Device.h"
#include "../../Misc/Globals.h"

//=============================

D3D11InputLayout::D3D11InputLayout()
{
	m_layout = nullptr;
	m_D3D11Device = nullptr;
}

void D3D11InputLayout::Initialize(D3D11Device* d3d11Device)
{
	m_D3D11Device = d3d11Device;
}

D3D11InputLayout::~D3D11InputLayout()
{
	DirectusSafeRelease(m_layout);
}

bool D3D11InputLayout::Create(ID3D10Blob* VSBlob, InputLayout layout)
{
	if (layout == Position)
		return CreatePos(VSBlob);

	if (layout == PositionColor)
		return CreatePosCol(VSBlob);

	if (layout == PositionTexture)
		return CreatePosTex(VSBlob);

	if (layout == PositionTextureNormalTangent)
		return CreatePosTexNorTan(VSBlob);

	return false;
}

void D3D11InputLayout::Set()
{
	m_D3D11Device->GetDeviceContext()->IASetInputLayout(m_layout);
}

/*------------------------------------------------------------------------------
							[LAYOUTS]
------------------------------------------------------------------------------*/
bool D3D11InputLayout::CreatePos(ID3D10Blob* VSBlob)
{
	const unsigned int elementCount = 1;
	D3D11_INPUT_ELEMENT_DESC vertexInputLayout[elementCount];

	vertexInputLayout[0].SemanticName = "POSITION";
	vertexInputLayout[0].SemanticIndex = 0;
	vertexInputLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexInputLayout[0].InputSlot = 0;
	vertexInputLayout[0].AlignedByteOffset = 0;
	vertexInputLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[0].InstanceDataStepRate = 0;

	return CreateLayout(vertexInputLayout, elementCount, VSBlob);
}

bool D3D11InputLayout::CreatePosCol(ID3D10Blob* VSBlob)
{
	const unsigned int elementCount = 2;
	D3D11_INPUT_ELEMENT_DESC vertexInputLayout[elementCount];

	vertexInputLayout[0].SemanticName = "POSITION";
	vertexInputLayout[0].SemanticIndex = 0;
	vertexInputLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexInputLayout[0].InputSlot = 0;
	vertexInputLayout[0].AlignedByteOffset = 0;
	vertexInputLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[0].InstanceDataStepRate = 0;

	vertexInputLayout[1].SemanticName = "COLOR";
	vertexInputLayout[1].SemanticIndex = 0;
	vertexInputLayout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	vertexInputLayout[1].InputSlot = 0;
	vertexInputLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexInputLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[1].InstanceDataStepRate = 0;

	return CreateLayout(vertexInputLayout, elementCount, VSBlob);
}

bool D3D11InputLayout::CreatePosTex(ID3D10Blob* VSBlob)
{
	const unsigned int elementCount = 2;
	D3D11_INPUT_ELEMENT_DESC vertexInputLayout[elementCount];

	vertexInputLayout[0].SemanticName = "POSITION";
	vertexInputLayout[0].SemanticIndex = 0;
	vertexInputLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexInputLayout[0].InputSlot = 0;
	vertexInputLayout[0].AlignedByteOffset = 0;
	vertexInputLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[0].InstanceDataStepRate = 0;

	vertexInputLayout[1].SemanticName = "TEXCOORD";
	vertexInputLayout[1].SemanticIndex = 0;
	vertexInputLayout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	vertexInputLayout[1].InputSlot = 0;
	vertexInputLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexInputLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[1].InstanceDataStepRate = 0;

	return CreateLayout(vertexInputLayout, elementCount, VSBlob);
}

bool D3D11InputLayout::CreatePosTexNorTan(ID3D10Blob* VSBlob)
{
	const unsigned int elementCount = 4;
	D3D11_INPUT_ELEMENT_DESC vertexInputLayout[elementCount];

	vertexInputLayout[0].SemanticName = "POSITION";
	vertexInputLayout[0].SemanticIndex = 0;
	vertexInputLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexInputLayout[0].InputSlot = 0;
	vertexInputLayout[0].AlignedByteOffset = 0;
	vertexInputLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[0].InstanceDataStepRate = 0;

	vertexInputLayout[1].SemanticName = "TEXCOORD";
	vertexInputLayout[1].SemanticIndex = 0;
	vertexInputLayout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	vertexInputLayout[1].InputSlot = 0;
	vertexInputLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexInputLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[1].InstanceDataStepRate = 0;

	vertexInputLayout[2].SemanticName = "NORMAL";
	vertexInputLayout[2].SemanticIndex = 0;
	vertexInputLayout[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexInputLayout[2].InputSlot = 0;
	vertexInputLayout[2].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexInputLayout[2].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[2].InstanceDataStepRate = 0;

	vertexInputLayout[3].SemanticName = "TANGENT";
	vertexInputLayout[3].SemanticIndex = 0;
	vertexInputLayout[3].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	vertexInputLayout[3].InputSlot = 0;
	vertexInputLayout[3].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	vertexInputLayout[3].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	vertexInputLayout[3].InstanceDataStepRate = 0;

	return CreateLayout(vertexInputLayout, elementCount, VSBlob);
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
bool D3D11InputLayout::CreateLayout(D3D11_INPUT_ELEMENT_DESC vertexInputLayout[], unsigned int elementCount, ID3D10Blob* VSBlob)
{
	HRESULT result = m_D3D11Device->GetDevice()->CreateInputLayout(vertexInputLayout, elementCount, VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), &m_layout);

	if (FAILED(result))
		return false;

	return true;
}
