/*
Copyright(c) 2016-2024 Panos Karabelas

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

#pragma once

//= INCLUDES =====================
#include "../Core/SpartanObject.h"
//================================

namespace Spartan
{
    class RHI_StructuredBuffer : public SpartanObject
    {
    public:
        RHI_StructuredBuffer(const uint32_t stride, const uint32_t element_count, const char* name);
        ~RHI_StructuredBuffer();

        void Update(void* data, const uint32_t update_size = 0);
        void ResetOffset()           { m_offset = 0; first_update = true; }
        uint32_t GetStride()   const { return m_stride; }
        uint32_t GetOffset()   const { return m_offset; }
        void* GetRhiResource() const { return m_rhi_resource; }

    private:
        uint32_t m_stride        = 0;
        uint32_t m_offset        = 0;
        uint32_t m_element_count = 0;
        bool first_update        = true;
        void* m_mapped_data      = nullptr;
        void* m_rhi_resource     = nullptr;
    };
}
