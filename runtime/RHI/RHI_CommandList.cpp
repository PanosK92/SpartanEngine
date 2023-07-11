/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "RHI_CommandList.h"
#include "RHI_Device.h"
#include "RHI_Fence.h"
#include "RHI_Semaphore.h"
#include "RHI_DescriptorSetLayout.h"
#include "RHI_Shader.h"
#include "RHI_Pipeline.h"
#include "RHI_CommandPool.h"
#include "../Rendering/Renderer.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_CommandList::Wait(const bool log_on_wait /*= true*/)
    {
        SP_ASSERT_MSG(m_state == RHI_CommandListState::Submitted, "The command list hasn't been submitted, can't wait for it.");

        // Wait for the command list to finish executing
        if (IsExecuting())
        {
            if (log_on_wait)
            {
                SP_LOG_WARNING("Waiting for command list \"%s\" to finish executing...", m_object_name.c_str());
            }

            SP_ASSERT_MSG(m_proccessed_fence->Wait(), "Timed out while waiting for the fence");
        }

        // Reset fence
        if (m_proccessed_fence->GetCpuState() == RHI_Sync_State::Submitted)
        {
            m_proccessed_fence->Reset();
        }

        m_state = RHI_CommandListState::Idle;
    }

    bool RHI_CommandList::IsExecuting()
    {
        return
            m_state == RHI_CommandListState::Submitted && // It has been submitted
            !m_proccessed_fence->IsSignaled();            // And the fence is not signaled yet
    }

    void RHI_CommandList::GetDescriptorsFromPipelineState(RHI_PipelineState& pipeline_state, vector<RHI_Descriptor>& descriptors)
    {
        if (!pipeline_state.IsValid())
        {
            SP_LOG_ERROR("Invalid pipeline state");
            descriptors.clear();
            return;
        }

        descriptors.clear();

        bool descriptors_acquired = false;

        if (pipeline_state.IsCompute())
        {
            SP_ASSERT_MSG(pipeline_state.shader_compute->GetCompilationState() == RHI_ShaderCompilationState::Succeeded, "Shader hasn't compiled");

            // Get compute shader descriptors
            descriptors = pipeline_state.shader_compute->GetDescriptors();
            descriptors_acquired = true;
        }
        else if (pipeline_state.IsGraphics())
        {
            SP_ASSERT_MSG(pipeline_state.shader_vertex->GetCompilationState() == RHI_ShaderCompilationState::Succeeded, "Shader hasn't compiled");

            // Get vertex shader descriptors
            descriptors = pipeline_state.shader_vertex->GetDescriptors();
            descriptors_acquired = true;

            // If there is a pixel shader, merge it's resources into our map as well
            if (pipeline_state.shader_pixel)
            {
                SP_ASSERT_MSG(pipeline_state.shader_pixel->GetCompilationState() == RHI_ShaderCompilationState::Succeeded, "Shader hasn't compiled");

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
    }
}
