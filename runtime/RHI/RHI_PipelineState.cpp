/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "RHI_SwapChain.h"
#include "RHI_BlendState.h"
#include "RHI_RasterizerState.h"
#include "RHI_DepthStencilState.h"
#include "../Rendering/Renderer.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        void validate(RHI_PipelineState& pso)
        {
            bool has_shader_compute  = pso.shaders[RHI_Shader_Type::Compute] ? pso.shaders[RHI_Shader_Type::Compute]->IsCompiled() : false;
            bool has_shader_vertex   = pso.shaders[RHI_Shader_Type::Vertex]  ? pso.shaders[RHI_Shader_Type::Vertex]->IsCompiled()  : false;
            bool has_shader_pixel    = pso.shaders[RHI_Shader_Type::Pixel]   ? pso.shaders[RHI_Shader_Type::Pixel]->IsCompiled()   : false;
            bool has_render_target   = pso.render_target_color_textures[0] || pso.render_target_depth_texture; // check that there is at least one render target
            bool has_backbuffer      = pso.render_target_swapchain;                                            // check that no both the swapchain and the color render target are active
            bool has_graphics_states = pso.rasterizer_state && pso.blend_state && pso.depth_stencil_state;
            bool is_graphics         = (has_shader_vertex || has_shader_pixel) && !has_shader_compute;
            bool is_compute          = has_shader_compute && (!has_shader_vertex && !has_shader_pixel);

            SP_ASSERT_MSG(has_shader_compute || has_shader_vertex || has_shader_pixel, "There must be at least one shader");
            if (is_graphics)
            {
                SP_ASSERT_MSG(has_graphics_states, "Graphics states are missing");
                SP_ASSERT_MSG(has_render_target || has_backbuffer, "A render target is missing");
                SP_ASSERT(pso.GetWidth() != 0 && pso.GetHeight() != 0);
            }
        }

        uint64_t compute_hash(RHI_PipelineState& pso)
        {
            uint64_t hash = 0;

            hash = rhi_hash_combine(hash, static_cast<uint64_t>(pso.instancing));
            hash = rhi_hash_combine(hash, static_cast<uint64_t>(pso.primitive_toplogy));

            if (pso.render_target_swapchain)
            {
                hash = rhi_hash_combine(hash, static_cast<uint64_t>(pso.render_target_swapchain->GetFormat()));
            }

            if (pso.rasterizer_state)
            {
                hash = rhi_hash_combine(hash, pso.rasterizer_state->GetHash());
            }

            if (pso.blend_state)
            {
                hash = rhi_hash_combine(hash, pso.blend_state->GetHash());
            }

            if (pso.depth_stencil_state)
            {
                hash = rhi_hash_combine(hash, pso.depth_stencil_state->GetHash());
            }

            // shaders
            for (RHI_Shader* shader : pso.shaders)
            {
                if (!shader)
                    continue;

                hash = rhi_hash_combine(hash, shader->GetHash());
            }

            // rt
            {
                // color
                for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                {
                    if (RHI_Texture* texture = pso.render_target_color_textures[i])
                    {
                        hash = rhi_hash_combine(hash, texture->GetObjectId());
                    }
                }

                // depth
                if (pso.render_target_depth_texture)
                {
                    hash = rhi_hash_combine(hash, pso.render_target_depth_texture->GetObjectId());
                }

                // variable rate shading
                if (pso.vrs_input_texture)
                {
                    hash = rhi_hash_combine(hash, pso.vrs_input_texture->GetObjectId());
                }

                hash = rhi_hash_combine(hash, pso.render_target_array_index);
            }

            return hash;
        }

        void get_dimensions(RHI_PipelineState& pso, uint32_t* width, uint32_t* height)
        {
            SP_ASSERT(width && height);

            *width  = 0;
            *height = 0;

            if (pso.render_target_swapchain)
            {
                if (width)  *width  = pso.render_target_swapchain->GetWidth();
                if (height) *height = pso.render_target_swapchain->GetHeight();
            }
            else if (pso.render_target_color_textures[0])
            {
                if (width)  *width  = pso.render_target_color_textures[0]->GetWidth();
                if (height) *height = pso.render_target_color_textures[0]->GetHeight();
            }
            else if (pso.render_target_depth_texture)
            {
                if (width)  *width  = pso.render_target_depth_texture->GetWidth();
                if (height) *height = pso.render_target_depth_texture->GetHeight();
            }

            if (pso.resolution_scale)
            { 
                float resolution_scale = Renderer::GetOption<float>(Renderer_Option::ResolutionScale);
                *width                 = static_cast<uint32_t>(*width * resolution_scale);
                *height                = static_cast<uint32_t>(*height * resolution_scale);
            }
        }
    }

    RHI_PipelineState::RHI_PipelineState()
    {
        clear_color.fill(rhi_color_load);
        render_target_color_textures.fill(nullptr);
    }

    RHI_PipelineState::~RHI_PipelineState()
    {

    }

    void RHI_PipelineState::Prepare()
    {
        m_hash = compute_hash(*this);
        get_dimensions(*this, &m_width, &m_height);
        validate(*this);
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

    bool RHI_PipelineState::IsGraphics() const
    {
        return (HasShader(RHI_Shader_Type::Vertex) || HasShader(RHI_Shader_Type::Pixel)) && !HasShader(RHI_Shader_Type::Compute);
    }

    bool RHI_PipelineState::IsCompute() const
    {
        return HasShader(RHI_Shader_Type::Compute) && !(HasShader(RHI_Shader_Type::Vertex) || HasShader(RHI_Shader_Type::Pixel));
    }

    bool RHI_PipelineState::HasTessellation()
    {
        return HasShader(RHI_Shader_Type::Hull) && HasShader(RHI_Shader_Type::Domain);
    }

    bool RHI_PipelineState::HasShader(const RHI_Shader_Type shader_stage) const
    {
        return shaders[static_cast<uint32_t>(shader_stage)] != nullptr;
    }
}
