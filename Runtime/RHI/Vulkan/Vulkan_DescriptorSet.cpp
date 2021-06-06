/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===============================
#include "Spartan.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_DescriptorSetLayoutCache.h"
//==========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSet::~RHI_DescriptorSet()
    {

    }

    bool RHI_DescriptorSet::Create()
    {
        // Validate descriptor set
        SP_ASSERT(m_resource == nullptr);

        // Descriptor set layouts
        array<void*, 1> descriptor_set_layouts = { m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout()->GetResource() };

        // Allocate info
        VkDescriptorSetAllocateInfo allocate_info   = {};
        allocate_info.sType                         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool                = static_cast<VkDescriptorPool>(m_descriptor_set_layout_cache->GetResource_DescriptorPool());
        allocate_info.descriptorSetCount            = 1;
        allocate_info.pSetLayouts                   = reinterpret_cast<VkDescriptorSetLayout*>(descriptor_set_layouts.data());

        // Allocate
        if (!vulkan_utility::error::check(vkAllocateDescriptorSets(m_rhi_device->GetContextRhi()->device, &allocate_info, reinterpret_cast<VkDescriptorSet*>(&m_resource))))
            return false;

        // Name
        vulkan_utility::debug::set_name(*reinterpret_cast<VkDescriptorSet*>(&m_resource), m_object_name.c_str());

        return true;
    }

    void RHI_DescriptorSet::Update(const vector<RHI_Descriptor>& descriptors)
    {
        // Validate descriptor set
        SP_ASSERT(m_resource != nullptr);

        array<VkDescriptorImageInfo,    RHI_Context::descriptors_max> image_infos;
        array<VkDescriptorBufferInfo,   RHI_Context::descriptors_max> buffer_infos;
        array<VkWriteDescriptorSet,     RHI_Context::descriptors_max> write_descriptor_sets;
        uint8_t i = 0;

        for (const RHI_Descriptor& descriptor : descriptors)
        {
            // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
            if (!descriptor.resource)
                continue;

            // Sampler
            if (descriptor.type == RHI_Descriptor_Type::Sampler)
            {
                image_infos[i].sampler      = static_cast<VkSampler>(descriptor.resource);
                image_infos[i].imageView    = nullptr;
                image_infos[i].imageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            }
            // Sampled/Storage texture
            else if (descriptor.type == RHI_Descriptor_Type::Texture)
            {
                image_infos[i].sampler      = nullptr;
                image_infos[i].imageView    = static_cast<VkImageView>(descriptor.resource);
                image_infos[i].imageLayout  = descriptor.resource ? vulkan_image_layout[static_cast<uint8_t>(descriptor.layout)] : VK_IMAGE_LAYOUT_UNDEFINED;
            }
            // Constant/Uniform buffer
            else if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            {
                buffer_infos[i].buffer  = static_cast<VkBuffer>(descriptor.resource);
                buffer_infos[i].offset  = descriptor.offset;
                buffer_infos[i].range   = descriptor.range;
            }

            // Write descriptor set
            write_descriptor_sets[i].sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_descriptor_sets[i].pNext             = nullptr;
            write_descriptor_sets[i].dstSet            = static_cast<VkDescriptorSet>(m_resource);
            write_descriptor_sets[i].dstBinding        = descriptor.slot;
            write_descriptor_sets[i].dstArrayElement   = 0;
            write_descriptor_sets[i].descriptorCount   = 1;
            write_descriptor_sets[i].descriptorType    = vulkan_utility::ToVulkanDescriptorType(descriptor);
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
}
