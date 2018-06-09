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

//= INCLUDES ====================
#include "D3D11_ConstantBuffer.h"
#include "../Backend_Imp.h"
#include "../../../Logging/Log.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	D3D11_ConstantBuffer::D3D11_ConstantBuffer(D3D11_Device* graphicsDevice) : m_graphics(graphicsDevice)
	{
		m_buffer = nullptr;
	}

	D3D11_ConstantBuffer::~D3D11_ConstantBuffer()
	{
		SafeRelease(m_buffer);
	}

	bool D3D11_ConstantBuffer::Create(unsigned int size)
	{
		if (!m_graphics->GetDevice())
			return false;

		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = size;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, nullptr, &m_buffer);
		if FAILED(result)
		{
			LOG_ERROR("Failed to create constant buffer");
			return false;
		}

		return true;
	}

	void* D3D11_ConstantBuffer::Map()
	{
		if (!m_graphics->GetDeviceContext())
			return nullptr;

		if (!m_buffer)
		{
			LOG_ERROR("Can't map uninitialized constant buffer.");
			return nullptr;
		}

		// disable GPU access to the vertex buffer data.
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT result = m_graphics->GetDeviceContext()->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to map constant buffer.");
			return nullptr;
		}

		return mappedResource.pData;
	}

	bool D3D11_ConstantBuffer::Unmap()
	{
		if (!m_buffer || !m_graphics->GetDeviceContext())
			return false;

		// re-enable GPU access to the vertex buffer data.
		m_graphics->GetDeviceContext()->Unmap(m_buffer, 0);

		return true;
	}

	bool D3D11_ConstantBuffer::SetVS(unsigned int startSlot)
	{
		if (!m_buffer || !m_graphics->GetDeviceContext())
			return false;

		m_graphics->GetDeviceContext()->VSSetConstantBuffers(startSlot, 1, &m_buffer);

		return true;
	}

	bool D3D11_ConstantBuffer::SetPS(unsigned int startSlot)
	{
		if (!m_buffer || !m_graphics->GetDeviceContext())
			return false;

		m_graphics->GetDeviceContext()->PSSetConstantBuffers(startSlot, 1, &m_buffer);

		return true;
	}
}