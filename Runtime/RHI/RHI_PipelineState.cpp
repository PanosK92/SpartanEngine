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
        clear_color.fill(rhi_color_load);
    }

    RHI_PipelineState::~RHI_PipelineState()
    {

    }

    bool RHI_PipelineState::IsValid()
    {
        // Deduce states
        bool has_shader_compute  = shader_compute ? shader_compute->IsCompiled() : false;
        bool has_shader_vertex   = shader_vertex  ? shader_vertex->IsCompiled()  : false;
        bool has_shader_pixel    = shader_pixel   ? shader_pixel->IsCompiled()   : false;
        bool has_render_target   = render_target_color_textures[0] || render_target_depth_texture; // Check that there is at least one render target
        bool has_backbuffer      = render_target_swapchain;                                        // Check that no both the swapchain and the color render target are active
        bool has_graphics_states = rasterizer_state && blend_state && depth_stencil_state && primitive_topology != RHI_PrimitiveTopology_Mode::Undefined;
        bool is_graphics_pso     = (has_shader_vertex || has_shader_pixel) && !has_shader_compute;
        bool is_compute_pso      = has_shader_compute && (!has_shader_vertex && !has_shader_pixel);

        // Note: Sometimes a pipeline state is needed just to update a constant buffer, therefore
        // a pipeline state can have no shaders but still be valid.

        // Validate graphics states
        if (is_graphics_pso && !has_graphics_states)
        {
            return false;
        }

        // Validate render targets
        if (is_graphics_pso && !has_render_target && !has_backbuffer)
        {
            if (!has_render_target && !has_backbuffer)
            {
                return false;
            }

            if (has_render_target && has_backbuffer)
            {
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

    bool RHI_PipelineState::HasClearValues()
    {
        if (clear_depth != rhi_depth_stencil_load && clear_depth != rhi_depth_stencil_dont_care)
            return true;

        if (clear_stencil != rhi_depth_stencil_load && clear_stencil != rhi_depth_stencil_dont_care)
            return true;

        for (const Math::Vector4& color : clear_color)
        {
            if (color != rhi_color_load && color != rhi_color_dont_care)
                return true;
        }

        return false;
    }
    
    uint32_t RHI_PipelineState::ComputeHash() const
    {
        uint32_t hash = 0;

        Utility::Hash::hash_combine(hash, can_use_vertex_index_buffers);
        Utility::Hash::hash_combine(hash, dynamic_scissor);
        Utility::Hash::hash_combine(hash, viewport.x);
        Utility::Hash::hash_combine(hash, viewport.y);
        Utility::Hash::hash_combine(hash, viewport.width);
        Utility::Hash::hash_combine(hash, viewport.height);
        Utility::Hash::hash_combine(hash, primitive_topology);
        Utility::Hash::hash_combine(hash, render_target_color_texture_array_index);
        Utility::Hash::hash_combine(hash, render_target_depth_stencil_texture_array_index);
        Utility::Hash::hash_combine(hash, render_target_swapchain ? render_target_swapchain->GetObjectId() : 0);

        if (!dynamic_scissor)
        {
            Utility::Hash::hash_combine(hash, scissor.left);
            Utility::Hash::hash_combine(hash, scissor.top);
            Utility::Hash::hash_combine(hash, scissor.right);
            Utility::Hash::hash_combine(hash, scissor.bottom);
        }

        if (rasterizer_state)
        {
            Utility::Hash::hash_combine(hash, rasterizer_state->GetObjectId());
        }

        if (blend_state)
        {
            Utility::Hash::hash_combine(hash, blend_state->GetObjectId());
        }

        if (depth_stencil_state)
        {
            Utility::Hash::hash_combine(hash, depth_stencil_state->GetObjectId());
        }

        // Shaders
        {
            if (shader_compute)
            {
                Utility::Hash::hash_combine(hash, shader_compute->GetObjectId());
            }

            if (shader_vertex)
            {
                Utility::Hash::hash_combine(hash, shader_vertex->GetObjectId());
            }

            if (shader_pixel)
            {
                Utility::Hash::hash_combine(hash, shader_pixel->GetObjectId());
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
                    Utility::Hash::hash_combine(hash, texture->GetObjectId());

                    load_op = clear_color[i] == rhi_color_dont_care ? 0 : clear_color[i] == rhi_color_load ? 1 : 2;
                    Utility::Hash::hash_combine(hash, load_op);

                    has_rt_color = true;
                }
            }

            // Depth
            if (render_target_depth_texture)
            {
                Utility::Hash::hash_combine(hash, render_target_depth_texture->GetObjectId());

                load_op = clear_depth == rhi_depth_stencil_dont_care ? 0 : clear_depth == rhi_depth_stencil_load ? 1 : 2;
                Utility::Hash::hash_combine(hash, load_op);

                load_op = clear_stencil == rhi_depth_stencil_dont_care ? 0 : clear_stencil == rhi_depth_stencil_load ? 1 : 2;
                Utility::Hash::hash_combine(hash, load_op);
            }
        }

        return hash;
    }

void RHI_PipelineState::TransitionRenderTargetLayouts(RHI_CommandList* cmd_list)
    {
        // Color
        {
            // Texture
            for (auto i = 0; i < rhi_max_render_target_count; i++)
            {
                if (RHI_Texture* texture = render_target_color_textures[i])
                {
                    RHI_Image_Layout layout = RHI_Image_Layout::Color_Attachment_Optimal;

                    texture->SetLayout(layout, cmd_list);
                }
            }

            // Swapchain
            if (RHI_SwapChain* swapchain = render_target_swapchain)
            {
                RHI_Image_Layout layout = RHI_Image_Layout::Present_Src;
            }
        }
        
        // Depth
        if (RHI_Texture* texture = render_target_depth_texture)
        {
            RHI_Image_Layout layout = texture->IsStencilFormat() ? RHI_Image_Layout::Depth_Stencil_Attachment_Optimal : RHI_Image_Layout::Depth_Attachment_Optimal;

            if (render_target_depth_texture_read_only)
            {
                layout = RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal;
            }

            texture->SetLayout(layout, cmd_list);
        }
    }
}
