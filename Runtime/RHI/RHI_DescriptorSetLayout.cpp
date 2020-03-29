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
    RHI_DescriptorSetLayout::RHI_DescriptorSetLayout(const RHI_Device* rhi_device, const std::vector<RHI_Descriptor>& descriptors)
    {
        m_rhi_device            = rhi_device;
        m_descriptors           = descriptors;
        m_descriptor_set_layout = CreateDescriptorSetLayout(m_descriptors);
    }

    void RHI_DescriptorSetLayout::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            const bool is_dynamic   = constant_buffer->IsDynamic();
            const bool is_same_type = (!is_dynamic && descriptor.type == RHI_Descriptor_ConstantBuffer) || (is_dynamic && descriptor.type == RHI_Descriptor_ConstantBufferDynamic);
            const bool is_same_slot = is_same_type && descriptor.slot == slot + m_rhi_device->GetContextRhi()->shader_shift_buffer;

            if (is_same_slot)
            {
                // Determine if the descriptor set needs to bind
                m_needs_to_bind = descriptor.id     != constant_buffer->GetId()     ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.offset != constant_buffer->GetOffset() ? true : m_needs_to_bind;
                m_needs_to_bind = descriptor.range  != constant_buffer->GetStride() ? true : m_needs_to_bind;
                m_needs_to_bind = !m_constant_buffer_dynamic_offsets.empty() ? (m_constant_buffer_dynamic_offsets[0] != constant_buffer->GetOffsetDynamic() ? true : m_needs_to_bind) : m_needs_to_bind;

                // Update
                descriptor.id       = constant_buffer->GetId();
                descriptor.resource = constant_buffer->GetResource();
                descriptor.offset   = constant_buffer->GetOffset();
                descriptor.range    = constant_buffer->GetStride();

                // Update the dynamic offset.
                // Note: This is not directly related to the descriptor, it's a value that gets set when vkCmdBindDescriptorSets is called, just before a draw call.
                if (is_dynamic)
                {
                    if (m_constant_buffer_dynamic_offsets.empty())
                    {
                        m_constant_buffer_dynamic_offsets.emplace_back(constant_buffer->GetOffsetDynamic());
                    }
                    else
                    {
                        m_constant_buffer_dynamic_offsets[0] = constant_buffer->GetOffsetDynamic();
                    }
                }

                break;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Sampler && descriptor.slot == slot + m_rhi_device->GetContextRhi()->shader_shift_sampler)
            {
                // Determine if the descriptor set needs to bind
                m_needs_to_bind = descriptor.id != sampler->GetId() ? true : m_needs_to_bind;

                // Update
                descriptor.id       = sampler->GetId();
                descriptor.resource = sampler->GetResource();

                break;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetTexture(const uint32_t slot, RHI_Texture* texture)
    {
        if (!texture->IsSampled())
        {
            LOG_ERROR("Texture can't be used for sampling");
            return;
        }

        if (texture->GetLayout() == RHI_Image_Undefined || texture->GetLayout() == RHI_Image_Preinitialized)
        {
            LOG_ERROR("Texture has an invalid layout");
            return;
        }

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Texture && descriptor.slot == slot + m_rhi_device->GetContextRhi()->shader_shift_texture)
            {
                // Determine if the descriptor set needs to bind
                m_needs_to_bind = descriptor.id != texture->GetId() ? true : m_needs_to_bind;

                // Update
                descriptor.id       = texture->GetId();
                descriptor.resource = texture->Get_View_Texture();
                descriptor.layout   = texture->GetLayout();

                break;
            }
        }
    }

    void* RHI_DescriptorSetLayout::GetResource_DescriptorSet(RHI_DescriptorCache* descriptor_cache)
    {
        void* descriptor_set = nullptr;

        // Get the hash of the current state of the descriptors
        const size_t hash = ComputeDescriptorSetHash(m_descriptors);

        // If we don't have a descriptor set to match that state, create one
        if (m_descriptor_sets.find(hash) == m_descriptor_sets.end())
        {
            // Only allocate if the descriptor set cache hash enough capacity
            if (descriptor_cache->HasEnoughCapacity())
            {
                descriptor_set = CreateDescriptorSet(hash, descriptor_cache);
            }
        }
        else // retrieve the existing one
        {
            if (m_needs_to_bind)
            {
                descriptor_set  = m_descriptor_sets[hash];
                m_needs_to_bind = false;
            }
        }

        return descriptor_set;
    }

    size_t RHI_DescriptorSetLayout::ComputeDescriptorSetHash(const vector<RHI_Descriptor>& descriptors)
    {
        size_t hash = 0;

        for (const RHI_Descriptor& descriptor : descriptors)
        {
            Utility::Hash::hash_combine(hash, descriptor.slot);
            Utility::Hash::hash_combine(hash, descriptor.stage);
            Utility::Hash::hash_combine(hash, descriptor.id);
            Utility::Hash::hash_combine(hash, descriptor.offset);
            Utility::Hash::hash_combine(hash, descriptor.range);
            Utility::Hash::hash_combine(hash, descriptor.resource);
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(descriptor.type));
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(descriptor.layout));
        }

        return hash;
    }
}
