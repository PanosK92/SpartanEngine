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

#pragma once

//= INCLUDES =====================
#include "../Core/SpartanObject.h"
//================================

namespace Spartan
{
    class RHI_StructuredBuffer : public SpartanObject
    {
    public:
        RHI_StructuredBuffer(const std::shared_ptr<RHI_Device>& rhi_device, const uint32_t stride, const uint32_t element_count, const char* name);
        ~RHI_StructuredBuffer();

        // Advance offset, copy memory and flush/unmap
        template<typename T>
        void Update(T& data_cpu)
        {
            SP_ASSERT_MSG(m_offset + m_stride <= m_object_size_gpu, "Out of memory");

            // Advance offset
            m_offset += m_stride;
            if (m_reset_offset)
            {
                m_offset = 0;
                m_reset_offset = false;
            }

            // Map (Vulkan uses persistent mapping so it will simply return the already mapped pointer)
            T* data_gpu = static_cast<T*>(Map());

            // Copy
            memcpy(reinterpret_cast<std::byte*>(data_gpu) + m_offset, reinterpret_cast<std::byte*>(&data_cpu), m_stride);

            // Flush/Unmap
            if (m_persistent_mapping) // Vulkan
            {
                Flush(m_stride, m_offset);
            }
            else // D3D11
            {
                Unmap();
            }
        }

        void* Map();
        void Unmap();
        void Flush(const uint64_t size, const uint64_t offset);
        void ResetOffset() { m_reset_offset = true; }

        uint32_t GetStride() const { return m_stride; }
        uint32_t GetOffset() const { return m_offset; }

        void* GetRhiResource() { return m_rhi_resource; }
        void* GetRhiUav()      { return m_rhi_uav; }

    private:
        uint32_t m_stride         = 0;
        uint32_t m_offset         = 0;
        uint32_t m_element_count  = 0;
        bool m_reset_offset       = false;
        bool m_persistent_mapping = false;
        void* m_mapped_data       = nullptr;
        void* m_rhi_resource      = nullptr;
        void* m_rhi_uav           = nullptr;

        std::shared_ptr<RHI_Device> m_rhi_device;
    };
}
