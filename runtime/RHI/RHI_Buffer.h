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
    enum class RHI_Buffer_Type
    {
        Vertex,
        Index,
        Instance,
        Storage,
        Constant,
        Max
    };

    class RHI_Buffer : public SpartanObject
    {
    public:
        RHI_Buffer() = default;
        RHI_Buffer(const RHI_Buffer_Type type, const size_t stride, const uint32_t element_count, const void* data, const bool mappable, const char* name)
        {
            // check
            SP_ASSERT(type != RHI_Buffer_Type::Max);
            SP_ASSERT(stride != 0);
            SP_ASSERT(element_count != 0);
            SP_ASSERT_MSG(name != nullptr, "Name the buffer to aid the validation layer");
            if (m_type == RHI_Buffer_Type::Constant)
            {
                SP_ASSERT_MSG(mappable, "Constant buffers must be mappable");
            }

            // set
            m_type             = type;
            m_stride_unaligned = static_cast<uint32_t>(stride);
            m_stride           = m_stride_unaligned;
            m_element_count    = element_count;
            m_object_size      = stride * element_count;
            m_mappable         = mappable;
            m_object_name      = name;

            // allocate
            RHI_CreateResource(data);
        }
        ~RHI_Buffer() { RHI_DestroyResource(); }

        // storage and constant buffer updating
        void Update(void* data_cpu, const uint32_t size = 0);
        void ResetOffset() { m_offset = 0; first_update = true; }

        // propeties
        uint32_t GetStrideUnaligned() const { return m_stride_unaligned; }
        uint32_t GetStride() const          { return m_stride; }
        uint32_t GetElementCount() const    { return m_element_count; }
        uint32_t GetOffset()   const        { return m_offset; }
        void* GetMappedData() const         { return m_data_gpu; }
        void* GetRhiResource() const        { return m_rhi_resource; }

    private:
        RHI_Buffer_Type m_type      = RHI_Buffer_Type::Max;
        uint32_t m_stride_unaligned = 0;
        uint32_t m_stride           = 0;
        uint32_t m_element_count    = 0;
        uint32_t m_offset           = 0;
        void* m_data_gpu            = nullptr;
        bool m_mappable             = false;
        bool first_update           = true;

        // rhi
        void RHI_DestroyResource();
        void RHI_CreateResource(const void* data);
        void* m_rhi_resource = nullptr;
    };
}
