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

//= INCLUDES ===========================
#include "D3D11_VertexBuffer.h"
#include "../Backend_Imp.h"
#include "../../../Logging/Log.h"
#include "../../../Profiling/Profiler.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	D3D11_VertexBuffer::D3D11_VertexBuffer(D3D11_Device* graphicsDevice)
	{
		m_graphics = graphicsDevice;
		m_buffer = nullptr;
		m_stride = 0;
		m_memoryUsage = 0;
	}

	D3D11_VertexBuffer::~D3D11_VertexBuffer()
	{
		SafeRelease(m_buffer);
	}

	bool D3D11_VertexBuffer::Create(const vector<RI_Vertex_PosCol>& vertices)
	{
		if (!m_graphics->GetDevice() || vertices.empty())
			return false;

		m_stride = sizeof(RI_Vertex_PosCol);
		unsigned int size = (unsigned int)vertices.size();
		unsigned int byteWidth = m_stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = byteWidth;
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = vertices.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(RI_Vertex_PosCol) * vertices.size());

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_buffer);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11VertexBuffer: Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool D3D11_VertexBuffer::Create(const vector<RI_Vertex_PosUV>& vertices)
	{
		if (!m_graphics || !m_graphics->GetDevice() || vertices.empty())
			return false;

		m_stride = sizeof(RI_Vertex_PosUV);
		auto size = (unsigned int)vertices.size();
		unsigned int byteWidth = m_stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = byteWidth;
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = vertices.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(RI_Vertex_PosUV) * vertices.size());

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_buffer);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11VertexBuffer: Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool D3D11_VertexBuffer::Create(const vector<RI_Vertex_PosUVTBN>& vertices)
	{
		if (!m_graphics || !m_graphics->GetDevice() || vertices.empty())
			return false;

		m_stride = sizeof(RI_Vertex_PosUVTBN);
		unsigned int size = (unsigned int)vertices.size();
		unsigned int byteWidth = m_stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = byteWidth;
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = vertices.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(RI_Vertex_PosUVTBN) * vertices.size());

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_buffer);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11VertexBuffer: Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool D3D11_VertexBuffer::CreateDynamic(unsigned int stride, unsigned int initialSize)
	{
		if (!m_graphics || !m_graphics->GetDevice())
			return false;

		m_stride = stride;
		unsigned int byteWidth = m_stride * initialSize;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth = byteWidth;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		HRESULT result = m_graphics->GetDevice()->CreateBuffer(&bufferDesc, nullptr, &m_buffer);
		if FAILED(result)
		{
			LOG_ERROR("D3D11VertexBuffer: Failed to create dynamic vertex buffer");
			return false;
		}

		return true;
	}

	void* D3D11_VertexBuffer::Map()
	{
		if (!m_graphics)
		{
			LOG_ERROR("D3D11VertexBuffer: Can't map. Graphics adapter is uninitialized.");
			return nullptr;
		}

		if (!m_graphics->GetDeviceContext())
		{
			LOG_ERROR("D3D11VertexBuffer: Can't map. Graphics adapter context is uninitialized.");
			return nullptr;
		}

		if (!m_buffer)
		{
			LOG_ERROR("D3D11VertexBuffer: Can't map. Buffer is uninitialized.");
			return nullptr;
		}

		// disable GPU access to the vertex buffer data.
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT result = m_graphics->GetDeviceContext()->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11VertexBuffer: Failed to map vertex buffer.");
			return nullptr;
		}

		return mappedResource.pData;
	}

	bool D3D11_VertexBuffer::Unmap()
	{
		if (!m_graphics)
		{
			LOG_ERROR("D3D11VertexBuffer: Can't unmap. Graphics adapter is uninitialized.");
			return false;
		}

		if (!m_graphics->GetDeviceContext())
		{
			LOG_ERROR("D3D11VertexBuffer: Can't unmap. Graphics adapter context is uninitialized.");
			return false;
		}

		if (!m_buffer)
		{
			LOG_ERROR("D3D11VertexBuffer: Can't unmap. Buffer is uninitialized.");
			return false;
		}

		// re-enable GPU access to the vertex buffer data.
		m_graphics->GetDeviceContext()->Unmap(m_buffer, 0);

		return true;
	}

	bool D3D11_VertexBuffer::SetIA()
	{
		if (!m_graphics)
		{
			LOG_ERROR("D3D11VertexBuffer: Can't set input assembly. Graphics adapter is uninitialized.");
			return false;
		}

		if (!m_graphics->GetDeviceContext())
		{
			LOG_ERROR("D3D11VertexBuffer: Can't set input assembly. Graphics adapter context is uninitialized.");
			return false;
		}

		if (!m_buffer)
		{
			LOG_ERROR("D3D11VertexBuffer: Can't set input assembly. Buffer is uninitialized.");
			return false;
		}

		Profiler::Get().m_bindBufferVertexCount++;
		unsigned int offset = 0;
		m_graphics->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_buffer, &m_stride, &offset);

		return true;
	}
}