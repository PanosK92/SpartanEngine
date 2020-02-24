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

//= INCLUDES =================
#include "RHI_PipelineCache.h"
#include "RHI_Texture.h"
#include "RHI_Pipeline.h"
#include "RHI_SwapChain.h"
#include "RHI_DescriptorSet.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Pipeline* RHI_PipelineCache::GetPipeline(RHI_PipelineState& pipeline_state, RHI_CommandList* cmd_list)
    {
        // Validate it
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            return nullptr;
        }

        // Render target layout transitions
        {
            // Color
            for (auto i = 0; i < state_max_render_target_count; i++)
            {
                if (RHI_Texture* texture = pipeline_state.render_target_color_textures[i])
                {
                    texture->SetLayout(RHI_Image_Shader_Read_Only_Optimal, cmd_list);
                }
            }

            // Depth
            if (RHI_Texture* texture = pipeline_state.render_target_depth_texture)
            {
                texture->SetLayout(RHI_Image_Depth_Stencil_Attachment_Optimal, cmd_list);
            }

            // Swapchain
            if (RHI_SwapChain* swapchain = pipeline_state.render_target_swapchain)
            {
                swapchain->SetLayout(RHI_Image_Present_Src, cmd_list);
            }
        }

        // Compute a hash for it
        pipeline_state.ComputeHash();
        size_t hash = pipeline_state.GetHash();

        // If no pipeline exists for this state, create one
        if (m_cache.find(hash) == m_cache.end())
        {
            // Cache a new pipeline
            m_cache.emplace(make_pair(hash, move(make_shared<RHI_Pipeline>(m_rhi_device, pipeline_state))));
        }

        RHI_Pipeline* pipeline = m_cache[hash].get();

        if (RHI_DescriptorSet* descriptor_set = pipeline->GetDescriptorSet())
        {
            descriptor_set->MakeDirty();
        }
        else
        {
            LOG_ERROR("Pipeline has no descriptor set");
        }

        return pipeline;
    }
}
