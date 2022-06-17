/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Runtime/Core/Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_VertexBuffer::_destroy()
    {
        d3d11_utility::release<ID3D11Buffer>(m_resource);
    }

    bool RHI_VertexBuffer::_create(const void* vertices)
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetContextRhi()->device_context != nullptr);

        const bool is_dynamic = vertices == nullptr;

        // Destroy previous buffer
        _destroy();

        // fill in a buffer description.
        D3D11_BUFFER_DESC buffer_desc   = {};
        buffer_desc.ByteWidth           = static_cast<UINT>(m_object_size_gpu);
        buffer_desc.Usage               = is_dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
        buffer_desc.CPUAccessFlags      = is_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
        buffer_desc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
        buffer_desc.MiscFlags           = 0;
        buffer_desc.StructureByteStride = 0;

        // fill in the subresource data.
        D3D11_SUBRESOURCE_DATA init_data = {};
        init_data.pSysMem                = vertices;
        init_data.SysMemPitch            = 0;
        init_data.SysMemSlicePitch       = 0;

        const auto ptr    = reinterpret_cast<ID3D11Buffer**>(&m_resource);
        const auto result = m_rhi_device->GetContextRhi()->device->CreateBuffer(&buffer_desc, is_dynamic ? nullptr : &init_data, ptr);
        if (FAILED(result))
        {
            LOG_ERROR("Failed to create vertex buffer");
            return false;
        }

        return true;
    }

    void* RHI_VertexBuffer::Map()
    {
        SP_ASSERT(m_resource != nullptr);

        // Disable GPU access to the vertex buffer data.
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        const auto result = m_rhi_device->GetContextRhi()->device_context->Map(static_cast<ID3D11Resource*>(m_resource), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
        if (FAILED(result))
        {
            LOG_ERROR("Failed to map vertex buffer");
            return nullptr;
        }

        return mapped_resource.pData;
    }

    void RHI_VertexBuffer::Unmap()
    {
        SP_ASSERT(m_resource != nullptr);

        // Re-enable GPU access to the vertex buffer data.
        m_rhi_device->GetContextRhi()->device_context->Unmap(static_cast<ID3D11Resource*>(m_resource), 0);
    }
}
