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

    }

    bool RHI_InputLayout::_CreateResource(void* vertex_shader_blob)
    {
        SP_ASSERT(vertex_shader_blob != nullptr);
        SP_ASSERT(!m_vertex_attributes.empty());

        vector<D3D12_INPUT_ELEMENT_DESC > vertex_attributes;
        for (const auto& vertex_attribute : m_vertex_attributes)
        {
            vertex_attributes.emplace_back(D3D12_INPUT_ELEMENT_DESC
            {
                vertex_attribute.name.c_str(),              // SemanticName
                0,                                          // SemanticIndex
                d3d12_format[vertex_attribute.format],      // Format
                0,                                          // InputSlot
                vertex_attribute.offset,                    // AlignedByteOffset
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, // InputSlotClass
                0                                           // InstanceDataStepRate
            });
        }

        return true;
    }
}
