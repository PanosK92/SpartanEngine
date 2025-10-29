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
#include <vector>
#include "RHI_Descriptor.h"
//================================

// A descriptor set layout is created by individual descriptors.
// The descriptors come from shader reflection and contain no resource pointers.
// The descriptor set layout resource is part of the pipeline creation.
// 
// The descriptors that are a member RHI_DescriptorSetLayout also hold resource pointers.
// These descriptors are used to created a descriptor set.
// The descriptor set is what is actually bound before any draw/dispatch calls.
 
namespace spartan
{
    class RHI_DescriptorSetLayout : public SpartanObject
    {
    public:
         RHI_DescriptorSetLayout() = default;
         RHI_DescriptorSetLayout(const RHI_Descriptor* descriptors, size_t count, const char* name);
        ~RHI_DescriptorSetLayout();

        // set
        void SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer);
        void SetBuffer(const uint32_t slot, RHI_Buffer* buffer);
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, const uint32_t mip_range);
        void SetAccelerationStructure(const uint32_t slot, RHI_AccelerationStructure* tlas);

        // dynamic offsets
        void GetDynamicOffsets(std::array<uint32_t, 10>* offsets, uint32_t* count);

        // misc
        void ClearDescriptorData();
        RHI_DescriptorSet* GetDescriptorSet();
        const std::vector<RHI_Descriptor>& GetDescriptors() const { return m_descriptors; }
        uint64_t GetHash() const                                  { return m_hash; }
        void* GetRhiResource() const                              { return m_rhi_resource; }

    private:
        void CreateRhiResource(std::vector<RHI_Descriptor> descriptors);

        void* m_rhi_resource = nullptr;
        uint64_t m_hash      = 0;
        std::vector<RHI_Descriptor> m_descriptors;
    };
}
