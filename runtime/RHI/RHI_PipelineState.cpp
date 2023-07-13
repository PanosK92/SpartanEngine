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

//= INCLUDES =====================
#include "pch.h"
#include "RHI_PipelineState.h"
#include "RHI_Shader.h"
#include "RHI_Texture.h"
#include "RHI_SwapChain.h"
#include "RHI_BlendState.h"
#include "RHI_InputLayout.h"
#include "RHI_RasterizerState.h"
#include "RHI_DepthStencilState.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_PipelineState::RHI_PipelineState()
    {
        clear_color.fill(rhi_color_load);
        render_target_color_textures.fill(nullptr);
    }

    RHI_PipelineState::~RHI_PipelineState()
    {

    }

    uint64_t RHI_PipelineState::ComputeHash()
    {
        m_hash = 0;

        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(dynamic_scissor));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(can_use_vertex_index_buffers));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(primitive_topology));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(render_target_color_texture_array_index));
        m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(render_target_depth_stencil_texture_array_index));

        if (render_target_swapchain)
        {
            m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(render_target_swapchain->GetFormat()));
        }

        if (rasterizer_state)
        {
            m_hash = rhi_hash_combine(m_hash, rasterizer_state->GetHash());
        }

        if (blend_state)
        {
            m_hash = rhi_hash_combine(m_hash, blend_state->GetHash());
        }

        if (depth_stencil_state)
        {
            m_hash = rhi_hash_combine(m_hash, depth_stencil_state->GetHash());
        }

        // Shaders
        {
            if (shader_compute)
            {
                m_hash = rhi_hash_combine(m_hash, shader_compute->GetHash());
            }

            if (shader_vertex)
            {
                m_hash = rhi_hash_combine(m_hash, shader_vertex->GetHash());
            }

            if (shader_pixel)
            {
                m_hash = rhi_hash_combine(m_hash, shader_pixel->GetHash());
            }
        }

        // RTs
        {
            // Color
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                if (RHI_Texture* texture = render_target_color_textures[i])
                {
                    m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(texture->GetFormat()));
                }
            }

            // Depth
            if (render_target_depth_texture)
            {
                m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(render_target_depth_texture->GetFormat()));
            }
        }

        return m_hash;
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

    bool RHI_PipelineState::IsValid() const
    {
        // Deduce states
        bool has_shader_compute  = shader_compute ? shader_compute->IsCompiled() : false;
        bool has_shader_vertex   = shader_vertex  ? shader_vertex->IsCompiled()  : false;
        bool has_shader_pixel    = shader_pixel   ? shader_pixel->IsCompiled()   : false;
        bool has_render_target   = render_target_color_textures[0] || render_target_depth_texture; // Check that there is at least one render target
        bool has_backbuffer      = render_target_swapchain;                                        // Check that no both the swapchain and the color render target are active
        bool has_graphics_states = rasterizer_state && blend_state && depth_stencil_state && primitive_topology != RHI_PrimitiveTopology_Mode::Undefined;
        bool is_graphics         = (has_shader_vertex || has_shader_pixel) && !has_shader_compute;
        bool is_compute          = has_shader_compute && (!has_shader_vertex && !has_shader_pixel);

        // There must be at least one shader
        if (!has_shader_compute && !has_shader_vertex && !has_shader_pixel)
            return false;

        // If this is a graphics then there must he graphics states
        if (is_graphics&& !has_graphics_states)
            return false;

        // If this is a graphics then there must be a render target
        if (is_graphics&& !has_render_target && !has_backbuffer)
        {
            if (!has_render_target && !has_backbuffer)
                return false;
            
            if (has_render_target && has_backbuffer)
                return false;
        }

        return true;
    }

    bool RHI_PipelineState::HasClearValues() const
    {
        if (clear_depth != rhi_depth_load && clear_depth != rhi_depth_dont_care)
            return true;

        if (clear_stencil != rhi_stencil_load && clear_stencil != rhi_stencil_dont_care)
            return true;

        for (const Color& color : clear_color)
        {
            if (color != rhi_color_load && color != rhi_color_dont_care)
                return true;
        }

        return false;
    }
}
