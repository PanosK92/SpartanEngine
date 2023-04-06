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

//= INCLUDES =======================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_StructuredBuffer.h"
#include "../Rendering/Renderer.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_StructuredBuffer::RHI_StructuredBuffer(const uint32_t stride, const uint32_t element_count, const char* name)
    {
        // Buffer
        D3D11_BUFFER_DESC desc = {};
        {
            desc.Usage               = D3D11_USAGE_DEFAULT;
            desc.ByteWidth           = stride * element_count;
            desc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
            desc.StructureByteStride = stride;

            // Initial data
            void* data                              = nullptr;
            D3D11_SUBRESOURCE_DATA subresource_data = {};
            subresource_data.pSysMem                = data;

            SP_ASSERT_MSG(d3d11_utility::error_check(RHI_Context::device->CreateBuffer(&desc, data ? &subresource_data : nullptr, reinterpret_cast<ID3D11Buffer**>(&m_rhi_resource))),
                "Failed to create buffer");
        }

        // UAV
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
            desc.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
            desc.Format                           = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.FirstElement              = 0;
            desc.Buffer.NumElements               = element_count;

            SP_ASSERT_MSG(d3d11_utility::error_check(RHI_Context::device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(m_rhi_resource), &desc, reinterpret_cast<ID3D11UnorderedAccessView**>(&m_rhi_uav))),
                "Failed to create UAV");
        }
    }

    RHI_StructuredBuffer::~RHI_StructuredBuffer()
    {
        d3d11_utility::release<ID3D11Buffer>(m_rhi_resource);
        d3d11_utility::release<ID3D11UnorderedAccessView>(m_rhi_uav);
    }

    void RHI_StructuredBuffer::Update(void* data_cpu)
    {
        // Map
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        if (FAILED(RHI_Context::device_context->Map(static_cast<ID3D11Buffer*>(m_rhi_resource), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
        {
            SP_LOG_ERROR("Failed to map structured buffer");
        }

        // Copy
        memcpy(reinterpret_cast<std::byte*>(mapped_resource.pData), reinterpret_cast<std::byte*>(data_cpu), m_stride);

        // Unmap
        RHI_Context::device_context->Unmap(static_cast<ID3D11Buffer*>(m_rhi_resource), 0);
    }
}
