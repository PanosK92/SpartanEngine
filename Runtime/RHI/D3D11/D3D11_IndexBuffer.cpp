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

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_IndexBuffer::RHI_IndexBuffer(const std::shared_ptr<RHI_Device>& rhi_device, const RHI_Format format)
	{
		m_rhiDevice		= rhi_device;
		m_buffer		= nullptr;
		m_buffer_format	= format;
		m_memory_usage	= 0;
		m_index_count	= 0;
	}

	RHI_IndexBuffer::~RHI_IndexBuffer()
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));
	}

	bool RHI_IndexBuffer::Create(const vector<unsigned int>& indices)
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));

		if (!m_rhiDevice || !m_rhiDevice->GetContext()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (indices.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_index_count					= static_cast<unsigned int>(indices.size());
		const unsigned int byte_width	= sizeof(unsigned int) * m_index_count;

		D3D11_BUFFER_DESC buffer_desc;
		ZeroMemory(&buffer_desc, sizeof(buffer_desc));
		buffer_desc.ByteWidth			= byte_width;
		buffer_desc.Usage				= D3D11_USAGE_IMMUTABLE;
		buffer_desc.BindFlags			= D3D11_BIND_INDEX_BUFFER;
		buffer_desc.CPUAccessFlags		= 0;
		buffer_desc.MiscFlags			= 0;
		buffer_desc.StructureByteStride	= 0;

		D3D11_SUBRESOURCE_DATA init_data;
		init_data.pSysMem			= indices.data();
		init_data.SysMemPitch		= 0;
		init_data.SysMemSlicePitch	= 0;

		const auto ptr		= reinterpret_cast<ID3D11Buffer**>(&m_buffer);
		const auto result	= m_rhiDevice->GetContext()->device->CreateBuffer(&buffer_desc, &init_data, ptr);
		if FAILED(result)
		{
			LOG_ERROR(" Failed to create index buffer");
			return false;
		}

		// Compute memory usage
		m_memory_usage = unsigned int((sizeof(unsigned int) * indices.size()));

		return true;
	}

	bool RHI_IndexBuffer::CreateDynamic(const unsigned int stride, const unsigned int index_count)
	{
		safe_release(static_cast<ID3D11Buffer*>(m_buffer));

		if (!m_rhiDevice || !m_rhiDevice->GetContext()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_index_count = index_count;

		D3D11_BUFFER_DESC buffer_desc;
		ZeroMemory(&buffer_desc, sizeof(buffer_desc));
		buffer_desc.ByteWidth			= index_count * stride;
		buffer_desc.Usage				= D3D11_USAGE_DYNAMIC;
		buffer_desc.BindFlags			= D3D11_BIND_INDEX_BUFFER;
		buffer_desc.CPUAccessFlags		= D3D11_CPU_ACCESS_WRITE;
		buffer_desc.MiscFlags			= 0;
		buffer_desc.StructureByteStride	= 0;

		const auto ptr		= reinterpret_cast<ID3D11Buffer**>(&m_buffer);
		const auto result	= m_rhiDevice->GetContext()->device->CreateBuffer(&buffer_desc, nullptr, ptr);
		if FAILED(result)
		{
			LOG_ERROR("Failed to create dynamic index buffer");
			return false;
		}

		return true;
	}

	void* RHI_IndexBuffer::Map() const
	{
		if (!m_rhiDevice || !m_rhiDevice->GetContext()->device_context || !m_buffer)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return nullptr;
		}

		D3D11_MAPPED_SUBRESOURCE mapped_resource;
		const auto result = m_rhiDevice->GetContext()->device_context->Map(static_cast<ID3D11Resource*>(m_buffer), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to map index buffer.");
			return nullptr;
		}

		return mapped_resource.pData;
	}

	bool RHI_IndexBuffer::Unmap() const
	{
		if (!m_rhiDevice || !m_rhiDevice->GetContext()->device_context || !m_buffer)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhiDevice->GetContext()->device_context->Unmap(static_cast<ID3D11Resource*>(m_buffer), 0);

		return true;
	}
}
#endif