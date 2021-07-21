/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_StructuredBuffer.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_StructuredBuffer::RHI_StructuredBuffer(const shared_ptr<RHI_Device>& rhi_device, const uint32_t stride, const uint32_t element_count, const void* data /*= nullptr*/)
    {
        m_rhi_device = rhi_device;

        // Buffer
        D3D11_BUFFER_DESC desc = {};
        {
            desc.Usage                  = D3D11_USAGE_DEFAULT;
            desc.ByteWidth              = stride * element_count;
            desc.BindFlags              = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags              = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride    = stride;

            // Initial data
            D3D11_SUBRESOURCE_DATA subresource_data = {};
            subresource_data.pSysMem                = &data;

            if (!d3d11_utility::error_check(m_rhi_device->GetContextRhi()->device->CreateBuffer(&desc, data ? &subresource_data : nullptr, reinterpret_cast<ID3D11Buffer**>(&m_resource))))
                return;
        }

        // UAV
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC desc   = {};
            desc.ViewDimension                      = D3D11_UAV_DIMENSION_BUFFER;
            desc.Format                             = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.FirstElement                = 0;
            desc.Buffer.NumElements                 = element_count;

            d3d11_utility::error_check(rhi_device->GetContextRhi()->device->CreateUnorderedAccessView(static_cast<ID3D11Resource*>(m_resource), &desc, reinterpret_cast<ID3D11UnorderedAccessView**>(&m_resource)));
        }
    }

    RHI_StructuredBuffer::~RHI_StructuredBuffer()
    {
        d3d11_utility::release(static_cast<ID3D11Buffer*>(m_resource));
        d3d11_utility::release(static_cast<ID3D11UnorderedAccessView*>(m_resource));
    }
}
