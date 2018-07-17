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

//= INCLUDES ========================
#include "D3D11_Device.h"
#include "D3D11_IndexBuffer.h"
#include "../RHI_Implementation.h"
#include "../../Logging/Log.h"
#include "../../Profiling/Profiler.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	D3D11_IndexBuffer::D3D11_IndexBuffer(D3D11_Device* graphicsDevice) : m_graphics(graphicsDevice)
	{
		m_buffer = nullptr;
		m_memoryUsage = 0;
	}

	D3D11_IndexBuffer::~D3D11_IndexBuffer()
	{
		SafeRelease(m_buffer);
	}

	bool D3D11_IndexBuffer::Create(const vector<unsigned int>& indices)
	{
		if (!m_graphics->GetDevice() || indices.empty())
			return false;

		unsigned int stride = sizeof(unsigned int);
		unsigned int size = (unsigned int)indices.size();
		unsigned int finalSize = stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = finalSize;
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = indices.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(unsigned int) * indices.size());

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_buffer);
		if FAILED(result)
		{
			LOG_ERROR("D3D11IndexBuffer: Failed to create index buffer");
			return false;
		}

		return true;
	}

	bool D3D11_IndexBuffer::CreateDynamic(unsigned int initialSize)
	{
		if (!m_graphics || !m_graphics->GetDevice())
			return false;

		unsigned int byteWidth = sizeof(unsigned int) * initialSize;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = byteWidth;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, nullptr, &m_buffer);
		if FAILED(result)
		{
			LOG_ERROR("D3D11IndexBuffer: Failed to create dynamic index buffer");
			return false;
		}

		return true;
	}

	void* D3D11_IndexBuffer::Map()
	{
		if (!m_graphics || !m_graphics->GetDeviceContext())
			return nullptr;

		if (!m_buffer)
		{
			LOG_ERROR("D3D11IndexBuffer: Can't map uninitialized index buffer.");
			return nullptr;
		}

		// disable GPU access to the index buffer data.
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT result = m_graphics->GetDeviceContext()->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11IndexBuffer: Failed to map index buffer.");
			return nullptr;
		}

		return mappedResource.pData;
	}

	bool D3D11_IndexBuffer::Unmap()
	{
		if (!m_graphics || !m_graphics->GetDeviceContext() || !m_buffer)
			return false;

		// re-enable GPU access to the index buffer data.
		m_graphics->GetDeviceContext()->Unmap(m_buffer, 0);

		return true;
	}

	bool D3D11_IndexBuffer::SetIA()
	{
		if (!m_graphics->GetDeviceContext() || !m_buffer)
			return false;

		Profiler::Get().m_bindBufferIndexCount++;
		m_graphics->GetDeviceContext()->IASetIndexBuffer(m_buffer, DXGI_FORMAT_R32_UINT, 0);
		return true;
	}
}