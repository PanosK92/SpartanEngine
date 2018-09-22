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

//= INCLUDES =====================
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_Vertex.h"
#include "../../Logging/Log.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_VertexBuffer::RHI_VertexBuffer(std::shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice		= rhiDevice;
		m_buffer		= nullptr;
		m_stride		= 0;
		m_memoryUsage	= 0;
	}

	RHI_VertexBuffer::~RHI_VertexBuffer()
	{
		SafeRelease((ID3D11Buffer*)m_buffer);
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosCol>& vertices)
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Invalid RHI device");
			return false;
		}

		if (vertices.empty())
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Invalid parameter");
			return false;
		}

		m_stride				= sizeof(RHI_Vertex_PosCol);
		auto size				= (unsigned int)vertices.size();
		unsigned int byteWidth	= m_stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth			= byteWidth;
		bufferDesc.Usage				= D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags		= 0;
		bufferDesc.MiscFlags			= 0;
		bufferDesc.StructureByteStride	= 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = vertices.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(RHI_Vertex_PosCol) * vertices.size());

		auto ptr = (ID3D11Buffer**)&m_buffer;
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateBuffer(&bufferDesc, &initData, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosUV>& vertices)
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDevice<ID3D11Device>())
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Invalid RHI device");
			return false;
		}

		if (vertices.empty())
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Invalid parameter");
			return false;
		}

		m_stride				= sizeof(RHI_Vertex_PosUV);
		auto size				= (unsigned int)vertices.size();
		unsigned int byteWidth	= m_stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth			= byteWidth;
		bufferDesc.Usage				= D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags		= 0;
		bufferDesc.MiscFlags			= 0;
		bufferDesc.StructureByteStride	= 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem			= vertices.data();
		initData.SysMemPitch		= 0;
		initData.SysMemSlicePitch	= 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(RHI_Vertex_PosUV) * vertices.size());

		auto ptr = (ID3D11Buffer**)&m_buffer;
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateBuffer(&bufferDesc, &initData, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosUVTBN>& vertices)
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDevice<ID3D11Device>() || vertices.empty())
			return false;

		m_stride = sizeof(RHI_Vertex_PosUVTBN);
		unsigned int size		= (unsigned int)vertices.size();
		unsigned int byteWidth	= m_stride * size;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth			= byteWidth;
		bufferDesc.Usage				= D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags		= 0;
		bufferDesc.MiscFlags			= 0;
		bufferDesc.StructureByteStride	= 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem			= vertices.data();
		initData.SysMemPitch		= 0;
		initData.SysMemSlicePitch	= 0;

		// Compute memory usage
		m_memoryUsage = (unsigned int)(sizeof(RHI_Vertex_PosUVTBN) * vertices.size());

		auto ptr = (ID3D11Buffer**)&m_buffer;
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateBuffer(&bufferDesc, &initData, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_VertexBuffer::Create: Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool RHI_VertexBuffer::CreateDynamic(unsigned int stride, unsigned int initialSize)
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11Device>())
		{
			LOG_ERROR("RHI_VertexBuffer::CreateDynamic: Invalid RHI device");
			return false;
		}

		m_stride = stride;
		unsigned int byteWidth = m_stride * initialSize;

		// fill in a buffer description.
		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));
		bufferDesc.ByteWidth			= byteWidth;
		bufferDesc.Usage				= D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags		= D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags			= 0;
		bufferDesc.StructureByteStride	= 0;

		auto ptr = (ID3D11Buffer**)&m_buffer;
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateBuffer(&bufferDesc, nullptr, ptr);
		if FAILED(result)
		{
			LOG_ERROR("RHI_VertexBuffer::Bind: Failed to create dynamic vertex buffer");
			return false;
		}

		return true;
	}

	void* RHI_VertexBuffer::Map()
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>())
		{
			LOG_ERROR("RHI_VertexBuffer::Map: Invalid RHI device");
			return false;
		}

		if (!m_buffer)
		{
			LOG_ERROR("RHI_IndexBuffer::Map: Invalid buffer");
			return false;
		}

		// disable GPU access to the vertex buffer data.
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT result = m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->Map((ID3D11Resource*)m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_VertexBuffer::Map: Failed to map vertex buffer");
			return nullptr;
		}

		return mappedResource.pData;
	}

	bool RHI_VertexBuffer::Unmap()
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>())
		{
			LOG_ERROR("RHI_VertexBuffer::Unmap: Invalid RHI device");
			return false;
		}

		if (!m_buffer)
		{
			LOG_ERROR("RHI_IndexBuffer::Unmap: Invalid buffer");
			return false;
		}

		// re-enable GPU access to the vertex buffer data.
		m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->Unmap((ID3D11Resource*)m_buffer, 0);

		return true;
	}

	bool RHI_VertexBuffer::Bind()
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>())
		{
			LOG_ERROR("RHI_VertexBuffer::Bind: Invalid RHI device");
			return false;
		}

		if (!m_buffer)
		{
			LOG_ERROR("RHI_IndexBuffer::Bind: Invalid buffer");
			return false;
		}

		unsigned int offset = 0;
		auto ptr = (ID3D11Buffer*const*)&m_buffer;
		m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->IASetVertexBuffers(0, 1, ptr, &m_stride, &offset);
		return true;
	}
}