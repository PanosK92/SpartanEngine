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

//= INCLUDES ======================
#include "../Core/Spartan_Object.h"
#include <unordered_map>
#include <vector>
#include <array>
#include "RHI_Desctiptor.h"
//=================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_DescriptorSetLayout : public Spartan_Object
    {
    public:
        RHI_DescriptorSetLayout() = default;
        RHI_DescriptorSetLayout(const RHI_Device* rhi_device, const std::vector<RHI_Descriptor>& descriptors, const std::string& name);
        ~RHI_DescriptorSetLayout();

        bool SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer);
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler);
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const bool storage);

        bool GetResource_DescriptorSet(RHI_DescriptorCache* descriptor_cache, void*& descriptor_set);
        const std::array<uint32_t, rhi_max_constant_buffer_count> GetDynamicOffsets() const;
        uint32_t GetDynamicOffsetCount() const;
        void* GetResource_DescriptorSetLayout() const { return m_descriptor_set_layout; }      
        uint32_t GetDescriptorSetCount()        const { return static_cast<uint32_t>(m_descriptor_sets.size()); }
        void NeedsToBind()                            { m_needs_to_bind = true; }

    private:
        void* CreateDescriptorSet(const std::size_t hash, const RHI_DescriptorCache* descriptor_cache);
        void UpdateDescriptorSet(void* descriptor_set, const std::vector<RHI_Descriptor>& descriptors);
        void* CreateDescriptorSetLayout(const std::vector<RHI_Descriptor>& descriptors);

        // Misc
        bool m_needs_to_bind = false;
        std::array<uint32_t, rhi_max_constant_buffer_count> m_dynamic_offsets;

        // Descriptors
        std::vector<RHI_Descriptor> m_descriptors;

        // Descriptor sets
        std::unordered_map<std::size_t, void*> m_descriptor_sets;

        // Descriptor set layout
        void* m_descriptor_set_layout = nullptr;
        size_t m_descriptor_set_layout_hash = 0;

        // Dependencies
        const RHI_Device* m_rhi_device = nullptr;
    };
}
