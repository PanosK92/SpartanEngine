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

#pragma once

//= INCLUDES ==============
#include "RHI_Object.h"
#include "RHI_Definition.h"
#include <unordered_map>
#include <vector>
#include <memory>
//=========================

namespace Spartan
{
    class SPARTAN_CLASS RHI_DescriptorCache : public RHI_Object
    {
    public:
        RHI_DescriptorCache(const RHI_Device* rhi_device);
        ~RHI_DescriptorCache();

        void SetPipelineState(RHI_PipelineState& pipeline_state);

        // Descriptor resource updating
        void SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer);
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler);
        void SetTexture(const uint32_t slot, RHI_Texture* texture);

        // Properties
        void* GetResource_DescriptorSetPool() const { return m_descriptor_pool; }
        void* GetResource_DescriptorSetLayout() const;
        bool GetResource_DescriptorSet(void*& descriptor_set);
        const std::vector<uint32_t>& GetDynamicOffsets() const;

        // Capacity
        bool HasEnoughCapacity() const;
        void GrowIfNeeded();

    private:
        uint32_t GetDescriptorSetCount() const;
        void SetDescriptorSetCapacity(uint32_t descriptor_capacity);
        bool CreateDescriptorPool(uint32_t descriptor_set_capacity);
        std::vector<RHI_Descriptor> GenerateDescriptors(RHI_PipelineState& pipeline_state);

        // Descriptor set layouts 
        std::unordered_map<std::size_t, std::shared_ptr<RHI_DescriptorSetLayout>> m_descriptor_set_layouts;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;

        // Descriptor pool
        uint32_t m_descriptor_set_capacity = 16;
        void* m_descriptor_pool = nullptr;

        // Dependencies
        const RHI_Device* m_rhi_device;
    };
}
