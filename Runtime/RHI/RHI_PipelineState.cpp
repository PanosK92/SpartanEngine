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
#include "Spartan.h"
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

    }

    bool RHI_PipelineState::IsValid()
    {
        // Deduce if marking and profiling should occur
        profile = pass_name != nullptr;
        mark    = pass_name != nullptr;

        // Deduce states
        bool has_shader_compute     = shader_compute    ? shader_compute->IsCompiled()  : false;
        bool has_shader_vertex      = shader_vertex     ? shader_vertex->IsCompiled()   : false;
        bool has_shader_pixel       = shader_pixel      ? shader_pixel->IsCompiled()    : false;
        bool has_render_target      = render_target_color_textures[0] || render_target_depth_texture;   // Check that there is at least one render target
        bool has_backbuffer         = render_target_swapchain;                                          // Check that no both the swapchain and the color render target are active
        bool has_graphics_states    = rasterizer_state && blend_state && depth_stencil_state && primitive_topology != RHI_PrimitiveTopology_Unknown;
        bool is_graphics_pso        = (has_shader_vertex || has_shader_pixel) && !has_shader_compute;
        bool is_compute_pso         = has_shader_compute && (!has_shader_vertex && !has_shader_pixel);

        // Validate pipeline type
        if (!is_compute_pso && !is_graphics_pso)
        {
            LOG_ERROR("Invalid pipeline state. No compute, vertex or pixel shaders have been provided.");
            return false;
        }

        // Validate graphics states
        if (is_graphics_pso && !has_graphics_states)
        {
            LOG_ERROR("Invalid graphics pipeline state. Not all required graphics states have been provided.");
            return false;
        }

        // Validate render targets
        if (is_graphics_pso && !has_render_target && !has_backbuffer)
        {
            if (!has_render_target && !has_backbuffer)
            {
                LOG_ERROR("Invalid graphics pipeline state. No render targets or a backbuffer have been provided.");
                return false;
            }

            if (has_render_target && has_backbuffer)
            {
                LOG_ERROR("Invalid graphics pipeline state. Both a render target and a backbuffer have been provided.");
                return false;
            }
        }

        return true;
    }

    uint32_t RHI_PipelineState::GetWidth() const
    {
        if (render_target_swapchain)
            return render_target_swapchain->GetWidth();

        if (render_target_color_textures[0])
            return render_target_color_textures[0]->GetWidth();

        if (render_target_depth_texture)
            return render_target_depth_texture->GetWidth();

        return 0;
    }

    uint32_t RHI_PipelineState::GetHeight() const
    {
        if (render_target_swapchain)
            return render_target_swapchain->GetHeight();

        if (render_target_color_textures[0])
            return render_target_color_textures[0]->GetHeight();

        if (render_target_depth_texture)
            return render_target_depth_texture->GetHeight();

        return 0;
    }

    void RHI_PipelineState::ResetClearValues()
    {
        clear_color.fill(rhi_color_load);
        clear_depth   = rhi_depth_load;
        clear_stencil = rhi_stencil_load;
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
        Utility::Hash::hash_combine(m_hash, render_target_swapchain ? render_target_swapchain->GetId() : 0);

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

        // Shaders
        {
            if (shader_compute)
            {
                Utility::Hash::hash_combine(m_hash, shader_compute->GetId());
            }

            if (shader_vertex)
            {
                Utility::Hash::hash_combine(m_hash, shader_vertex->GetId());
            }

            if (shader_pixel)
            {
                Utility::Hash::hash_combine(m_hash, shader_pixel->GetId());
            }
        }

        // RTs
        bool has_rt_color = false;
        {
            uint8_t load_op = 0;

            // Color
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                if (RHI_Texture* texture = render_target_color_textures[i])
                {
                    Utility::Hash::hash_combine(m_hash, texture->GetId());

                    load_op = clear_color[i] == rhi_color_dont_care ? 0 : clear_color[i] == rhi_color_load ? 1 : 2;
                    Utility::Hash::hash_combine(m_hash, load_op);

                    has_rt_color = true;
                }
            }

            // Depth
            if (render_target_depth_texture)
            {
                Utility::Hash::hash_combine(m_hash, render_target_depth_texture->GetId());

                load_op = clear_depth == rhi_depth_dont_care ? 0 : clear_depth == rhi_depth_load ? 1 : 2;
                Utility::Hash::hash_combine(m_hash, load_op);

                load_op = clear_stencil == rhi_stencil_dont_care ? 0 : clear_stencil == rhi_stencil_load ? 1 : 2;
                Utility::Hash::hash_combine(m_hash, load_op);
            }
        }

        // Initial and final layouts
        {
            if (has_rt_color)
            {
                Utility::Hash::hash_combine(m_hash, render_target_color_layout_initial);
                Utility::Hash::hash_combine(m_hash, render_target_color_layout_final);
            }

            if (render_target_depth_texture)
            {
                Utility::Hash::hash_combine(m_hash, render_target_depth_layout_initial);
                Utility::Hash::hash_combine(m_hash, render_target_depth_layout_final);
            }
        }
    }
}
