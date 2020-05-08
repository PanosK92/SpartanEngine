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

//= INCLUDES =====================
#include "RHI_PipelineState.h"
#include "RHI_Shader.h"
#include "RHI_Texture.h"
#include "RHI_SwapChain.h"
#include "RHI_BlendState.h"
#include "RHI_InputLayout.h"
#include "RHI_RasterizerState.h"
#include "RHI_DepthStencilState.h"
#include "..\Utilities\Hash.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{

    RHI_PipelineState::RHI_PipelineState()
    {
        for (uint32_t i = 0; i < state_max_render_target_count; i++)
        {
            render_target_color_textures[i] = nullptr;
            clear_color[i]                  = state_color_load;
            m_frame_buffers[i]              = nullptr;
        }
    }

    bool RHI_PipelineState::IsValid()
    {
        bool is_valid = true;

        // Deduce if marking and profiling should occur
        profile = pass_name != nullptr;
        mark    = pass_name != nullptr;

        // Vertex shader
        if (shader_vertex)
        {
            if (!shader_vertex->IsCompiled())
            {
                is_valid = false;
            }

            // Check that there is a render target
            if (!render_target_swapchain && !render_target_color_textures[0] && !render_target_depth_texture)
            {
                is_valid = false;
            }

            // Check that no both the swapchain and the color render target are active
            if (render_target_swapchain && render_target_color_textures[0])
            {
                is_valid = false;
            }

            // Check for required states
            if (!rasterizer_state || !blend_state || !depth_stencil_state || primitive_topology == RHI_PrimitiveTopology_Unknown)
            {
                is_valid = false;
            }
        }

        // Pixel shader
        if (shader_pixel)
        {
            if (!shader_pixel->IsCompiled())
            {
                is_valid = false;
            }
        }

        // Compute shader
        if (shader_compute)
        {
            if (!shader_compute->IsCompiled())
            {
                is_valid = false;
            }

            if (!unordered_access_view)
            {
                is_valid = false;
            }
        }

        if (!is_valid)
        {
            LOG_ERROR("Invalid pipeline state");
        }

        return is_valid;
    }

    bool RHI_PipelineState::AcquireNextImage() const
    {
        return render_target_swapchain ? render_target_swapchain->AcquireNextImage() : true;
    }

	uint32_t RHI_PipelineState::GetWidth() const
	{
        if (render_target_swapchain)
            return render_target_swapchain->GetWidth();

        if (render_target_color_textures[0])
            return render_target_color_textures[0]->GetWidth();

        return 0;
	}

    uint32_t RHI_PipelineState::GetHeight() const
    {
        if (render_target_swapchain)
            return render_target_swapchain->GetHeight();

        if (render_target_color_textures[0])
            return render_target_color_textures[0]->GetHeight();

        return 0;
    }

	void RHI_PipelineState::ResetClearValues()
	{
        clear_color.fill(state_color_dont_care);
        clear_depth   = state_depth_dont_care;
        clear_stencil = state_stencil_dont_care;
	}

	void RHI_PipelineState::ComputeHash()
    {
        m_hash = 0;

        Utility::Hash::hash_combine(m_hash, dynamic_scissor);
        Utility::Hash::hash_combine(m_hash, viewport.x);
        Utility::Hash::hash_combine(m_hash, viewport.y);
        Utility::Hash::hash_combine(m_hash, viewport.width);
        Utility::Hash::hash_combine(m_hash, viewport.height);
        Utility::Hash::hash_combine(m_hash, primitive_topology);
        Utility::Hash::hash_combine(m_hash, vertex_buffer_stride);
        Utility::Hash::hash_combine(m_hash, render_target_color_texture_array_index);
        Utility::Hash::hash_combine(m_hash, render_target_depth_stencil_texture_array_index);
        Utility::Hash::hash_combine(m_hash, render_target_swapchain != nullptr);

        if (!dynamic_scissor)
        {
            Utility::Hash::hash_combine(m_hash, scissor.left);
            Utility::Hash::hash_combine(m_hash, scissor.top);
            Utility::Hash::hash_combine(m_hash, scissor.right);
            Utility::Hash::hash_combine(m_hash, scissor.bottom);
        }

        if (rasterizer_state)
        {
            Utility::Hash::hash_combine(m_hash, rasterizer_state->GetId());
        }

        if (blend_state)
        {
            Utility::Hash::hash_combine(m_hash, blend_state->GetId());
        }

        if (depth_stencil_state)
        {
            Utility::Hash::hash_combine(m_hash, depth_stencil_state->GetId());
        }

        if (shader_vertex)
        {
            Utility::Hash::hash_combine(m_hash, shader_vertex->GetId());
        }

        if (shader_pixel)
        {
            Utility::Hash::hash_combine(m_hash, shader_pixel->GetId());
        }

        if (shader_compute)
        {
            Utility::Hash::hash_combine(m_hash, shader_compute->GetId());
        }

        for (RHI_Texture* render_target_color_texture : render_target_color_textures)
        {
            if (render_target_color_texture)
            {
                Utility::Hash::hash_combine(m_hash, render_target_color_texture->GetId());
            }
        }

        if (render_target_depth_texture)
        {
            Utility::Hash::hash_combine(m_hash, render_target_depth_texture->GetId());
        }
    }
}
