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

//= INCLUDES ==========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../Rendering/Renderer.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSetLayout::~RHI_DescriptorSetLayout()
    {
        if (m_resource)
        {
            RHI_Device::AddToDeletionQueue(RHI_Resource_Type::descriptor_set_layout, m_resource);
            m_resource = nullptr;
        }
    }

    void RHI_DescriptorSetLayout::CreateResource(const vector<RHI_Descriptor>& descriptors)
    {
        SP_ASSERT(m_resource == nullptr);

        // Layout bindings
        static const uint8_t descriptors_max = 255;
        static array<VkDescriptorSetLayoutBinding, descriptors_max> layout_bindings;
        static array<VkDescriptorBindingFlags, descriptors_max> layout_binding_flags;

        for (uint32_t i = 0; i < static_cast<uint32_t>(descriptors.size()); i++)
        {
            const RHI_Descriptor& descriptor = descriptors[i];

            // Stage flags
            VkShaderStageFlags stage_flags = 0;
            stage_flags |= (descriptor.stage & RHI_Shader_Vertex)  ? VK_SHADER_STAGE_VERTEX_BIT   : 0;
            stage_flags |= (descriptor.stage & RHI_Shader_Pixel)   ? VK_SHADER_STAGE_FRAGMENT_BIT : 0;
            stage_flags |= (descriptor.stage & RHI_Shader_Compute) ? VK_SHADER_STAGE_COMPUTE_BIT  : 0;

            layout_bindings[i].descriptorType     = vulkan_utility::to_vulkan_desscriptor_type(descriptor);
            layout_bindings[i].binding            = descriptor.slot;
            layout_bindings[i].descriptorCount    = descriptor.IsArray() ? descriptor.array_size : 1;
            layout_bindings[i].stageFlags         = stage_flags;
            layout_bindings[i].pImmutableSamplers = nullptr;

            // bindless support
            layout_binding_flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT; 
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flags_info = {};
        flags_info.sType                                          = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
        flags_info.pNext                                          = nullptr;
        flags_info.bindingCount                                   = static_cast<uint32_t>(descriptors.size());
        flags_info.pBindingFlags                                  = layout_binding_flags.data();

        // Create info
        VkDescriptorSetLayoutCreateInfo create_info = {};
        create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.flags                           = 0;
        create_info.pNext                           = &flags_info;
        create_info.bindingCount                    = static_cast<uint32_t>(descriptors.size());
        create_info.pBindings                       = layout_bindings.data();

        // Descriptor set layout
        SP_VK_ASSERT_MSG(
            vkCreateDescriptorSetLayout(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkDescriptorSetLayout*>(&m_resource)),
            "Failed to allocate descriptor set layout");

        // Name
        vulkan_utility::debug::set_object_name(static_cast<VkDescriptorSetLayout>(m_resource), m_object_name.c_str());
    }
}
