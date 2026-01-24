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

//= INCLUDES ============================
#include "pch.h"
#include "../RHI_Device.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Implementation.h"
#include "../RHI_Buffer.h"
#include "../RHI_AccelerationStructure.h"
#include "../Rendering/Renderer.h"
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // scratch space for descriptor writes - avoids repeated allocations
        constexpr uint32_t max_descriptors = 256;

        struct DescriptorWriteContext
        {
            array<VkWriteDescriptorSet, max_descriptors> writes = {};
            array<VkDescriptorImageInfo, max_descriptors> images = {};
            array<VkDescriptorBufferInfo, max_descriptors> buffers = {};
            array<VkWriteDescriptorSetAccelerationStructureKHR, max_descriptors> accel_structs = {};
            array<VkAccelerationStructureKHR, max_descriptors> accel_handles = {};

            uint32_t write_count  = 0;
            uint32_t image_count  = 0;
            uint32_t buffer_count = 0;
            uint32_t accel_count  = 0;

            void Reset()
            {
                write_count  = 0;
                image_count  = 0;
                buffer_count = 0;
                accel_count  = 0;
            }
        };

        thread_local DescriptorWriteContext g_ctx;

        void* get_fallback_srv()
        {
            if (RHI_Texture* tex = Renderer::GetStandardTexture(Renderer_StandardTexture::Checkerboard))
            {
                return tex->GetRhiSrv();
            }
            return nullptr;
        }

        void add_image_write(
            VkDescriptorSet dst_set,
            uint32_t binding,
            VkDescriptorType type,
            VkImageView view,
            VkImageLayout layout,
            uint32_t count = 1)
        {
            uint32_t image_idx = g_ctx.image_count++;
            g_ctx.images[image_idx].sampler     = nullptr;
            g_ctx.images[image_idx].imageView   = view;
            g_ctx.images[image_idx].imageLayout = layout;

            uint32_t write_idx = g_ctx.write_count++;
            auto& write = g_ctx.writes[write_idx];
            write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext            = nullptr;
            write.dstSet           = dst_set;
            write.dstBinding       = binding;
            write.dstArrayElement  = 0;
            write.descriptorCount  = count;
            write.descriptorType   = type;
            write.pImageInfo       = &g_ctx.images[image_idx];
            write.pBufferInfo      = nullptr;
            write.pTexelBufferView = nullptr;
        }

        void add_buffer_write(
            VkDescriptorSet dst_set,
            uint32_t binding,
            VkDescriptorType type,
            VkBuffer buffer,
            uint64_t range)
        {
            uint32_t buffer_idx = g_ctx.buffer_count++;
            g_ctx.buffers[buffer_idx].buffer = buffer;
            g_ctx.buffers[buffer_idx].offset = 0;
            g_ctx.buffers[buffer_idx].range  = range;

            uint32_t write_idx = g_ctx.write_count++;
            auto& write = g_ctx.writes[write_idx];
            write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext            = nullptr;
            write.dstSet           = dst_set;
            write.dstBinding       = binding;
            write.dstArrayElement  = 0;
            write.descriptorCount  = 1;
            write.descriptorType   = type;
            write.pImageInfo       = nullptr;
            write.pBufferInfo      = &g_ctx.buffers[buffer_idx];
            write.pTexelBufferView = nullptr;
        }

        void add_accel_struct_write(
            VkDescriptorSet dst_set,
            uint32_t binding,
            VkAccelerationStructureKHR accel)
        {
            uint32_t accel_idx = g_ctx.accel_count++;
            g_ctx.accel_handles[accel_idx] = accel;

            auto& info = g_ctx.accel_structs[accel_idx];
            info.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            info.pNext                      = nullptr;
            info.accelerationStructureCount = 1;
            info.pAccelerationStructures    = &g_ctx.accel_handles[accel_idx];

            uint32_t write_idx = g_ctx.write_count++;
            auto& write = g_ctx.writes[write_idx];
            write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext            = &g_ctx.accel_structs[accel_idx];
            write.dstSet           = dst_set;
            write.dstBinding       = binding;
            write.dstArrayElement  = 0;
            write.descriptorCount  = 1;
            write.descriptorType   = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            write.pImageInfo       = nullptr;
            write.pBufferInfo      = nullptr;
            write.pTexelBufferView = nullptr;
        }
    }

    void RHI_DescriptorSet::Update(const vector<RHI_DescriptorWithBinding>& descriptors)
    {
        m_descriptors = descriptors;
        SP_ASSERT(m_resource != nullptr);

        VkDescriptorSet vk_set = static_cast<VkDescriptorSet>(m_resource);
        void* fallback_srv = get_fallback_srv();

        g_ctx.Reset();

        for (const RHI_DescriptorWithBinding& desc : descriptors)
        {
            const RHI_Descriptor& layout = desc.descriptor;
            const RHI_DescriptorBinding& binding = desc.binding;

            // skip unbound descriptors (except images which use fallback)
            if (!binding.IsBound() && layout.type != RHI_Descriptor_Type::Image)
                continue;

            // skip bindless arrays - they have their own descriptor sets
            if (layout.as_array && layout.array_length == rhi_max_array_size)
                continue;

            VkDescriptorType vk_type = static_cast<VkDescriptorType>(RHI_Device::GetDescriptorType(layout));

            switch (layout.type)
            {
                case RHI_Descriptor_Type::Image:
                case RHI_Descriptor_Type::TextureStorage:
                {
                    RHI_Texture* texture = static_cast<RHI_Texture*>(binding.resource);
                    bool mip_specified = binding.mip != rhi_all_mips;

                    if (!layout.as_array)
                    {
                        // single texture binding
                        void* view = nullptr;
                        VkImageLayout img_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                        if (texture)
                        {
                            view = mip_specified ? texture->GetRhiSrvMip(binding.mip) : texture->GetRhiSrv();
                            img_layout = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(mip_specified ? binding.mip : 0))];
                        }
                        else if (layout.type == RHI_Descriptor_Type::Image)
                        {
                            view = fallback_srv;
                            img_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        }

                        if (view)
                        {
                            add_image_write(vk_set, layout.slot, vk_type, static_cast<VkImageView>(view), img_layout);
                        }
                    }
                    else
                    {
                        // array of mip views
                        uint32_t mip_start = mip_specified ? binding.mip : 0;
                        uint32_t mip_count = binding.mip_range > 0 ? binding.mip_range : 1;
                        uint32_t first_image_idx = g_ctx.image_count;

                        for (uint32_t m = 0; m < mip_count; ++m)
                        {
                            uint32_t mip_idx = mip_start + m;
                            void* view = texture ? texture->GetRhiSrvMip(mip_idx) : fallback_srv;
                            VkImageLayout img_layout = texture
                                ? vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(mip_idx))]
                                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                            uint32_t idx = g_ctx.image_count++;
                            g_ctx.images[idx].sampler     = nullptr;
                            g_ctx.images[idx].imageView   = static_cast<VkImageView>(view);
                            g_ctx.images[idx].imageLayout = img_layout;
                        }

                        uint32_t write_idx = g_ctx.write_count++;
                        auto& write = g_ctx.writes[write_idx];
                        write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.pNext            = nullptr;
                        write.dstSet           = vk_set;
                        write.dstBinding       = layout.slot;
                        write.dstArrayElement  = 0;
                        write.descriptorCount  = mip_count;
                        write.descriptorType   = vk_type;
                        write.pImageInfo       = &g_ctx.images[first_image_idx];
                        write.pBufferInfo      = nullptr;
                        write.pTexelBufferView = nullptr;
                    }
                    break;
                }

                case RHI_Descriptor_Type::ConstantBuffer:
                case RHI_Descriptor_Type::StructuredBuffer:
                {
                    RHI_Buffer* buffer = static_cast<RHI_Buffer*>(binding.resource);
                    if (buffer && buffer->GetRhiResource())
                    {
                        add_buffer_write(
                            vk_set,
                            layout.slot,
                            vk_type,
                            static_cast<VkBuffer>(buffer->GetRhiResource()),
                            binding.range
                        );
                    }
                    break;
                }

                case RHI_Descriptor_Type::AccelerationStructure:
                {
                    RHI_AccelerationStructure* tlas = static_cast<RHI_AccelerationStructure*>(binding.resource);
                    if (tlas && tlas->GetRhiResource())
                    {
                        add_accel_struct_write(
                            vk_set,
                            layout.slot,
                            static_cast<VkAccelerationStructureKHR>(tlas->GetRhiResource())
                        );
                    }
                    else
                    {
                        SP_LOG_WARNING("Acceleration structure is null or invalid, skipping descriptor update");
                    }
                    break;
                }

                default:
                    SP_ASSERT_MSG(false, "Unhandled descriptor type");
                    break;
            }
        }

        if (g_ctx.write_count > 0)
        {
            vkUpdateDescriptorSets(
                RHI_Context::device,
                g_ctx.write_count,
                g_ctx.writes.data(),
                0,
                nullptr
            );
        }
    }
}
