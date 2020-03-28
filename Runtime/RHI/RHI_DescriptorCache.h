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
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include "RHI_Object.h"
#include "RHI_Definition.h"
//=========================

namespace Spartan
{
    class SPARTAN_CLASS RHI_DescriptorCache : public RHI_Object
    {
    public:
        RHI_DescriptorCache(const std::shared_ptr<RHI_Device>& rhi_device);
        ~RHI_DescriptorCache();

        void Initialize(const std::vector<uint32_t>& constant_buffer_dynamic_slots, const RHI_Shader* shader_vertex, const RHI_Shader* shader_pixel = nullptr);

        // Descriptor resource updating
        void SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer);
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler);
        void SetTexture(const uint32_t slot, RHI_Texture* texture);

        // Capacity
        void GrowIfNeeded();

        // Misc
        void NeedsToBind() { m_needs_to_bind = true; }

        // Properties
        void* GetResource_DescriptorSet();
        void* GetResource_DescriptorSetLayout()             const { return m_descriptor_set_layout; }
        const std::vector<uint32_t>& GetDynamicOffsets()    const { return m_constant_buffer_dynamic_offsets; }

    private:
        void SetDescriptorCapacity(uint32_t descriptor_capacity);
        std::size_t GetDescriptorsHash(const std::vector<RHI_Descriptor>& descriptor_blueprint);
        bool CreateDescriptorPool(uint32_t descriptor_set_capacity);
        bool CreateDescriptorSetLayout();
        void* CreateDescriptorSet(std::size_t hash);
        void UpdateDescriptorSet(void* descriptor_set);
        void ReflectShaders(const RHI_Shader* shader_vertex, const RHI_Shader* shader_pixel = nullptr);

        // Dynamic constant buffers
        std::vector<uint32_t> m_constant_buffer_dynamic_slots;
        std::vector<uint32_t> m_constant_buffer_dynamic_offsets;

        // Descriptors
        bool m_needs_to_bind = false;
        std::vector<RHI_Descriptor> m_descriptors;

        // Descriptor sets
        uint32_t m_descriptor_set_capacity = 20;
        std::unordered_map<std::size_t, void*> m_descriptor_sets;

        // Descriptor set layout
        void* m_descriptor_set_layout = nullptr;

        // Descriptor pool
        void* m_descriptor_pool = nullptr;

        // Dependencies
        std::shared_ptr<RHI_Device> m_rhi_device;
    };
}
