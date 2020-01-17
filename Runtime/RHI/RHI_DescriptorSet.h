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
#include "RHI_Definition.h"
#include <vector>
#include <map>
#include <memory>
//=========================

namespace Spartan
{
    class RHI_DescriptorSet
    {
    public:
        RHI_DescriptorSet(const std::shared_ptr<RHI_Device> rhi_device, std::vector<uint32_t> constant_buffer_dynamic_slots, const RHI_Shader* shader_vertex, const RHI_Shader* shader_pixel = nullptr);
        ~RHI_DescriptorSet();

        // Descriptor resource updating
        void SetConstantBuffer(uint32_t slot, RHI_ConstantBuffer* constant_buffer);
        void SetSampler(uint32_t slot, RHI_Sampler* sampler);
        void SetTexture(uint32_t slot, RHI_Texture* texture);

        // Capacity
        void DoubleCapacity();
        bool HashEnoughCapacity() { return m_descriptor_sets.size() < m_descriptor_capacity; }

        // Misc
        void MakeDirty() { m_needs_to_bind = true; }

        // Properties
        void* GetResource_Set();
        void* GetResource_Layout()                          const { return m_descriptor_set_layout; }
        const std::vector<uint32_t>& GetDynamicOffsets()    const { return m_constant_buffer_dynamic_offsets; }

    private:
        void SetDescriptorCapacity(uint32_t descriptor_capacity);
        std::size_t GetDescriptorBlueprintHash(const std::vector<RHI_Descriptor>& descriptor_blueprint);
        bool CreateDescriptorPool(uint32_t descriptor_set_capacity);
        bool CreateDescriptorSetLayout();
        void* CreateDescriptorSet(std::size_t hash);
        void UpdateDescriptorSet(void* descriptor_set);
        void ReflectShaders();

        // Descriptors
        const uint32_t m_max_constant_buffer        = 10;
        const uint32_t m_max_constantbuffer_dynamic = 10;
        const uint32_t m_max_sampler                = 10;
        const uint32_t m_max_texture                = 10;
        uint32_t m_descriptor_capacity              = 20;
        bool m_needs_to_bind                        = false;
        std::vector<RHI_Descriptor> m_descriptors;

        // Dynamic constant buffers
        std::vector<uint32_t> m_constant_buffer_dynamic_slots;
        std::vector<uint32_t> m_constant_buffer_dynamic_offsets;

        // Dependencies
        std::shared_ptr<RHI_Device> m_rhi_device;
        const RHI_Shader* m_shader_vertex   = nullptr;
        const RHI_Shader* m_shader_pixel    = nullptr;

		// API
		void* m_descriptor_pool         = nullptr;
		void* m_descriptor_set_layout   = nullptr;
        std::map<std::size_t, void*> m_descriptor_sets;
    };
}
