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

//= INCLUDES ===================
#include "RHI_DescriptorCache.h"
#include "RHI_Shader.h"
#include "RHI_Sampler.h"
#include "RHI_Texture.h"
#include "RHI_PipelineState.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_Implementation.h"
#include "..\Utilities\Hash.h"
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorCache::RHI_DescriptorCache(const RHI_Device* rhi_device)
    {
        m_rhi_device = rhi_device;
    }

    void RHI_DescriptorCache::SetPipelineState(RHI_PipelineState& pipeline_state)
    {
        // Name this resource, very useful for Vulkan debugging
        m_name = (pipeline_state.shader_vertex ? pipeline_state.shader_vertex->GetName() : "null") + "-" + (pipeline_state.shader_pixel ? pipeline_state.shader_pixel->GetName() : "null");

        // Update dynamic constant buffer slots
        m_constant_buffer_dynamic_slots.clear();
        if (pipeline_state.dynamic_constant_buffer_slot != -1)
        {
            m_constant_buffer_dynamic_slots.emplace_back(pipeline_state.dynamic_constant_buffer_slot);
        }

        // Generate descriptors by reflecting the shaders
        ReflectShaders(pipeline_state.shader_vertex, pipeline_state.shader_pixel);

        // Set descriptor set capacity
        SetDescriptorCapacity(m_descriptor_set_capacity);

        // maybe we don't have to bind whenever the pipeline changes, have to investigate this
        m_needs_to_bind = true;
    }

    void RHI_DescriptorCache::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer)
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
                m_needs_to_bind = !m_constant_buffer_dynamic_offsets.empty() ? (m_constant_buffer_dynamic_offsets[0]  != constant_buffer->GetOffsetDynamic()  ? true : m_needs_to_bind) : m_needs_to_bind;

                // Update
                descriptor.id       = constant_buffer->GetId();
                descriptor.resource = constant_buffer->GetResource();     
                descriptor.offset   = constant_buffer->GetOffset();
                descriptor.range    = constant_buffer->GetStride();

                // Update the dynamic offset.
                // Note: This is not directly related to the descriptor, it a value that gets set when vkCmdBindDescriptorSets is called, just before a draw call.
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

    void RHI_DescriptorCache::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
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

    void RHI_DescriptorCache::SetTexture(const uint32_t slot, RHI_Texture* texture)
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

    void RHI_DescriptorCache::GrowIfNeeded()
    {
        // If the descriptor pool is full, re-allocate with double size
        if (m_descriptor_sets.size() < m_descriptor_set_capacity)
            return;

        m_descriptor_set_capacity *= 2;
        SetDescriptorCapacity(m_descriptor_set_capacity);
    }

	void* RHI_DescriptorCache::GetResource_DescriptorSet()
    {
        void* descriptor_set = nullptr;

        // Get the hash of the current descriptor blueprint
        const size_t hash = GetDescriptorsHash(m_descriptors);

        if (m_descriptor_sets.find(hash) == m_descriptor_sets.end())
        {
            // If the descriptor set doesn't exist, create one
            descriptor_set = CreateDescriptorSet(hash);
        }
        else
        {
            // If the descriptor set exists but needs to be bound, return it
            if (m_needs_to_bind)
            {
                descriptor_set = m_descriptor_sets[hash];
                m_needs_to_bind = false;
            }
        }

        return descriptor_set;
    }

    size_t RHI_DescriptorCache::GetDescriptorsHash(const vector<RHI_Descriptor>& descriptor_blueprint)
    {
        size_t hash = 0;

        for (const RHI_Descriptor& descriptor : m_descriptors)
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

    void RHI_DescriptorCache::ReflectShaders(const RHI_Shader* shader_vertex, const RHI_Shader* shader_pixel /*= nullptr*/)
    {
        m_descriptors.clear();

        if (!shader_vertex)
        {
            LOG_ERROR("Vertex shader is invalid");
            return;
        }

        // Wait for shader to compile
        while (shader_vertex->GetCompilationState() == Shader_Compilation_Compiling) {}

        // Get vertex shader descriptors
        m_descriptors = shader_vertex->GetDescriptors();

        // If there is a pixel shader, merge it's resources into our map as well
        if (shader_pixel)
        {
            while (shader_pixel->GetCompilationState() == Shader_Compilation_Compiling) {}
            for (const RHI_Descriptor& descriptor_reflected : shader_pixel->GetDescriptors())
            {
                // Assume that the descriptor has been created in the vertex shader and only try to update it's shader stage
                bool updated_existing = false;
                for (RHI_Descriptor& descriptor : m_descriptors)
                {
                    bool is_same_resource =
                        (descriptor.type == descriptor_reflected.type) &&
                        (descriptor.slot == descriptor_reflected.slot);

                    if ((descriptor.type == descriptor_reflected.type) && (descriptor.slot == descriptor_reflected.slot))
                    {
                        descriptor.stage |= descriptor_reflected.stage;
                        updated_existing = true;
                        break;
                    }
                }

                // If no updating took place, this descriptor is new, so add it
                if (!updated_existing)
                {
                    m_descriptors.emplace_back(descriptor_reflected);
                }
            }
        }

        // Change constant buffers to dynamic (if requested)
        for (uint32_t constant_buffer_dynamic_slot : m_constant_buffer_dynamic_slots)
        {
            for (RHI_Descriptor& descriptor : m_descriptors)
            {
                if (descriptor.type == RHI_Descriptor_ConstantBuffer)
                {
                    if (descriptor.slot == constant_buffer_dynamic_slot + m_rhi_device->GetContextRhi()->shader_shift_buffer)
                    {
                        descriptor.type = RHI_Descriptor_ConstantBufferDynamic;
                    }
                }
            }
        }
    }
}
