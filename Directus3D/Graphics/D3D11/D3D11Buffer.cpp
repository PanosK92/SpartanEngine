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
#include "D3D11Buffer.h"
#include "D3D11Graphics.h"
#include "../../Core/Helper.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

D3D11Buffer::D3D11Buffer()
{
	m_graphics = nullptr;
	m_buffer = nullptr;
	m_stride = -1;
	m_size = -1;
	m_usage = (D3D11_USAGE)0;
	m_bindFlag = (D3D11_BIND_FLAG)0;
	m_cpuAccessFlag = (D3D11_CPU_ACCESS_FLAG)0;
}

D3D11Buffer::~D3D11Buffer()
{
	SafeRelease(m_buffer);
}

void D3D11Buffer::Initialize(Graphics* graphicsDevice)
{
	m_graphics = graphicsDevice;
}

bool D3D11Buffer::CreateConstantBuffer(unsigned int size)
{
	bool result = Create(
		-1,
		size,
		nullptr,
		D3D11_USAGE_DYNAMIC,
		D3D11_BIND_CONSTANT_BUFFER,
		D3D11_CPU_ACCESS_WRITE
	);

	if (!result)
		LOG_ERROR("Failed to create constant buffer.");

	return result;
}

bool D3D11Buffer::CreateVertexBuffer(vector<VertexPositionTextureNormalTangent>& vertices)
{
	bool result = Create(
		sizeof(VertexPositionTextureNormalTangent),
		(unsigned int)vertices.size(),
		&vertices[0],
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_VERTEX_BUFFER,
		static_cast<D3D11_CPU_ACCESS_FLAG>(0)
	);

	if (!result)
		LOG_ERROR("Failed to create vertex buffer.");

	return result;
}

bool D3D11Buffer::CreateIndexBuffer(vector<unsigned int>& indices)
{
	bool result = Create(
		sizeof(unsigned int),
		indices.size(),
		&indices[0],
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_INDEX_BUFFER,
		static_cast<D3D11_CPU_ACCESS_FLAG>(0)
	);

	if (!result)
		LOG_ERROR("Failed to create index buffer.");

	return result;
}

bool D3D11Buffer::Create(unsigned int stride, unsigned int size, void* data, D3D11_USAGE usage, D3D11_BIND_FLAG bindFlag, D3D11_CPU_ACCESS_FLAG cpuAccessFlag)
{
	m_stride = stride;
	m_size = size;
	m_usage = usage;
	m_bindFlag = bindFlag;
	m_cpuAccessFlag = cpuAccessFlag;
	unsigned int finalSize = m_stride * m_size;

	// This means that this is a constant buffer
	if (stride == -1 && size != -1)
		finalSize = size;

	// fill in a buffer description.
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.Usage = usage;
	bufferDesc.ByteWidth = finalSize;
	bufferDesc.BindFlags = bindFlag;
	bufferDesc.CPUAccessFlags = cpuAccessFlag;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	// fill in the subresource data.
	D3D11_SUBRESOURCE_DATA initData;
	initData.pSysMem = data;
	initData.SysMemPitch = 0;
	initData.SysMemSlicePitch = 0;

	HRESULT result;
	if ((bindFlag == D3D11_BIND_VERTEX_BUFFER || bindFlag == D3D11_BIND_INDEX_BUFFER) && data)
		result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_buffer);
	else
		result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, nullptr, &m_buffer);

	return FAILED(result) ? false : true;
}

void D3D11Buffer::SetIA()
{
	unsigned int offset = 0;

	if (m_bindFlag == D3D11_BIND_VERTEX_BUFFER)
		m_graphics->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_buffer, &m_stride, &offset);
	else if (m_bindFlag == D3D11_BIND_INDEX_BUFFER)
		m_graphics->GetDeviceContext()->IASetIndexBuffer(m_buffer, DXGI_FORMAT_R32_UINT, 0);
}

void D3D11Buffer::SetVS(unsigned int startSlot)
{
	m_graphics->GetDeviceContext()->VSSetConstantBuffers(startSlot, 1, &m_buffer);
}

void D3D11Buffer::SetPS(unsigned int startSlot)
{
	m_graphics->GetDeviceContext()->PSSetConstantBuffers(startSlot, 1, &m_buffer);
}

void* D3D11Buffer::Map()
{
	if (!m_buffer)
	{
		LOG_ERROR("Can't map uninitialized buffer.");
		return nullptr;
	}

	// disable GPU access to the vertex buffer data.
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = m_graphics->GetDeviceContext()->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to map buffer.");
		return nullptr;
	}

	return mappedResource.pData;
}

void D3D11Buffer::Unmap()
{
	// re-enable GPU access to the vertex buffer data.
	m_graphics->GetDeviceContext()->Unmap(m_buffer, 0);
}