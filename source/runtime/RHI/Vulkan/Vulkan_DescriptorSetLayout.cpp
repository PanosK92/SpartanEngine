/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ==========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Device.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    RHI_DescriptorSetLayout::~RHI_DescriptorSetLayout()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::DescriptorSetLayout, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_DescriptorSetLayout::CreateRhiResource()
    {
        SP_ASSERT(m_rhi_resource == nullptr);

        // filter descriptors - exclude push constants and bindless arrays
        vector<RHI_Descriptor> filtered;
        filtered.reserve(m_descriptors.size());
        for (const RHI_Descriptor& desc : m_descriptors)
        {
            if (desc.type == RHI_Descriptor_Type::PushConstantBuffer)
                continue;
            if (desc.as_array && desc.array_length == rhi_max_array_size)
                continue;
            filtered.push_back(desc);
        }

        if (filtered.empty())
        {
            // create empty layout
            VkDescriptorSetLayoutCreateInfo create_info = {};
            create_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            create_info.bindingCount = 0;
            create_info.pBindings    = nullptr;

            SP_ASSERT_VK(vkCreateDescriptorSetLayout(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkDescriptorSetLayout*>(&m_rhi_resource)));
            RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::DescriptorSetLayout, m_object_name.c_str());
            return;
        }

        // validate unique bindings
        {
            unordered_set<uint32_t> unique_bindings;
            for (const RHI_Descriptor& desc : filtered)
            {
                bool inserted = unique_bindings.insert(desc.slot).second;
                SP_ASSERT_MSG(inserted, "Duplicate binding slot detected");
            }
        }

        // build vulkan layout bindings
        constexpr uint32_t max_bindings = 255;
        array<VkDescriptorSetLayoutBinding, max_bindings> layout_bindings = {};
        array<VkDescriptorBindingFlags, max_bindings> binding_flags = {};

        for (size_t i = 0; i < filtered.size(); ++i)
        {
            const RHI_Descriptor& desc = filtered[i];

            // convert stage mask to vulkan flags
            VkShaderStageFlags stage_flags = 0;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::Vertex))        stage_flags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::Hull))          stage_flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::Domain))        stage_flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::Pixel))         stage_flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::Compute))       stage_flags |= VK_SHADER_STAGE_COMPUTE_BIT;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::RayGeneration)) stage_flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::RayMiss))       stage_flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
            if (desc.stage & rhi_shader_type_to_mask(RHI_Shader_Type::RayHit))        stage_flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

            auto& binding              = layout_bindings[i];
            binding.binding            = desc.slot;
            binding.descriptorType     = static_cast<VkDescriptorType>(RHI_Device::GetDescriptorType(desc));
            binding.descriptorCount    = desc.as_array ? desc.array_length : 1;
            binding.stageFlags         = stage_flags;
            binding.pImmutableSamplers = nullptr;

            binding_flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flags_info = {};
        flags_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
        flags_info.bindingCount  = static_cast<uint32_t>(filtered.size());
        flags_info.pBindingFlags = binding_flags.data();

        VkDescriptorSetLayoutCreateInfo create_info = {};
        create_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.pNext        = &flags_info;
        create_info.bindingCount = static_cast<uint32_t>(filtered.size());
        create_info.pBindings    = layout_bindings.data();

        SP_ASSERT_VK(vkCreateDescriptorSetLayout(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkDescriptorSetLayout*>(&m_rhi_resource)));
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::DescriptorSetLayout, m_object_name.c_str());
    }
}
