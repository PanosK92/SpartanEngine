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
        Max
    };

    class RHI_Buffer : public SpartanObject
    {
    public:
        RHI_Buffer() = default;
        RHI_Buffer(const RHI_Buffer_Type type, const uint32_t stride, const uint32_t element_count, const bool is_mappable, const char* name)
        {
            m_type          = type;
            m_stride        = stride;
            m_element_count = element_count;
            m_object_size   = stride * element_count;
            m_is_mappable   = is_mappable;
            m_object_name   = name;

            if (m_type == RHI_Buffer_Type::Storage)
            {
                SP_ASSERT_MSG(m_is_mappable, "Storage buffers must be mappable")
                RHI_CreateResource(nullptr);
            }
        }
        ~RHI_Buffer() { RHI_DestroyResource(); }

        template<typename T>
        void Create(const std::vector<T>& data)
        {
            m_stride        = sizeof(T);
            m_element_count = static_cast<uint32_t>(data.size());
            m_object_size   = static_cast<uint64_t>(m_stride * m_element_count);

            RHI_CreateResource(static_cast<const void*>(data.data()));
        }

        template<typename T>
        void Create(const T* indices, const uint32_t index_count)
        {
            m_stride        = sizeof(T);
            m_element_count = index_count;
            m_object_size   = static_cast<uint64_t>(m_stride * m_element_count);

            RHI_CreateResource(static_cast<const void*>(indices));
        }

        template<typename T>
        void CreateDynamic(const uint32_t index_count)
        {
            m_stride        = sizeof(T);
            m_element_count = index_count;
            m_object_size   = static_cast<uint64_t>(m_stride * m_element_count);

            RHI_CreateResource(nullptr);
        }

        void Update(void* data_cpu, const uint32_t size = 0);
        void ResetOffset() { m_offset = 0; first_update = true; }

        // propeties
        void* GetMappedData() const      { return m_data; }
        void* GetRhiResource() const     { return m_rhi_resource; }
        uint32_t GetElementCount() const { return m_element_count; }
        uint32_t GetStride() const       { return m_stride; }
        uint32_t GetOffset()   const     { return m_offset; }

    private:
        RHI_Buffer_Type m_type   = RHI_Buffer_Type::Max;
        uint32_t m_stride        = 0;
        uint32_t m_element_count = 0;
        uint32_t m_offset        = 0;
        void* m_data             = nullptr;
        bool m_is_mappable       = false;
        bool first_update        = true;

        // rhi
        void RHI_DestroyResource();
        void RHI_CreateResource(const void* indices);
        void* m_rhi_resource = nullptr;
    };
}
