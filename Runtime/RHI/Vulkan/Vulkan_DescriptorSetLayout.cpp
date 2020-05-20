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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ==========================
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_DescriptorCache.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
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

            vulkan_utility::debug::set_descriptor_set_name(*reinterpret_cast<VkDescriptorSet*>(&descriptor_set), m_name.c_str());
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

        const uint32_t descriptor_count = static_cast<uint32_t>(descriptors.size());
        
        vector<VkDescriptorImageInfo> image_infos;
        image_infos.reserve(descriptor_count);
        
        vector<VkDescriptorBufferInfo> buffer_infos;
        buffer_infos.reserve(descriptor_count);
        
        vector<VkWriteDescriptorSet> write_descriptor_sets;
        write_descriptor_sets.reserve(descriptor_count);
        
        for (const RHI_Descriptor& descriptor : descriptors)
        {
            // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
            if (!descriptor.resource)
                continue;
        
            // Texture or Sampler
            image_infos.push_back
            ({
                descriptor.type == RHI_Descriptor_Sampler ? static_cast<VkSampler>(descriptor.resource)     : nullptr,                                  // sampler
                descriptor.type == RHI_Descriptor_Texture ? static_cast<VkImageView>(descriptor.resource)   : nullptr,                                  // imageView
                descriptor.type == RHI_Descriptor_Texture && descriptor.resource ? vulkan_image_layout[descriptor.layout] : VK_IMAGE_LAYOUT_UNDEFINED   // imageLayout
            });
        
            // Constant/Uniform buffer
            const bool is_constant_buffer = descriptor.type == RHI_Descriptor_ConstantBuffer || descriptor.type == RHI_Descriptor_ConstantBufferDynamic;
            buffer_infos.push_back
            ({
                is_constant_buffer ? static_cast<VkBuffer>(descriptor.resource) : nullptr,  // buffer
                is_constant_buffer ? descriptor.offset  : 0,                                // offset
                is_constant_buffer ? descriptor.range   : 0                                 // range
            });
        
            write_descriptor_sets.push_back
            ({
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,	        // sType
                nullptr,								        // pNext
                static_cast<VkDescriptorSet>(descriptor_set),   // dstSet
                descriptor.slot,				                // dstBinding
                0,									            // dstArrayElement
                1,									            // descriptorCount
                vulkan_descriptor_type[descriptor.type],	    // descriptorType
                &image_infos.back(),                            // pImageInfo 
                &buffer_infos.back(),                           // pBufferInfo
                nullptr									        // pTexelBufferView
            });
        }
        
        vkUpdateDescriptorSets(m_rhi_device->GetContextRhi()->device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
    }

    void* RHI_DescriptorSetLayout::CreateDescriptorSetLayout(const vector<RHI_Descriptor>& descriptors)
    {
        // Layout bindings
        vector<VkDescriptorSetLayoutBinding> layout_bindings;
        layout_bindings.reserve(descriptors.size());
        {
            for (const RHI_Descriptor& descriptor : descriptors)
            {
                // Stage flags
                VkShaderStageFlags stage_flags = 0;
                stage_flags |= (descriptor.stage & RHI_Shader_Vertex)   ? VK_SHADER_STAGE_VERTEX_BIT    : 0;
                stage_flags |= (descriptor.stage & RHI_Shader_Pixel)    ? VK_SHADER_STAGE_FRAGMENT_BIT  : 0;
                stage_flags |= (descriptor.stage & RHI_Shader_Compute)  ? VK_SHADER_STAGE_COMPUTE_BIT   : 0;

                layout_bindings.push_back
                ({
                    descriptor.slot,							// binding
                    vulkan_descriptor_type[descriptor.type],	// descriptorType
                    1,										    // descriptorCount
                    stage_flags,							    // stageFlags
                    nullptr									    // pImmutableSamplers
                    });
            }
        }

        // Create info
        VkDescriptorSetLayoutCreateInfo create_info = {};
        create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.flags                           = 0;
        create_info.pNext                           = nullptr;
        create_info.bindingCount                    = static_cast<uint32_t>(layout_bindings.size());
        create_info.pBindings                       = layout_bindings.data();

        // Descriptor set layout
        void* descriptor_set_layout = nullptr;
        if (!vulkan_utility::error::check(vkCreateDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, &create_info, nullptr, reinterpret_cast<VkDescriptorSetLayout*>(&descriptor_set_layout))))
            return false;

        vulkan_utility::debug::set_descriptor_set_layout_name(static_cast<VkDescriptorSetLayout>(descriptor_set_layout), m_name.c_str());

        return descriptor_set_layout;
    }
}
#endif
