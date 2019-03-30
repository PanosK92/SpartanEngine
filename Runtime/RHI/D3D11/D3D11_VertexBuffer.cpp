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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES =====================
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
	RHI_VertexBuffer::RHI_VertexBuffer(const std::shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device		= rhi_device;
		m_buffer		= nullptr;
		m_stride		= 0;
		m_vertex_count	= 0;
		m_memory_usage	= 0;
	}

	RHI_VertexBuffer::~RHI_VertexBuffer()
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosCol>& vertices)
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));

		if (!m_rhi_device || !m_rhi_device->GetContext()->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (vertices.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_stride		= sizeof(RHI_Vertex_PosCol);
		m_vertex_count	= static_cast<unsigned int>(vertices.size());

		// fill in a buffer description.
		D3D11_BUFFER_DESC buffer_desc;
		ZeroMemory(&buffer_desc, sizeof(buffer_desc));
		buffer_desc.ByteWidth			= m_stride * m_vertex_count;
		buffer_desc.Usage				= D3D11_USAGE_IMMUTABLE;
		buffer_desc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		buffer_desc.CPUAccessFlags		= 0;
		buffer_desc.MiscFlags			= 0;
		buffer_desc.StructureByteStride	= 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA init_data;
		init_data.pSysMem			= vertices.data();
		init_data.SysMemPitch		= 0;
		init_data.SysMemSlicePitch	= 0;

		// Compute memory usage
		m_memory_usage = static_cast<unsigned int>(sizeof(RHI_Vertex_PosCol) * vertices.size());

		const auto ptr = reinterpret_cast<ID3D11Buffer**>(&m_buffer);
		const auto result = m_rhi_device->GetContext()->device->CreateBuffer(&buffer_desc, &init_data, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosUV>& vertices)
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));

		if (!m_rhi_device || !m_rhi_device->GetContext()->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (vertices.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_stride		= sizeof(RHI_Vertex_PosUV);
		m_vertex_count	= static_cast<unsigned int>(vertices.size());

		// fill in a buffer description.
		D3D11_BUFFER_DESC buffer_desc;
		ZeroMemory(&buffer_desc, sizeof(buffer_desc));
		buffer_desc.ByteWidth			= m_stride * m_vertex_count;
		buffer_desc.Usage				= D3D11_USAGE_IMMUTABLE;
		buffer_desc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		buffer_desc.CPUAccessFlags		= 0;
		buffer_desc.MiscFlags			= 0;
		buffer_desc.StructureByteStride	= 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA init_data;
		init_data.pSysMem			= vertices.data();
		init_data.SysMemPitch		= 0;
		init_data.SysMemSlicePitch	= 0;

		// Compute memory usage
		m_memory_usage = static_cast<unsigned int>(sizeof(RHI_Vertex_PosUV) * vertices.size());

		const auto ptr		= reinterpret_cast<ID3D11Buffer**>(&m_buffer);
		const auto result	= m_rhi_device->GetContext()->device->CreateBuffer(&buffer_desc, &init_data, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosUvNorTan>& vertices)
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));

		if (!m_rhi_device || !m_rhi_device->GetContext()->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (vertices.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_stride		= sizeof(RHI_Vertex_PosUvNorTan);
		m_vertex_count	= static_cast<unsigned int>(vertices.size());

		// fill in a buffer description.
		D3D11_BUFFER_DESC buffer_desc;
		ZeroMemory(&buffer_desc, sizeof(buffer_desc));
		buffer_desc.ByteWidth			= m_stride * m_vertex_count;
		buffer_desc.Usage				= D3D11_USAGE_IMMUTABLE;
		buffer_desc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		buffer_desc.CPUAccessFlags		= 0;
		buffer_desc.MiscFlags			= 0;
		buffer_desc.StructureByteStride	= 0;

		// fill in the subresource data.
		D3D11_SUBRESOURCE_DATA init_data;
		init_data.pSysMem			= vertices.data();
		init_data.SysMemPitch		= 0;
		init_data.SysMemSlicePitch	= 0;

		// Compute memory usage
		m_memory_usage = static_cast<unsigned int>(sizeof(RHI_Vertex_PosUvNorTan) * vertices.size());

		const auto ptr = reinterpret_cast<ID3D11Buffer**>(&m_buffer);
		const auto result = m_rhi_device->GetContext()->device->CreateBuffer(&buffer_desc, &init_data, ptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create vertex buffer");
			return false;
		}

		return true;
	}

	bool RHI_VertexBuffer::CreateDynamic(const unsigned int stride, const unsigned int vertex_count)
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));

		if (!m_rhi_device || !m_rhi_device->GetContext()->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_stride		= stride;
		m_vertex_count	= vertex_count;

		// fill in a buffer description.
		D3D11_BUFFER_DESC buffer_desc;
		ZeroMemory(&buffer_desc, sizeof(buffer_desc));
		buffer_desc.ByteWidth			= vertex_count * stride;
		buffer_desc.Usage				= D3D11_USAGE_DYNAMIC;
		buffer_desc.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
		buffer_desc.CPUAccessFlags		= D3D11_CPU_ACCESS_WRITE;
		buffer_desc.MiscFlags			= 0;
		buffer_desc.StructureByteStride	= 0;

		const auto ptr = reinterpret_cast<ID3D11Buffer**>(&m_buffer);
		const auto result = m_rhi_device->GetContext()->device->CreateBuffer(&buffer_desc, nullptr, ptr);
		if FAILED(result)
		{
			LOG_ERROR("Failed to create dynamic vertex buffer");
			return false;
		}

		return true;
	}

	void* RHI_VertexBuffer::Map() const
	{
		if (!m_rhi_device || !m_rhi_device->GetContext()->device_context || !m_buffer)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return nullptr;
		}

		// Disable GPU access to the vertex buffer data.
		D3D11_MAPPED_SUBRESOURCE mapped_resource;
		const auto result = m_rhi_device->GetContext()->device_context->Map(static_cast<ID3D11Resource*>(m_buffer), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to map vertex buffer");
			return nullptr;
		}

		return mapped_resource.pData;
	}

	bool RHI_VertexBuffer::Unmap() const
	{
		if (!m_rhi_device || !m_rhi_device->GetContext()->device_context || !m_buffer)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Re-enable GPU access to the vertex buffer data.
		m_rhi_device->GetContext()->device_context->Unmap(static_cast<ID3D11Resource*>(m_buffer), 0);
		return true;
	}
}
#endif