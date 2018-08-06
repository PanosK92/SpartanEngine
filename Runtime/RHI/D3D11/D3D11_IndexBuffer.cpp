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
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../../Logging/Log.h"
#include "../../Profiling/Profiler.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_IndexBuffer::RHI_IndexBuffer(std::shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice		= rhiDevice;
		m_buffer		= nullptr;
		m_memoryUsage	= 0;
	}

	RHI_IndexBuffer::~RHI_IndexBuffer()
	{
		SafeRelease((ID3D11Buffer*)m_buffer);
	}

	bool RHI_IndexBuffer::Create(const vector<unsigned int>& indices)
	{
		if (!m_rhiDevice->GetDevice<ID3D11Device>() || indices.empty())
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

		auto ptr = (ID3D11Buffer**)&m_buffer;
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateBuffer(&bufferDesc, &initData, ptr);
		if FAILED(result)
		{
			LOG_ERROR("D3D11IndexBuffer: Failed to create index buffer");
			return false;
		}

		return true;
	}

	bool RHI_IndexBuffer::CreateDynamic(unsigned int initialSize)
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDevice<ID3D11Device>())
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

		auto ptr = (ID3D11Buffer**)&m_buffer;
		auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateBuffer(&bufferDesc, nullptr, ptr);
		if FAILED(result)
		{
			LOG_ERROR("D3D11IndexBuffer: Failed to create dynamic index buffer");
			return false;
		}

		return true;
	}

	void* RHI_IndexBuffer::Map()
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>())
			return nullptr;

		if (!m_buffer)
		{
			LOG_ERROR("D3D11IndexBuffer: Can't map uninitialized index buffer.");
			return nullptr;
		}

		// disable GPU access to the index buffer data.
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		auto result = m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->Map((ID3D11Resource*)m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11IndexBuffer: Failed to map index buffer.");
			return nullptr;
		}

		return mappedResource.pData;
	}

	bool RHI_IndexBuffer::Unmap()
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>() || !m_buffer)
			return false;

		// re-enable GPU access to the index buffer data.
		m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->Unmap((ID3D11Resource*)m_buffer, 0);

		return true;
	}

	bool RHI_IndexBuffer::Bind()
	{
		if (!m_rhiDevice || !m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>() || !m_buffer)
		{
			LOG_ERROR("D3D11_IndexBuffer::Bind: Invalid device context");
			return false;
		}

		m_rhiDevice->GetDeviceContext<ID3D11DeviceContext>()->IASetIndexBuffer((ID3D11Buffer*)m_buffer, DXGI_FORMAT_R32_UINT, 0);
		return true;
	}
}