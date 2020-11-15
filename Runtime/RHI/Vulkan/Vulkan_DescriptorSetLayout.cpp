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

//= INCLUDES ==========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_DescriptorCache.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static VkDescriptorType GetDescriptorType(const RHI_Descriptor& descriptor)
    {
        if (descriptor.type == RHI_Descriptor_ConstantBuffer)
        {
            return descriptor.is_dynamic_constant_buffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
        else if (descriptor.type == RHI_Descriptor_Texture)
        {
            return descriptor.is_storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        }
        else if (descriptor.type == RHI_Descriptor_Sampler)
        {
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        }

        LOG_ERROR("Invalid descriptor type");
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    RHI_DescriptorSetLayout::~RHI_DescriptorSetLayout()
    {
        if (m_descriptor_set_layout)
        {
            vkDestroyDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);
            m_descriptor_set_layout = nullptr;
        }
    }

    void* RHI_DescriptorSetLayout::CreateDescriptorSet(const size_t hash, const RHI_DescriptorCache* descriptor_cache)
    {
        // Allocate descriptor set
        void* descriptor_set = nullptr;
        {
            // Allocate info
            VkDescriptorSetAllocateInfo allocate_info   = {};
            allocate_info.sType                         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate_info.descriptorPool                = static_cast<VkDescriptorPool>(descriptor_cache->GetResource_DescriptorSetPool());
            allocate_info.descriptorSetCount            = 1;
            allocate_info.pSetLayouts                   = reinterpret_cast<VkDescriptorSetLayout*>(&m_descriptor_set_layout);

            // Allocate        
            if (!vulkan_utility::error::check(vkAllocateDescriptorSets(m_rhi_device->GetContextRhi()->device, &allocate_info, reinterpret_cast<VkDescriptorSet*>(&descriptor_set))))
                return nullptr;

            vulkan_utility::debug::set_name(*reinterpret_cast<VkDescriptorSet*>(&descriptor_set), m_name.c_str());
        }

        UpdateDescriptorSet(descriptor_set, m_descriptors);

        // Cache descriptor
        m_descriptor_sets[hash] = descriptor_set;

        return descriptor_set;
    }

    void RHI_DescriptorSetLayout::UpdateDescriptorSet(void* descriptor_set, const vector<RHI_Descriptor>& descriptors)
    {
        if (!descriptor_set)
            return;

        static const uint8_t descriptors_max = 255;
        array<VkDescriptorImageInfo,    descriptors_max> image_infos;
        array<VkDescriptorBufferInfo,   descriptors_max> buffer_infos;
        array<VkWriteDescriptorSet,     descriptors_max> write_descriptor_sets;
        uint8_t i = 0;

        for (const RHI_Descriptor& descriptor : descriptors)
        {
            // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
            if (!descriptor.resource)
                continue;

            // Sampler
            if (descriptor.type == RHI_Descriptor_Sampler)
            {
                image_infos[i].sampler      = static_cast<VkSampler>(descriptor.resource);
                image_infos[i].imageView    = nullptr;
                image_infos[i].imageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            }
            // Sampled/Storage texture
            else if (descriptor.type == RHI_Descriptor_Texture)
            {
                image_infos[i].sampler      = nullptr;
                image_infos[i].imageView    = static_cast<VkImageView>(descriptor.resource);
                image_infos[i].imageLayout  = descriptor.resource ? vulkan_image_layout[static_cast<uint8_t>(descriptor.layout)] : VK_IMAGE_LAYOUT_UNDEFINED;
            }
            // Constant/Uniform buffer
            else if (descriptor.type == RHI_Descriptor_ConstantBuffer)
            {
                buffer_infos[i].buffer  = static_cast<VkBuffer>(descriptor.resource);
                buffer_infos[i].offset  = descriptor.offset;
                buffer_infos[i].range   = descriptor.range;
            }

            // Write descriptor set
            write_descriptor_sets[i].sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_descriptor_sets[i].pNext             = nullptr;
            write_descriptor_sets[i].dstSet            = static_cast<VkDescriptorSet>(descriptor_set);
            write_descriptor_sets[i].dstBinding        = descriptor.slot;
            write_descriptor_sets[i].dstArrayElement   = 0;
            write_descriptor_sets[i].descriptorCount   = 1;
            write_descriptor_sets[i].descriptorType    = GetDescriptorType(descriptor);
            write_descriptor_sets[i].pImageInfo        = &image_infos[i];
            write_descriptor_sets[i].pBufferInfo       = &buffer_infos[i];
            write_descriptor_sets[i].pTexelBufferView  = nullptr;

            i++;
        }

        vkUpdateDescriptorSets(
            m_rhi_device->GetContextRhi()->device,  // device
            static_cast<uint32_t>(i),               // descriptorWriteCount
            write_descriptor_sets.data(),           // pDescriptorWrites
            0,                                      // descriptorCopyCount
            nullptr                                 // pDescriptorCopies
        );
    }

    void* RHI_DescriptorSetLayout::CreateDescriptorSetLayout(const vector<RHI_Descriptor>& descriptors)
    {
        // Layout bindings
        static const uint8_t descriptors_max = 255;
        array<VkDescriptorSetLayoutBinding, descriptors_max> layout_bindings;
        uint8_t i = 0;

        for (const RHI_Descriptor& descriptor : descriptors)
        {
            // Stage flags
            VkShaderStageFlags stage_flags = 0;
            stage_flags |= (descriptor.stage & RHI_Shader_Vertex)   ? VK_SHADER_STAGE_VERTEX_BIT    : 0;
            stage_flags |= (descriptor.stage & RHI_Shader_Pixel)    ? VK_SHADER_STAGE_FRAGMENT_BIT  : 0;
            stage_flags |= (descriptor.stage & RHI_Shader_Compute)  ? VK_SHADER_STAGE_COMPUTE_BIT   : 0;
        
            layout_bindings[i].binding              = descriptor.slot;
            layout_bindings[i].descriptorType       = GetDescriptorType(descriptor);
            layout_bindings[i].descriptorCount      = 1;
            layout_bindings[i].stageFlags           = stage_flags;
            layout_bindings[i].pImmutableSamplers   = nullptr;
        
            i++;
        }

        // Create info
        VkDescriptorSetLayoutCreateInfo create_info = {};
        create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.flags                           = 0;
        create_info.pNext                           = nullptr;
        create_info.bindingCount                    = static_cast<uint32_t>(i);
        create_info.pBindings                       = layout_bindings.data();

        // Descriptor set layout
        void* descriptor_set_layout = nullptr;
        if (!vulkan_utility::error::check(vkCreateDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, &create_info, nullptr, reinterpret_cast<VkDescriptorSetLayout*>(&descriptor_set_layout))))
            return false;

        vulkan_utility::debug::set_name(static_cast<VkDescriptorSetLayout>(descriptor_set_layout), m_name.c_str());

        return descriptor_set_layout;
    }
}
