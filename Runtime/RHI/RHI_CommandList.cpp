/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "RHI_CommandList.h"
#include "RHI_Device.h"
#include "RHI_Fence.h"
#include "RHI_Semaphore.h"
#include "RHI_DescriptorSetLayout.h"
#include "RHI_Shader.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_CommandList::Wait()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Submitted);

        // Wait for the fence.
        SP_ASSERT(m_processed_fence->Wait() && "Timeout while waiting for command list fence.");

        Descriptors_GrowPool();

        m_state = RHI_CommandListState::Idle;
    }

    void RHI_CommandList::Discard()
    {
        m_discard = true;
    }
    
    uint32_t RHI_CommandList::Gpu_GetMemory(RHI_Device* rhi_device)
    {
        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            return physical_device->GetMemory();
        }
    
        return 0;
    }

    void RHI_CommandList::Descriptors_GrowPool()
    {
        // If there is room for at least one more descriptor set (hence +1), we don't need to re-allocate yet
        const uint32_t required_capacity = Descriptors_GetDescriptorSetCount() + 1;

        // If we are over-budget, re-allocate the descriptor pool with double size
        if (required_capacity > m_descriptor_set_capacity)
        {
            Descriptors_ResetPool(m_descriptor_set_capacity * 2);
        }
    }

    uint32_t RHI_CommandList::Descriptors_GetDescriptorSetCount() const
    {
        // Instead of updating descriptors to not reference it, the RHI_Texture2D destructor
        // resets the descriptor set layout cache. This can happen from another thread hence
        //  we do this wait here. Ideally ~RHI_Texture2D() is made to work right.
        while (m_descriptor_pool_resseting)
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

    bool RHI_CommandList::Descriptors_HasEnoughCapacity() const
    {
        uint32_t required_capacity = Descriptors_GetDescriptorSetCount();
        return m_descriptor_set_capacity > required_capacity;
    }

    void RHI_CommandList::Descriptors_GetDescriptorsFromPipelineState(RHI_PipelineState& pipeline_state, vector<RHI_Descriptor>& descriptors)
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
