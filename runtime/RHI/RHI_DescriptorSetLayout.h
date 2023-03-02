/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "../Core/Object.h"
#include <unordered_map>
#include <vector>
#include <array>
#include "RHI_Descriptor.h"
//================================

// A descriptor set layout is created by individual descriptors.
// The descriptors come from shader reflection and contain no resource pointers.
// The descriptor set layout resource is part of the pipeline creation.
// 
// The descriptors that are a member RHI_DescriptorSetLayout also hold resource pointers.
// These descriptors are used to created a descriptor set.
// The descriptor is what is actually bound before any draw/dispatch calls.
 
namespace Spartan
{
    class SP_CLASS RHI_DescriptorSetLayout : public Object
    {
    public:
        RHI_DescriptorSetLayout() = default;
        RHI_DescriptorSetLayout(RHI_Device* rhi_device, const std::vector<RHI_Descriptor>& descriptors, const std::string& name);
        ~RHI_DescriptorSetLayout();

        // Set
        void SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer);
        void SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer);
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler);
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, const uint32_t mip_range);

        // Dynamic offsets
        void GetDynamicOffsets(std::vector<uint32_t>* offsets);

        // Misc
        void ClearDescriptorData();
        RHI_DescriptorSet* GetDescriptorSet();
        void NeedsToBind()        { m_needs_to_bind = true; }
        void* GetResource() const { return m_resource; }

    private:
        void CreateResource(const std::vector<RHI_Descriptor>& descriptors);

        // Descriptor set layout
        void* m_resource = nullptr;
        uint64_t m_hash  = 0;

        // Descriptors
        std::vector<RHI_Descriptor> m_descriptors;

        // Misc
        bool m_needs_to_bind     = false;
        RHI_Device* m_rhi_device = nullptr;
    };
}
