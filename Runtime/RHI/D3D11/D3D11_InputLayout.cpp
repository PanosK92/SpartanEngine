/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_InputLayout.h"
#include "../RHI_Device.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_InputLayout::~RHI_InputLayout()
    {
        d3d11_utility::release(*reinterpret_cast<ID3D11InputLayout**>(&m_resource));
    }

    bool RHI_InputLayout::_CreateResource(void* vertex_shader_blob)
    {
        if (!vertex_shader_blob)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        if (m_vertex_attributes.empty())
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        vector<D3D11_INPUT_ELEMENT_DESC> vertex_attributes;
        for (const auto& vertex_attribute : m_vertex_attributes)
        {
            vertex_attributes.emplace_back(D3D11_INPUT_ELEMENT_DESC
            { 
                vertex_attribute.name.c_str(),            // SemanticName
                0,                                        // SemanticIndex
                d3d11_format[vertex_attribute.format],    // Format
                0,                                        // InputSlot
                vertex_attribute.offset,                // AlignedByteOffset
                D3D11_INPUT_PER_VERTEX_DATA,            // InputSlotClass
                0                                        // InstanceDataStepRate
            });
        }

        // Create input layout
        auto d3d_blob = static_cast<ID3D10Blob*>(vertex_shader_blob);
        const auto result = m_rhi_device->GetContextRhi()->device->CreateInputLayout
        (
            vertex_attributes.data(),
            static_cast<UINT>(vertex_attributes.size()),
            d3d_blob->GetBufferPointer(),
            d3d_blob->GetBufferSize(),
            reinterpret_cast<ID3D11InputLayout**>(&m_resource)
        );

        if (FAILED(result))
        {
            LOG_ERROR("Failed to create input layout, %s", d3d11_utility::dxgi_error_to_string(result));
            return false;
        }

        return true;
    }
}
