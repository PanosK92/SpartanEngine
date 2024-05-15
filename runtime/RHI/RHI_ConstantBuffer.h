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
#include <memory>
#include "../Core/SpartanObject.h"
//================================

namespace Spartan
{
    class SP_CLASS RHI_ConstantBuffer : public SpartanObject
    {
    public:
        RHI_ConstantBuffer() = default;
        RHI_ConstantBuffer(const std::string& name);
        ~RHI_ConstantBuffer();

        template<typename T>
        void Create(const uint32_t element_count)
        {
            SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(T);
            SP_ASSERT_MSG(sizeof(T) % 16 == 0, "The size is not a multiple of 16");
            SP_ASSERT_MSG(element_count != 0,  "Element count can't be zero");

            m_type_size     = static_cast<uint32_t>(sizeof(T));
            m_stride        = m_type_size; // will be aligned based on minimum device offset alignment
            m_element_count = element_count;

            RHI_CreateResource();
        }

        void Update(void* data_cpu);

        void ResetOffset()
        {
            m_offset      = 0;
            m_has_updated = false;
        }

        uint32_t GetStructSize()  const { return m_type_size; }
        uint32_t GetStride()      const { return m_stride; }
        uint32_t GetOffset()      const { return m_offset; }
        uint32_t GetStrideCount() const { return m_element_count; }
        void* GetRhiResource()    const { return m_rhi_resource; }

    private:
        void RHI_CreateResource();

        uint32_t m_type_size     = 0;
        uint32_t m_stride        = 0;
        uint32_t m_offset        = 0;
        uint32_t m_element_count = 0;
        bool m_has_updated       = false;
        void* m_mapped_data      = nullptr;
        void* m_rhi_resource     = nullptr;
    };
}
