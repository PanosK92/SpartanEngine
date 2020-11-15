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

//= INCLUDES =======================
#include "Spartan.h"
#include "RHI_DescriptorSetLayout.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_Sampler.h"
#include "RHI_Texture.h"
#include "RHI_Implementation.h"
#include "RHI_DescriptorCache.h"
#include "../Utilities/Hash.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSetLayout::RHI_DescriptorSetLayout(const RHI_Device* rhi_device, const std::vector<RHI_Descriptor>& descriptors, const string& name)
    {
        m_rhi_device            = rhi_device;
        m_descriptors           = descriptors;
        m_name                  = name;
        m_descriptor_set_layout = CreateDescriptorSetLayout(m_descriptors);
        m_dynamic_offsets.fill(rhi_dynamic_offset_empty);

        for (const RHI_Descriptor& descriptor : descriptors)
        {
            Utility::Hash::hash_combine(m_descriptor_set_layout_hash, descriptor.GetHash());
        }
    }

    bool RHI_DescriptorSetLayout::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if ((descriptor.type == RHI_Descriptor_ConstantBuffer) && descriptor.slot == slot + rhi_shader_shift_buffer)
            {
                // Determine if the descriptor set needs to bind
                m_needs_to_bind = descriptor.resource   != constant_buffer->GetResource()   ? true : m_needs_to_bind; // affects vkUpdateDescriptorSets
                m_needs_to_bind = descriptor.offset     != constant_buffer->GetOffset()     ? true : m_needs_to_bind; // affects vkUpdateDescriptorSets
                m_needs_to_bind = descriptor.range      != constant_buffer->GetStride()     ? true : m_needs_to_bind; // affects vkUpdateDescriptorSets

                // Keep track of dynamic offsets
                if (constant_buffer->IsDynamic())
                {
                    const uint32_t dynamic_offset = constant_buffer->GetOffsetDynamic();

                    if (m_dynamic_offsets[slot] != dynamic_offset)
                    {
                        m_dynamic_offsets[slot] = dynamic_offset;
                        m_needs_to_bind = true; // affects vkCmdBindDescriptorSets
                    }
                }

                // Update
                descriptor.resource = constant_buffer->GetResource();
                descriptor.offset   = constant_buffer->GetOffset();
                descriptor.range    = constant_buffer->GetStride();

                return true;
            }
        }

        return false;
    }

    void RHI_DescriptorSetLayout::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Sampler && descriptor.slot == slot + rhi_shader_shift_sampler)
            {
                // Determine if the descriptor set needs to bind
                m_needs_to_bind = descriptor.resource != sampler->GetResource() ? true : m_needs_to_bind; // affects vkUpdateDescriptorSets

                // Update
                descriptor.resource = sampler->GetResource();

                break;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetTexture(const uint32_t slot, RHI_Texture* texture, const bool storage)
    {
        if (!texture->IsSampled())
        {
            LOG_ERROR("Texture can't be used for sampling");
            return;
        }

        if (texture->GetLayout() == RHI_Image_Layout::Undefined || texture->GetLayout() == RHI_Image_Layout::Preinitialized)
        {
            LOG_ERROR("Texture has an invalid layout");
            return;
        }

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            const uint32_t slot_match = slot + (storage ? rhi_shader_shift_storage_texture : rhi_shader_shift_texture);

            if (descriptor.type == RHI_Descriptor_Texture && descriptor.slot == slot_match)
            {
                // Determine if the descriptor set needs to bind
                m_needs_to_bind = descriptor.resource != texture->Get_Resource_View() ? true : m_needs_to_bind; // affects vkUpdateDescriptorSets

                // Update
                descriptor.resource = texture->Get_Resource_View();
                descriptor.layout   = texture->GetLayout();

                break;
            }
        }
    }

    bool RHI_DescriptorSetLayout::GetResource_DescriptorSet(RHI_DescriptorCache* descriptor_cache, void*& descriptor_set)
    {
        // Integrate resource into the hash
        size_t hash = m_descriptor_set_layout_hash;
        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            Utility::Hash::hash_combine(hash, descriptor.resource);
        }

        // If we don't have a descriptor set to match that state, create one
        const auto it = m_descriptor_sets.find(hash);
        if (it == m_descriptor_sets.end())
        {
            // Only allocate if the descriptor set cache hash enough capacity
            if (descriptor_cache->HasEnoughCapacity())
            {
                descriptor_set = CreateDescriptorSet(hash, descriptor_cache);
            }
            else
            {
                return false;
            }
        }
        else // retrieve the existing one
        {
            if (m_needs_to_bind)
            {
                descriptor_set  = it->second;
                m_needs_to_bind = false;
            }
        }

        return true;
    }

    const std::array<uint32_t, Spartan::rhi_max_constant_buffer_count> RHI_DescriptorSetLayout::GetDynamicOffsets() const
    {
        // vkCmdBindDescriptorSets expects an array without empty values

        std::array<uint32_t, Spartan::rhi_max_constant_buffer_count> dynamic_offsets;
        dynamic_offsets.fill(0);

        uint32_t j = 0;
        for (uint32_t i = 0; i < rhi_max_constant_buffer_count; i++)
        {
            if (m_dynamic_offsets[i] != rhi_dynamic_offset_empty)
            {
                dynamic_offsets[j++] = m_dynamic_offsets[i];
            }
        }

        return dynamic_offsets;
    }

    uint32_t RHI_DescriptorSetLayout::GetDynamicOffsetCount() const
    {
        uint32_t dynamic_offset_count = 0;

        for (uint32_t i = 0; i < rhi_max_constant_buffer_count; i++)
        {
            if (m_dynamic_offsets[i] != rhi_dynamic_offset_empty)
            {
                dynamic_offset_count++;
            }
        }

        return dynamic_offset_count;
    }
}
