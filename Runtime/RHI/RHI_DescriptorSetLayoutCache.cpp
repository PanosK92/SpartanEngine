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
#include "RHI_DescriptorSetLayoutCache.h"
#include "RHI_Shader.h"
#include "RHI_PipelineState.h"
#include "RHI_DescriptorSetLayout.h"
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSetLayoutCache::RHI_DescriptorSetLayoutCache(const RHI_Device* rhi_device)
    {
        m_rhi_device = rhi_device;

        // Set the descriptor set capacity to an initial value
        SetDescriptorSetCapacity(256);
    }

    void RHI_DescriptorSetLayoutCache::SetPipelineState(RHI_PipelineState& pipeline_state)
    {
        // Get pipeline descriptors
        GetDescriptors(pipeline_state, m_descriptors);

        // Compute a hash for the descriptors
        uint32_t hash = 0;
        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            Utility::Hash::hash_combine(hash, descriptor.ComputeHash(false));
        }

        // Search for a descriptor set layout which matches this hash
        auto it = m_descriptor_set_layouts.find(hash);
        bool cached = it != m_descriptor_set_layouts.end();

        // If there is no descriptor set layout for this particular hash, create one
        if (!cached)
        {
            // Create a name for the descriptor set layout, very useful for Vulkan debugging
            string name = "CS:"     + (pipeline_state.shader_compute    ? pipeline_state.shader_compute->GetObjectName()  : "null");
            name        += "-VS:"   + (pipeline_state.shader_vertex     ? pipeline_state.shader_vertex->GetObjectName()   : "null");
            name        += "-PS:"   + (pipeline_state.shader_pixel      ? pipeline_state.shader_pixel->GetObjectName()    : "null");

            // Emplace a new descriptor set layout
            it = m_descriptor_set_layouts.emplace(make_pair(hash, make_shared<RHI_DescriptorSetLayout>(m_rhi_device, m_descriptors, name.c_str()))).first;
        }

        // Get the descriptor set layout we will be using
        m_descriptor_layout_current = it->second.get();

        // Clear any data data the the descriptors might contain from previous uses (and hence can possibly be invalid by now)
        if (cached)
        {
            m_descriptor_layout_current->ClearDescriptorData();
        }

        // Make it bind
        m_descriptor_layout_current->NeedsToBind();
    }

    void RHI_DescriptorSetLayoutCache::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        SP_ASSERT(m_descriptor_layout_current != nullptr);

        m_descriptor_layout_current->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_DescriptorSetLayoutCache::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        SP_ASSERT(m_descriptor_layout_current != nullptr);

        m_descriptor_layout_current->SetSampler(slot, sampler);
    }

    void RHI_DescriptorSetLayoutCache::SetTexture(const uint32_t slot, RHI_Texture* texture, const int mip, const bool ranged)
    {
        SP_ASSERT(m_descriptor_layout_current != nullptr);

        m_descriptor_layout_current->SetTexture(slot, texture, mip, ranged);
    }

    void RHI_DescriptorSetLayoutCache::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer)
    {
        SP_ASSERT(m_descriptor_layout_current != nullptr);

        m_descriptor_layout_current->SetStructuredBuffer(slot, structured_buffer);
    }

    void RHI_DescriptorSetLayoutCache::RemoveConstantBuffer(RHI_ConstantBuffer* constant_buffer)
    {
        SP_ASSERT(constant_buffer != nullptr);

        if (m_descriptor_layout_current)
        {
            m_descriptor_layout_current->RemoveConstantBuffer(constant_buffer);
        }
    }

    void RHI_DescriptorSetLayoutCache::RemoveTexture(RHI_Texture* texture, const int mip)
    {
        SP_ASSERT(texture != nullptr);

        if (m_descriptor_layout_current)
        {
            m_descriptor_layout_current->RemoveTexture(texture, mip);
        }
    }

    bool RHI_DescriptorSetLayoutCache::GetDescriptorSet(RHI_DescriptorSet*& descriptor_set)
    {
        if (m_descriptor_layout_current)
        {
            return m_descriptor_layout_current->GetDescriptorSet(this, descriptor_set);
        }

        return false;
    }

    void RHI_DescriptorSetLayoutCache::GrowIfNeeded()
    {
        // If there is room for at least one more descriptor set (hence +1), we don't need to re-allocate yet
        const uint32_t required_capacity = GetDescriptorSetCount() + 1;

        // If we are over-budget, re-allocate the descriptor pool with double size
        if (required_capacity > m_descriptor_set_capacity)
        {
            SetDescriptorSetCapacity(m_descriptor_set_capacity * 2);
        }
    }

    uint32_t RHI_DescriptorSetLayoutCache::GetDescriptorSetCount() const
    {
        // Instead of updating descriptors to not reference it, the RHI_Texture2D destructor
        // resets the descriptor set layout cache. This can happen from another thread hence
        //  we do this wait here. Ideally ~RHI_Texture2D() is made to work right.
        while (m_descriptor_set_layouts_being_cleared)
        {
            LOG_INFO("Waiting for descriptor set layouts to be cleared...");
            this_thread::sleep_for(chrono::milliseconds(16));
        }

        uint32_t descriptor_set_count = 0;
        for (const auto& it : m_descriptor_set_layouts)
        {
            descriptor_set_count += it.second->GetDescriptorSetCount();
        }

        return descriptor_set_count;
    }

    void RHI_DescriptorSetLayoutCache::GetDescriptors(RHI_PipelineState& pipeline_state, vector<RHI_Descriptor>& descriptors)
    {
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            descriptors.clear();
            return;
        }

        descriptors.clear();

        bool descriptors_acquired = false;

        if (pipeline_state.IsCompute())
        {
            // Wait for compilation
            pipeline_state.shader_compute->WaitForCompilation();

            // Get compute shader descriptors
            descriptors = pipeline_state.shader_compute->GetDescriptors();
            descriptors_acquired = true;
        }
        else if (pipeline_state.IsGraphics())
        {
            // Wait for compilation
            pipeline_state.shader_vertex->WaitForCompilation();

            // Get vertex shader descriptors
            descriptors = pipeline_state.shader_vertex->GetDescriptors();
            descriptors_acquired = true;

            // If there is a pixel shader, merge it's resources into our map as well
            if (pipeline_state.shader_pixel)
            {
                // Wait for compilation
                pipeline_state.shader_pixel->WaitForCompilation();

                for (const RHI_Descriptor& descriptor_reflected : pipeline_state.shader_pixel->GetDescriptors())
                {
                    // Assume that the descriptor has been created in the vertex shader and only try to update it's shader stage
                    bool updated_existing = false;
                    for (RHI_Descriptor& descriptor : descriptors)
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
                        descriptors.emplace_back(descriptor_reflected);
                    }
                }
            }
        }

        // Change constant buffers to dynamic (if requested)
        if (descriptors_acquired)
        {
            for (uint32_t i = 0; i < rhi_max_constant_buffer_count; i++)
            {
                for (RHI_Descriptor& descriptor : descriptors)
                {
                    if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
                    {
                        if (descriptor.slot == pipeline_state.dynamic_constant_buffer_slots[i] + rhi_shader_shift_register_b)
                        {
                            descriptor.is_dynamic_constant_buffer = true;
                        }
                    }
                }
            }
        }
    }
}
