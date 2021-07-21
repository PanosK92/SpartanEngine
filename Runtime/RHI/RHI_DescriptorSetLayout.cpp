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

//= INCLUDES ============================
#include "Spartan.h"
#include "RHI_DescriptorSetLayout.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_StructuredBuffer.h"
#include "RHI_Sampler.h"
#include "RHI_Texture.h"
#include "RHI_DescriptorSet.h"
#include "RHI_DescriptorSetLayoutCache.h"
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSetLayout::RHI_DescriptorSetLayout(const RHI_Device* rhi_device, const vector<RHI_Descriptor>& descriptors, const string& name)
    {
        m_rhi_device    = rhi_device;
        m_descriptors   = descriptors;
        m_object_name   = name;
        CreateResource(m_descriptors);
        m_dynamic_offsets.fill(rhi_dynamic_offset_empty);

        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            Utility::Hash::hash_combine(m_hash, descriptor.ComputeHash(false));
        }
    }

    void RHI_DescriptorSetLayout::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if ((descriptor.type == RHI_Descriptor_Type::ConstantBuffer) && descriptor.slot == slot + rhi_shader_shift_register_b)
            {
                // Determine if the descriptor set needs to bind (affects vkUpdateDescriptorSets)
                m_needs_to_bind = descriptor.data   != constant_buffer              ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.offset != constant_buffer->GetOffset() ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.range  != constant_buffer->GetStride() ? true : m_needs_to_bind;

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
                descriptor.data     = static_cast<void*>(constant_buffer);
                descriptor.offset   = constant_buffer->GetOffset();
                descriptor.range    = constant_buffer->GetStride();

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Type::Sampler && descriptor.slot == slot + rhi_shader_shift_register_s)
            {
                // Determine if the descriptor set needs to bind (affects vkUpdateDescriptorSets)
                m_needs_to_bind = descriptor.data != sampler ? true : m_needs_to_bind;

                // Update
                descriptor.data = static_cast<void*>(sampler);

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetTexture(const uint32_t slot, RHI_Texture* texture, const int mip, const bool ranged)
    {
        bool set_individual_mip = mip != -1;
        RHI_Image_Layout layout = texture->GetLayout(set_individual_mip ? mip : 0);
        uint32_t mip_count      = ranged ? texture->GetMipCount() : 1; // will be bound as an array of textures if larger than 1

         // Validate layout
        SP_ASSERT(layout == RHI_Image_Layout::General || layout == RHI_Image_Layout::Shader_Read_Only_Optimal || layout == RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal);

        // Validate type
        SP_ASSERT(texture->IsSampled());

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            bool is_storage = layout == RHI_Image_Layout::General;
            bool match_type = descriptor.type == (is_storage ? RHI_Descriptor_Type::TextureStorage : RHI_Descriptor_Type::Texture);
            uint32_t shift  = is_storage ? rhi_shader_shift_register_u : rhi_shader_shift_register_t;
            bool match_slot = descriptor.slot == (slot + shift);

            if (match_type && match_slot)
            {
                // Determine if the descriptor set needs to bind (affects vkUpdateDescriptorSets)
                m_needs_to_bind = descriptor.data   != texture  ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.layout != layout   ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.mip    != mip      ? true : m_needs_to_bind;

                // Update
                descriptor.data         = static_cast<void*>(texture);
                descriptor.layout       = layout;
                descriptor.mip          = mip;
                descriptor.array_size   = mip_count;

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if ((descriptor.type == RHI_Descriptor_Type::StructuredBuffer) && descriptor.slot == slot + rhi_shader_shift_register_u)
            {
                // Determine if the descriptor set needs to bind (affects vkUpdateDescriptorSets)
                m_needs_to_bind = descriptor.data   != structured_buffer                        ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.range  != structured_buffer->GetObjectSizeGpu()    ? true : m_needs_to_bind;

                // Update
                descriptor.data     = static_cast<void*>(structured_buffer);
                descriptor.offset   = 0;
                descriptor.range    = structured_buffer->GetObjectSizeGpu();

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::RemoveConstantBuffer(RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            {
                if (descriptor.data == constant_buffer)
                {
                    // Determine if the descriptor set needs to bind (affects vkUpdateDescriptorSets)
                    m_needs_to_bind = true;

                    // Update
                    descriptor.data     = nullptr;
                    descriptor.offset   = 0;
                    descriptor.range    = 0;

                    return;
                }
            }
        }
    }

    void RHI_DescriptorSetLayout::RemoveTexture(RHI_Texture* texture, const int mip)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Type::Texture)
            {
                if (descriptor.data == texture)
                {
                    // Determine if the descriptor set needs to bind (affects vkUpdateDescriptorSets)
                    m_needs_to_bind = true;

                    // Update
                    descriptor.data     = nullptr;
                    descriptor.layout   = RHI_Image_Layout::Undefined;
                    descriptor.mip      = 0;

                    return;
                }
            }
        }
    }

    void RHI_DescriptorSetLayout::ClearDescriptorData()
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            descriptor.data = nullptr;
            descriptor.mip  = 0;
        }
    }

    bool RHI_DescriptorSetLayout::GetDescriptorSet(RHI_DescriptorSetLayoutCache* descriptor_set_layout_cache, RHI_DescriptorSet*& descriptor_set)
    {
        // Integrate descriptor data into the hash
        uint32_t hash = m_hash;
        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            Utility::Hash::hash_combine(hash, descriptor.data);
            Utility::Hash::hash_combine(hash, descriptor.mip);
        }

        // If we don't have a descriptor set to match that state, create one
        const auto it = m_descriptor_sets.find(hash);
        if (it == m_descriptor_sets.end())
        {
            // Only allocate if the descriptor set cache hash enough capacity
            if (descriptor_set_layout_cache->HasEnoughCapacity())
            {
                // Create descriptor set
                m_descriptor_sets[hash] = RHI_DescriptorSet(m_rhi_device, descriptor_set_layout_cache, m_descriptors, m_object_name.c_str());

                // Out
                descriptor_set = &m_descriptor_sets[hash];
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
                descriptor_set  = &it->second;
                m_needs_to_bind = false;
            }
        }

        return true;
    }

    const array<uint32_t, Spartan::rhi_max_constant_buffer_count> RHI_DescriptorSetLayout::GetDynamicOffsets() const
    {
        // vkCmdBindDescriptorSets expects an array without empty values

        array<uint32_t, Spartan::rhi_max_constant_buffer_count> dynamic_offsets;
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
