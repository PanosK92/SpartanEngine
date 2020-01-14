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
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ====================
#include "../RHI_DescriptorSet.h"
#include "../RHI_Shader.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSet::~RHI_DescriptorSet()
    {
        if (m_descriptor_set_layout)
        {
            vkDestroyDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);
            m_descriptor_set_layout = nullptr;
        }

        if (m_descriptor_pool)
        {
            vkDestroyDescriptorPool(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
            m_descriptor_pool = nullptr;
        }
    }

    void RHI_DescriptorSet::DoubleCapacity()
    {
        // If the descriptor pool is full, re-allocate with double size
        if (m_descriptor_sets.size() < m_descriptor_capacity)
            return;

        m_descriptor_capacity *= 2;
        SetDescriptorCapacity(m_descriptor_capacity);
    }

    void RHI_DescriptorSet::SetDescriptorCapacity(uint32_t descriptor_set_capacity)
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi())
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        // Destroy layout
        if (m_descriptor_set_layout)
        {
            vkDestroyDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);
            m_descriptor_set_layout = nullptr;
        }

        // Destroy pool
        if (m_descriptor_set_layout)
        {
            vkDestroyDescriptorPool(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
            m_descriptor_pool = nullptr;
        }

        // Clear cache (as it holds sets belonging to the destroyed pool)
        m_descriptor_sets.clear();

        // Re-allocate everything with double size
        CreateDescriptorPool(descriptor_set_capacity);
        CreateDescriptorSetLayout();
    }

    bool RHI_DescriptorSet::CreateDescriptorPool(uint32_t descriptor_set_capacity)
    {
        // Pool sizes
        vector<VkDescriptorPoolSize> pool_sizes(4);
        pool_sizes[0].type              = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount   = m_max_constant_buffer;
        pool_sizes[1].type              = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        pool_sizes[1].descriptorCount   = m_max_constantbuffer_dynamic;
        pool_sizes[2].type              = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        pool_sizes[2].descriptorCount   = m_max_texture;
        pool_sizes[3].type              = VK_DESCRIPTOR_TYPE_SAMPLER;
        pool_sizes[3].descriptorCount   = m_max_sampler;

        // Create info
        VkDescriptorPoolCreateInfo pool_create_info = {};
        pool_create_info.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.flags          = 0;
        pool_create_info.poolSizeCount  = static_cast<uint32_t>(pool_sizes.size());
        pool_create_info.pPoolSizes     = pool_sizes.data();
        pool_create_info.maxSets        = descriptor_set_capacity;

        // Pool
        const auto descriptor_pool = reinterpret_cast<VkDescriptorPool*>(&m_descriptor_pool);
        if (!vulkan_common::error::check(vkCreateDescriptorPool(m_rhi_device->GetContextRhi()->device, &pool_create_info, nullptr, descriptor_pool)))
            return false;

        return true;
    }

    bool RHI_DescriptorSet::CreateDescriptorSetLayout()
    {
        // Layout bindings
        vector<VkDescriptorSetLayoutBinding> layout_bindings;
        {
            for (const RHI_Descriptor& descriptor_blueprint : m_descriptors)
            {
                // Stage flags
                VkShaderStageFlags stage_flags = 0;
                stage_flags |= (descriptor_blueprint.stage & Shader_Vertex)     ? VK_SHADER_STAGE_VERTEX_BIT    : 0;
                stage_flags |= (descriptor_blueprint.stage & Shader_Pixel)      ? VK_SHADER_STAGE_FRAGMENT_BIT  : 0;
                stage_flags |= (descriptor_blueprint.stage & Shader_Compute)    ? VK_SHADER_STAGE_COMPUTE_BIT   : 0;

                layout_bindings.push_back
                ({
                    descriptor_blueprint.slot,							// binding
                    vulkan_descriptor_type[descriptor_blueprint.type],	// descriptorType
                    1,										            // descriptorCount
                    stage_flags,							            // stageFlags
                    nullptr									            // pImmutableSamplers
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
        auto descriptor_set_layout = reinterpret_cast<VkDescriptorSetLayout*>(&m_descriptor_set_layout);
        if (!vulkan_common::error::check(vkCreateDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, &create_info, nullptr, descriptor_set_layout)))
            return false;

        string name = (m_shader_vertex ? m_shader_vertex->GetName() : "null") + "-" + (m_shader_pixel ? m_shader_pixel->GetName() : "null");
        vulkan_common::debug::set_descriptor_set_layout_name(m_rhi_device->GetContextRhi()->device, *descriptor_set_layout, name.c_str());

        return true;
    }

    void* RHI_DescriptorSet::CreateDescriptorSet(size_t hash)
    {
        // Early exit if the descriptor cache is full
        if (m_descriptor_sets.size() == m_descriptor_capacity)
            return nullptr;

        const auto descriptor_pool = static_cast<VkDescriptorPool>(m_descriptor_pool);
        auto descriptor_set_layout = static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout);

        // Allocate descriptor set
        VkDescriptorSet descriptor_set;
        {
            // Allocate info
            VkDescriptorSetAllocateInfo allocate_info   = {};
            allocate_info.sType                         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate_info.descriptorPool                = descriptor_pool;
            allocate_info.descriptorSetCount            = 1;
            allocate_info.pSetLayouts                   = &descriptor_set_layout;

            // Allocate		
            if (!vulkan_common::error::check(vkAllocateDescriptorSets(m_rhi_device->GetContextRhi()->device, &allocate_info, &descriptor_set)))
                return nullptr;

            string name = (m_shader_vertex ? m_shader_vertex->GetName() : "null") + "-" + (m_shader_pixel ? m_shader_pixel->GetName() : "null");
            vulkan_common::debug::set_descriptor_set_name(m_rhi_device->GetContextRhi()->device, descriptor_set, name.c_str());
        }

        // Update descriptor sets
        {
            uint32_t descriptor_count = static_cast<uint32_t>(m_descriptors.size());

            vector<VkDescriptorImageInfo> image_infos;
            image_infos.reserve(descriptor_count);

            vector<VkDescriptorBufferInfo> buffer_infos;
            buffer_infos.reserve(descriptor_count);

            vector<VkWriteDescriptorSet> write_descriptor_sets;
            write_descriptor_sets.reserve(descriptor_count);

            for (RHI_Descriptor& resource_blueprint : m_descriptors)
            {
                // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
                if (!resource_blueprint.resource)
                    continue;

                // Texture or Sampler
                image_infos.push_back
                ({
                    resource_blueprint.type == RHI_Descriptor_Sampler ? static_cast<VkSampler>(resource_blueprint.resource) : nullptr,      // sampler
                    resource_blueprint.type == RHI_Descriptor_Texture ? static_cast<VkImageView>(resource_blueprint.resource) : nullptr,    // imageView
                    vulkan_image_layout[resource_blueprint.layout]                                                                          // imageLayout
                });

                // Constant/Uniform buffer
                bool is_constant_buffer = resource_blueprint.type == RHI_Descriptor_ConstantBuffer || resource_blueprint.type == RHI_Descriptor_ConstantBufferDynamic;
                buffer_infos.push_back
                ({
                    is_constant_buffer ? static_cast<VkBuffer>(resource_blueprint.resource) : nullptr,  // buffer
                    uint64_t(0),                                                                        // offset
                    is_constant_buffer ? VK_WHOLE_SIZE : 0                                              // range                
                });

                write_descriptor_sets.push_back
                ({
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,	            // sType
                    nullptr,								            // pNext
                    descriptor_set,							            // dstSet
                    resource_blueprint.slot,				            // dstBinding
                    0,									                // dstArrayElement
                    1,									                // descriptorCount
                    vulkan_descriptor_type[resource_blueprint.type],	// descriptorType
                    &image_infos.back(),                                // pImageInfo 
                    &buffer_infos.back(),                               // pBufferInfo
                    nullptr									            // pTexelBufferView
                    });
            }

            vkUpdateDescriptorSets(m_rhi_device->GetContextRhi()->device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
        }

        void* descriptor = static_cast<void*>(descriptor_set);

        // Cache descriptor
        m_descriptor_sets[hash] = descriptor;

        return descriptor;
    }
}
#endif
