/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "RHI_Texture.h"
#include "RHI_Device.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        void validate(RHI_PipelineState& pso)
        {
            bool has_shader_compute     = pso.shaders[RHI_Shader_Type::Compute]       ? pso.shaders[RHI_Shader_Type::Compute]->IsCompiled()       : false;
            bool has_shader_vertex      = pso.shaders[RHI_Shader_Type::Vertex]        ? pso.shaders[RHI_Shader_Type::Vertex]->IsCompiled()        : false;
            bool has_shader_hull        = pso.shaders[RHI_Shader_Type::Hull]          ? pso.shaders[RHI_Shader_Type::Hull]->IsCompiled()          : false;
            bool has_shader_domain      = pso.shaders[RHI_Shader_Type::Domain]        ? pso.shaders[RHI_Shader_Type::Domain]->IsCompiled()        : false;
            bool has_shader_pixel       = pso.shaders[RHI_Shader_Type::Pixel]         ? pso.shaders[RHI_Shader_Type::Pixel]->IsCompiled()         : false;
            bool has_shader_mesh        = pso.shaders[RHI_Shader_Type::MeshShader]          ? pso.shaders[RHI_Shader_Type::MeshShader]->IsCompiled()          : false;
            bool has_shader_raygen      = pso.shaders[RHI_Shader_Type::RayGeneration] ? pso.shaders[RHI_Shader_Type::RayGeneration]->IsCompiled() : false;
            bool has_shader_miss        = pso.shaders[RHI_Shader_Type::RayMiss]       ? pso.shaders[RHI_Shader_Type::RayMiss]->IsCompiled()       : false;
            bool has_shader_closest_hit = pso.shaders[RHI_Shader_Type::RayHit]        ? pso.shaders[RHI_Shader_Type::RayHit]->IsCompiled()        : false;
        
            bool has_some_shader = has_shader_compute || has_shader_vertex || has_shader_hull || has_shader_domain || has_shader_pixel || has_shader_mesh || has_shader_raygen || has_shader_miss || has_shader_closest_hit;
            SP_ASSERT_MSG(has_some_shader, "There is no shader set, ensure that it compiled successfully and that it has been set");
        
            bool is_graphics    = (has_shader_vertex || has_shader_hull || has_shader_domain || has_shader_pixel || has_shader_mesh) && !has_shader_compute && !has_shader_raygen && !has_shader_miss && !has_shader_closest_hit;
            bool is_compute     = has_shader_compute && !has_shader_vertex && !has_shader_hull && !has_shader_domain && !has_shader_pixel && !has_shader_mesh && !has_shader_raygen && !has_shader_miss && !has_shader_closest_hit;
            bool is_ray_tracing = (has_shader_raygen || has_shader_miss || has_shader_closest_hit) && !has_shader_compute && !has_shader_vertex && !has_shader_hull && !has_shader_domain && !has_shader_pixel && !has_shader_mesh;
            SP_ASSERT_MSG(is_graphics || is_compute || is_ray_tracing, "Invalid pipeline state type, must be graphics, compute, or ray tracing");
            SP_ASSERT_MSG(!(has_shader_mesh && has_shader_vertex), "Mesh and vertex shaders cannot be combined");
            SP_ASSERT_MSG(!(has_shader_mesh && (has_shader_hull || has_shader_domain)), "Mesh shaders cannot be combined with tessellation");
        
            if (is_graphics)
            {
                bool has_render_target   = pso.render_target_color_textures[0] || pso.render_target_depth_texture;
                bool has_backbuffer      = pso.render_target_swapchain;
                SP_ASSERT_MSG(pso.rasterizer_state && pso.blend_state && pso.depth_stencil_state, "Graphics states are missing");
                SP_ASSERT_MSG(has_render_target || has_backbuffer, "A render target is missing");
                SP_ASSERT(pso.GetWidth() != 0 && pso.GetHeight() != 0);
            }
            else if (is_ray_tracing)
            {
                SP_ASSERT_MSG(has_shader_raygen && has_shader_miss && has_shader_closest_hit, "Ray tracing requires ray generation, miss, and closest hit shaders");
            }
        
            SP_ASSERT_MSG(pso.name != nullptr, "Name your pipeline state");
        }

        uint64_t compute_hash(RHI_PipelineState& pso)
        {
            uint64_t hash = 0;

            hash = rhi_hash_combine(hash, static_cast<uint64_t>(pso.primitive_topology));

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
                {
                    continue;
                }

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
                hash = rhi_hash_combine(hash, static_cast<uint64_t>(pso.is_multiview));
            }

            // cull mode, only d3d12 bakes this into the pso, vulkan keeps the field at its default and uses dynamic state
            hash = rhi_hash_combine(hash, static_cast<uint64_t>(pso.cull_mode));

            return hash;
        }

        void get_dimensions(RHI_PipelineState& pso, uint32_t* width, uint32_t* height)
        {
            SP_ASSERT(width && height);

            *width  = 0;
            *height = 0;

            if (pso.render_target_swapchain)
            {
                if (width)
                {
                    *width  = pso.render_target_swapchain->GetWidth();
                }
                if (height)
                {
                    *height = pso.render_target_swapchain->GetHeight();
                }
            }
            else if (pso.render_target_color_textures[0])
            {
                if (width)
                {
                    *width  = pso.render_target_color_textures[0]->GetWidth();
                }
                if (height)
                {
                    *height = pso.render_target_color_textures[0]->GetHeight();
                }
            }
            else if (pso.render_target_depth_texture)
            {
                if (width)
                {
                    *width  = pso.render_target_depth_texture->GetWidth();
                }
                if (height)
                {
                    *height = pso.render_target_depth_texture->GetHeight();
                }
            }

            if (pso.resolution_scale)
            { 
                *width  = RHI_Device::ScaleDimension(*width);
                *height = RHI_Device::ScaleDimension(*height);
            }
        }

        void apply_graphics_defaults(RHI_PipelineState& pso)
        {
            if (!pso.IsGraphics())
            {
                return;
            }

            if (!pso.rasterizer_state)
            {
                static RHI_RasterizerState rasterizer(RHI_PolygonMode::Solid, true, 0.0f, 0.0f, 0.0f, 3.0f);
                pso.rasterizer_state = &rasterizer;
            }

            if (!pso.blend_state)
            {
                static RHI_BlendState blend(false);
                pso.blend_state = &blend;
            }

            if (!pso.depth_stencil_state)
            {
                static RHI_DepthStencilState depth_off(false, false, RHI_Comparison_Function::Never);
                static RHI_DepthStencilState depth_read_write(true, true, RHI_Comparison_Function::GreaterEqual);
                pso.depth_stencil_state = pso.render_target_depth_texture ? &depth_read_write : &depth_off;
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
        apply_graphics_defaults(*this);
        m_hash = compute_hash(*this);
        get_dimensions(*this, &m_width, &m_height);
        validate(*this);
    }

    void RHI_PipelineState::SetColorTargets(
        RHI_Texture* t0,
        RHI_Texture* t1,
        RHI_Texture* t2,
        RHI_Texture* t3,
        RHI_Texture* t4,
        RHI_Texture* t5,
        RHI_Texture* t6,
        RHI_Texture* t7
    )
    {
        render_target_color_textures = { t0, t1, t2, t3, t4, t5, t6, t7 };
    }

    void RHI_PipelineState::SetDepthTarget(RHI_Texture* texture)
    {
        render_target_depth_texture = texture;
    }

    bool RHI_PipelineState::HasClearValues() const
    {
        if (clear_depth != rhi_depth_load && clear_depth != rhi_depth_dont_care)
        {
            return true;
        }

        if (clear_stencil != rhi_stencil_load && clear_stencil != rhi_stencil_dont_care)
        {
            return true;
        }

        for (const Color& color : clear_color)
        {
            if (color != rhi_color_load && color != rhi_color_dont_care)
            {
                return true;
            }
        }

        return false;
    }

    bool RHI_PipelineState::IsGraphics() const
    {
        return (HasShader(RHI_Shader_Type::Vertex) || HasShader(RHI_Shader_Type::Hull) || HasShader(RHI_Shader_Type::Domain) || HasShader(RHI_Shader_Type::Pixel) || HasShader(RHI_Shader_Type::MeshShader)) &&
               !HasShader(RHI_Shader_Type::Compute) &&
               !HasShader(RHI_Shader_Type::RayGeneration) &&
               !HasShader(RHI_Shader_Type::RayMiss) &&
               !HasShader(RHI_Shader_Type::RayHit);
    }
    
    bool RHI_PipelineState::IsCompute() const
    {
        return HasShader(RHI_Shader_Type::Compute) &&
               !HasShader(RHI_Shader_Type::Vertex) &&
               !HasShader(RHI_Shader_Type::Hull) &&
               !HasShader(RHI_Shader_Type::Domain) &&
               !HasShader(RHI_Shader_Type::Pixel) &&
               !HasShader(RHI_Shader_Type::MeshShader) &&
               !HasShader(RHI_Shader_Type::RayGeneration) &&
               !HasShader(RHI_Shader_Type::RayMiss) &&
               !HasShader(RHI_Shader_Type::RayHit);
    }
    
    bool RHI_PipelineState::IsRayTracing() const
    {
        return (HasShader(RHI_Shader_Type::RayGeneration) || HasShader(RHI_Shader_Type::RayMiss) || HasShader(RHI_Shader_Type::RayHit)) &&
               !HasShader(RHI_Shader_Type::Vertex) &&
               !HasShader(RHI_Shader_Type::Hull) &&
               !HasShader(RHI_Shader_Type::Domain) &&
               !HasShader(RHI_Shader_Type::Pixel) &&
               !HasShader(RHI_Shader_Type::MeshShader) &&
               !HasShader(RHI_Shader_Type::Compute);
    }

    bool RHI_PipelineState::HasMeshShaders() const
    {
        return HasShader(RHI_Shader_Type::MeshShader);
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
