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

//= INCLUDES ==========================
#include "Runtime/Core/Spartan.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Sampler.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_StructuredBuffer.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_DescriptorSet::Create(RHI_DescriptorSetLayout* descriptor_set_layout)
    {
        // Validate descriptor set
        SP_ASSERT(m_resource == nullptr);

        // Descriptor set layouts
        array<void*, 1> descriptor_set_layouts = { descriptor_set_layout->GetResource() };

        // Allocate info
        VkDescriptorSetAllocateInfo allocate_info = {};
        allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool              = static_cast<VkDescriptorPool>(m_rhi_device->GetDescriptorPool());
        allocate_info.descriptorSetCount          = 1;
        allocate_info.pSetLayouts                 = reinterpret_cast<VkDescriptorSetLayout*>(descriptor_set_layouts.data());

        // Allocate
        bool allocated = vulkan_utility::error::check(vkAllocateDescriptorSets(m_rhi_device->GetContextRhi()->device, &allocate_info, reinterpret_cast<VkDescriptorSet*>(&m_resource)));
        SP_ASSERT(allocated && "Failed to allocate descriptor set.");

        // Name
        vulkan_utility::debug::set_name(*reinterpret_cast<VkDescriptorSet*>(&m_resource), m_object_name.c_str());
    }

    void RHI_DescriptorSet::Update(const vector<RHI_Descriptor>& descriptors)
    {
        // Validate descriptor set
        SP_ASSERT(m_resource != nullptr);

        const uint32_t descriptor_count = 256;

        vector<VkDescriptorImageInfo> info_images;
        info_images.resize(descriptor_count);
        info_images.reserve(descriptor_count);
        int image_index = -1;

        vector<VkDescriptorBufferInfo> info_buffers;
        info_buffers.resize(descriptor_count);
        info_buffers.reserve(descriptor_count);

        array<VkWriteDescriptorSet, descriptor_count> descriptor_sets;
        uint32_t index = 0;

        for (const RHI_Descriptor& descriptor : descriptors)
        {
            // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
            if (!descriptor.data)
                continue;

            uint32_t descriptor_index_start = 0;
            uint32_t descriptor_count       = 1;

            if (descriptor.type == RHI_Descriptor_Type::Sampler)
            {
                image_index++;

                info_images[image_index].sampler     = static_cast<VkSampler>(static_cast<RHI_Sampler*>(descriptor.data)->GetResource());
                info_images[image_index].imageView   = nullptr;
                info_images[image_index].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                descriptor_index_start = image_index;
            }
            else if (descriptor.type == RHI_Descriptor_Type::Texture || descriptor.type == RHI_Descriptor_Type::TextureStorage)
            {
                RHI_Texture* texture     = static_cast<RHI_Texture*>(descriptor.data);
                const bool mip_specified = descriptor.mip != rhi_all_mips;
                uint32_t mip_start       = mip_specified ? descriptor.mip : 0;

                if (!descriptor.IsArray())
                {
                    image_index++;

                    void* resource = mip_specified ? texture->GetResource_Views_Srv(descriptor.mip) : texture->GetResource_View_Srv();

                    info_images[image_index].sampler     = nullptr;
                    info_images[image_index].imageView   = static_cast<VkImageView>(resource);
                    info_images[image_index].imageLayout = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(mip_start))];

                    descriptor_index_start = image_index;
                }
                else // bind mips as an array of textures (not a Texture2DArray)
                {
                    for (uint32_t mip_index = mip_start; mip_index < mip_start + descriptor.mip_range; mip_index++)
                    {
                        image_index++;

                        info_images[image_index].sampler     = nullptr;
                        info_images[image_index].imageView   = static_cast<VkImageView>(texture->GetResource_Views_Srv(mip_index));
                        info_images[image_index].imageLayout = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(mip_index))];

                        if (mip_index == descriptor.mip)
                        {
                            descriptor_index_start = image_index;
                        }
                    }

                    descriptor_count = descriptor.mip_range;
                }
            }
            else if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            {
                info_buffers[index].buffer = static_cast<VkBuffer>(static_cast<RHI_ConstantBuffer*>(descriptor.data)->GetResource());
                info_buffers[index].offset = 0;
                info_buffers[index].range  = descriptor.range;

                descriptor_index_start = index;
            }
            else if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
            {
                info_buffers[index].buffer = static_cast<VkBuffer>(static_cast<RHI_StructuredBuffer*>(descriptor.data)->GetResource());
                info_buffers[index].offset = 0;
                info_buffers[index].range  = descriptor.range;

                descriptor_index_start = index;
            }

            // Write descriptor set
            descriptor_sets[index].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_sets[index].pNext            = nullptr;
            descriptor_sets[index].dstSet           = static_cast<VkDescriptorSet>(m_resource);
            descriptor_sets[index].dstBinding       = descriptor.slot;
            descriptor_sets[index].dstArrayElement  = 0; // The starting element in that array
            descriptor_sets[index].descriptorCount  = descriptor_count;
            descriptor_sets[index].descriptorType   = vulkan_utility::ToVulkanDescriptorType(descriptor);
            descriptor_sets[index].pImageInfo       = &info_images[descriptor_index_start];
            descriptor_sets[index].pBufferInfo      = &info_buffers[descriptor_index_start];
            descriptor_sets[index].pTexelBufferView = nullptr;

            index++;
        }

        vkUpdateDescriptorSets(
            m_rhi_device->GetContextRhi()->device, // device
            index,                                 // descriptorWriteCount
            descriptor_sets.data(),                // pDescriptorWrites
            0,                                     // descriptorCopyCount
            nullptr                                // pDescriptorCopies
        );
    }
}
