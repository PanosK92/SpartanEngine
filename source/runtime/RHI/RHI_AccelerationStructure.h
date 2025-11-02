/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "RHI_Definitions.h"
#include <vector>
#include <array>
//================================

namespace spartan
{
   enum class RHI_AccelerationStructureType
    {
        Bottom,
        Top,
        Max
    };

    struct RHI_AccelerationStructureGeometry
    {
        bool transparent                  = false;
        RHI_Format vertex_format          = RHI_Format::Max;
        uint64_t vertex_buffer_address    = 0;
        uint32_t vertex_stride            = 0;
        uint32_t max_vertex               = 0;
        RHI_Format index_format           = RHI_Format::Max;
        uint64_t index_buffer_address     = 0;
    };

    struct RHI_AccelerationStructureInstance
    {
        std::array<float, 12> transform                      = {}; // row-major 3x4 matrix
        uint32_t instance_custom_index                       = 0;
        uint32_t mask                                        = 0xFF;
        uint32_t instance_shader_binding_table_record_offset = 0;
        uint32_t flags                                       = 0;
        uint64_t device_address                              = 0;
    };

    class RHI_AccelerationStructure : public SpartanObject
    {
    public:
        RHI_AccelerationStructure(const RHI_AccelerationStructureType type, const char* name);
        ~RHI_AccelerationStructure();

        void BuildBottomLevel(RHI_CommandList* cmd_list, const std::vector<RHI_AccelerationStructureGeometry>& geometries, const std::vector<uint32_t>& primitive_counts);
        void BuildTopLevel(RHI_CommandList* cmd_list, const std::vector<RHI_AccelerationStructureInstance>& instances);

        // misc
        uint64_t GetDeviceAddress();
        void* GetRhiResource() const                  { return m_rhi_resource; }
        RHI_AccelerationStructureType GetType() const { return m_type; }

    private:
        void Destroy();

        // misc
        RHI_AccelerationStructureType m_type = RHI_AccelerationStructureType::Max;
        uint64_t m_size                      = 0;

        // rhi
        void* m_rhi_resource         = nullptr;
        void* m_rhi_resource_results = nullptr;

        // reusable buffers
        void* m_scratch_buffer          = nullptr;
        uint64_t m_scratch_buffer_size  = 0;
        void* m_instance_buffer         = nullptr;
        uint64_t m_instance_buffer_size = 0;
    };
}
