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
#include "../RHI_Sampler.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_StructuredBuffer.h"
//==========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static void* resource_from_descriptor(const RHI_Descriptor& descriptor)
    {
        if (!descriptor.data)
            return nullptr;

        if (descriptor.type == RHI_Descriptor_Type::Sampler)
        {
            return static_cast<RHI_Sampler*>(descriptor.data)->GetResource();
        }
        else if (descriptor.type == RHI_Descriptor_Type::Texture || descriptor.type == RHI_Descriptor_Type::TextureStorage)
        {
            RHI_Texture* texture = static_cast<RHI_Texture*>(descriptor.data);
            bool set_individual_mip = descriptor.mip != -1;
            return set_individual_mip ? texture->Get_Resource_Views_Srv(descriptor.mip) : texture->Get_Resource_View_Srv();
        }
        else if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
        {
            return static_cast<RHI_ConstantBuffer*>(descriptor.data)->GetResource();
        }
        else if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
        {
            return static_cast<RHI_StructuredBuffer*>(descriptor.data)->GetResource();
        }

        return nullptr;
    }

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

        vector<VkDescriptorImageInfo> info_images;
        info_images.resize(RHI_Context::descriptors_max);
        info_images.reserve(RHI_Context::descriptors_max);
        int image_index = -1;

        vector<VkDescriptorBufferInfo> info_buffers;
        info_buffers.resize(RHI_Context::descriptors_max);
        info_buffers.reserve(RHI_Context::descriptors_max);

        array<VkWriteDescriptorSet, RHI_Context::descriptors_max> descriptor_sets;
        uint32_t index = 0;

        for (const RHI_Descriptor & descriptor : descriptors)
        {
            // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
            if (void* resource = resource_from_descriptor(descriptor))
            {
                bool set_individual_mip         = descriptor.mip != -1;
                bool is_array_of_textures       = descriptor.array_size > 1;
                uint32_t descriptor_count       = 0;
                uint32_t descriptor_index_start = 0;
                uint32_t array_size             = 1;

                if (descriptor.type == RHI_Descriptor_Type::Sampler)
                {
                    image_index++;

                    info_images[image_index].sampler        = static_cast<VkSampler>(resource);
                    info_images[image_index].imageView      = nullptr;
                    info_images[image_index].imageLayout    = VK_IMAGE_LAYOUT_UNDEFINED;

                    descriptor_count++;
                    descriptor_index_start = image_index;
                }
                else if (descriptor.type == RHI_Descriptor_Type::Texture || descriptor.type == RHI_Descriptor_Type::TextureStorage)
                {
                    RHI_Texture* texture = static_cast<RHI_Texture*>(descriptor.data);

                    if (!is_array_of_textures)
                    {
                        image_index++;

                        info_images[image_index].sampler        = nullptr;
                        info_images[image_index].imageView      = static_cast<VkImageView>(resource);
                        info_images[image_index].imageLayout    = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(set_individual_mip ? descriptor.mip : 0))];

                        descriptor_count++;
                        descriptor_index_start = image_index;
                    }
                    else // array of textures (not a Texture2DArray)
                    {
                        for (uint32_t mip_index = descriptor.mip; mip_index < descriptor.array_size; mip_index++)
                        {
                            image_index++;

                            info_images[image_index].sampler        = nullptr;
                            info_images[image_index].imageView      = static_cast<VkImageView>(texture->Get_Resource_Views_Srv(mip_index));
                            info_images[image_index].imageLayout    = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(mip_index))];

                            descriptor_count++;

                            if (mip_index == descriptor.mip)
                            {
                                descriptor_index_start = image_index;
                            }
                        }

                        array_size = descriptor.array_size - descriptor.mip;
                    }
                }
                else if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
                {
                    info_buffers[index].buffer  = static_cast<VkBuffer>(resource);
                    info_buffers[index].offset  = descriptor.offset;
                    info_buffers[index].range   = descriptor.range;

                    descriptor_count++;
                    descriptor_index_start = index;
                }
                else if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
                {
                    info_buffers[index].buffer  = static_cast<VkBuffer>(resource);
                    info_buffers[index].offset  = descriptor.offset;
                    info_buffers[index].range   = descriptor.range;

                    descriptor_count++;
                    descriptor_index_start = index;
                }

                // Write descriptor set
                descriptor_sets[index].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptor_sets[index].pNext            = nullptr;
                descriptor_sets[index].dstSet           = static_cast<VkDescriptorSet>(m_resource);
                descriptor_sets[index].dstBinding       = descriptor.slot;
                descriptor_sets[index].dstArrayElement  = 0;
                descriptor_sets[index].descriptorCount  = array_size;
                descriptor_sets[index].descriptorType   = vulkan_utility::ToVulkanDescriptorType(descriptor);
                descriptor_sets[index].pImageInfo       = &info_images[descriptor_index_start];
                descriptor_sets[index].pBufferInfo      = &info_buffers[descriptor_index_start];
                descriptor_sets[index].pTexelBufferView = nullptr;

                index++;
            }
        }

        vkUpdateDescriptorSets(
            m_rhi_device->GetContextRhi()->device,  // device
            index,                                  // descriptorWriteCount
            descriptor_sets.data(),                 // pDescriptorWrites
            0,                                      // descriptorCopyCount
            nullptr                                 // pDescriptorCopies
        );
    }
}
