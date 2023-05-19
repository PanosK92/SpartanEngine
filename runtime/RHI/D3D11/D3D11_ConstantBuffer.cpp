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
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Device.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_ConstantBuffer::RHI_ConstantBuffer(const string& name)
    {
        m_object_name = name;
    }

    RHI_ConstantBuffer::~RHI_ConstantBuffer()
    {
        if (m_rhi_resource)
        {
            d3d11_utility::release<ID3D11Buffer>(m_rhi_resource);
        }
    }

    void RHI_ConstantBuffer::RHI_CreateResource()
    {
        if (m_rhi_resource)
        {
            d3d11_utility::release<ID3D11Buffer>(m_rhi_resource);
        }

        D3D11_BUFFER_DESC buffer_desc   = {};
        buffer_desc.ByteWidth           = static_cast<UINT>(m_stride);
        buffer_desc.Usage               = D3D11_USAGE_DYNAMIC;
        buffer_desc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
        buffer_desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        buffer_desc.MiscFlags           = 0;
        buffer_desc.StructureByteStride = 0;

        SP_ASSERT(d3d11_utility::error_check(RHI_Context::device->CreateBuffer(&buffer_desc, nullptr, reinterpret_cast<ID3D11Buffer**>(&m_rhi_resource))));
    }

    void RHI_ConstantBuffer::Update(void* data_cpu)
    {
        // Map
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        SP_ASSERT_MSG(SUCCEEDED(RHI_Context::device_context->Map(static_cast<ID3D11Buffer*>(m_rhi_resource), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)),
            "Failed to map constant buffer");

        // Copy
        memcpy(reinterpret_cast<std::byte*>(mapped_resource.pData), reinterpret_cast<std::byte*>(data_cpu), m_stride);

        // Unmap
        SP_ASSERT(m_rhi_resource != nullptr);
        RHI_Context::device_context->Unmap(static_cast<ID3D11Buffer*>(m_rhi_resource), 0);
    }
}
