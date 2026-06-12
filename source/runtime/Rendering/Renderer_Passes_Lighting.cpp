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

//= INCLUDES ==================================
#include "pch.h"
#include "Renderer.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_AccelerationStructure.h"
#include "../RHI/RHI_Device.h"
#include "../Core/Window.h"
SP_WARNINGS_OFF
#include "bend_sss_cpu.h"
SP_WARNINGS_ON
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::Pass_TransparencyReflectionRefraction(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* tex_frame             = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_ssr               = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_refraction_source = GetRenderTarget(Renderer_RenderTarget::frame_render_opaque);

        cmd_list->BeginTimeblock("transparency_reflection_refraction");
        {
            bool use_ray_traced = cvar_ray_traced_reflections.GetValueAs<bool>();

            if (!m_pass_state.cleared_reflections && !use_ray_traced)
            {
                cmd_list->ClearTexture(tex_ssr, Color::standard_transparent);
                m_pass_state.cleared_reflections = true;
            }

            cmd_list->InsertBarrier(tex_frame, RHI_BarrierType::EnsureReadThenWrite);

            cmd_list->BeginMarker("apply");
            {
                RHI_PipelineState pso;
                pso.name             = "apply_reflections_refraction";
                pso.shaders[Compute] = GetShader(Renderer_Shader::transparency_reflection_refraction_c);

                cmd_list->SetPipelineState(pso);
                SetCommonTextures(cmd_list, eye_layer);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_ssr);               // in - reflection
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_refraction_source); // in - refraction
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular));
                cmd_list->SetTexture(Renderer_BindingsSrv::tex4, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));
                cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_frame);             // out
                // shader uses buffer_pass via world_to_view so push constants must be set
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(tex_frame);
            }
            cmd_list->EndMarker();
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_RayTracedReflections(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        const uint32_t min_rt_dimension = 64;
        if (Window::IsMinimized())
        {
            return;
        }

        RHI_Texture* tex_reflections          = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_reflections_position = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_position);
        RHI_Texture* tex_reflections_normal   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_normal);
        RHI_Texture* tex_reflections_albedo   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_albedo);

        if (tex_reflections_position && (tex_reflections_position->GetWidth() < min_rt_dimension || tex_reflections_position->GetHeight() < min_rt_dimension))
        {
            return;
        }
        // rt reflections owns the entire primary specular lobe at every roughness now that it
        // jitters its ray across the ggx lobe and denoises it, restir pt is diffuse only at the
        // primary (restir_primary_specular_blend returns 0 in restir_reservoir.hlsl), so there is
        // no blend band and no roughness cutoff, the tracer fires for every reflective pixel
        const bool rt_reflections_active = cvar_ray_traced_reflections.GetValueAs<bool>();
        if (!rt_reflections_active || !tex_reflections_position)
        {
            if (!m_pass_state.cleared_rt_reflections)
            {
                cmd_list->ClearTexture(tex_reflections, Color::standard_black);
                m_pass_state.cleared_rt_reflections = true;
            }
            return;
        }
        m_pass_state.cleared_rt_reflections = false;

        cmd_list->BeginTimeblock("ray_traced_reflections");
        {
            RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
            if (!tlas || !tlas->GetRhiResource())
            {
                tex_reflections->SetLayout(RHI_Image_Layout::General, cmd_list);
                cmd_list->ClearTexture(tex_reflections, Color(1.0f, 1.0f, 0.0f, 1.0f));
                cmd_list->EndTimeblock();
                return;
            }

            tex_reflections_position->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_reflections_normal->SetLayout(RHI_Image_Layout::General, cmd_list);
            tex_reflections_albedo->SetLayout(RHI_Image_Layout::General, cmd_list);
            cmd_list->InsertBarrier(tex_reflections_position, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->InsertBarrier(tex_reflections_normal, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->InsertBarrier(tex_reflections_albedo, RHI_BarrierType::EnsureReadThenWrite);

            RHI_PipelineState pso;
            pso.name                   = "ray_traced_reflections";
            pso.shaders[RayGeneration] = GetShader(Renderer_Shader::reflections_ray_generation_r);
            pso.shaders[RayMiss]       = GetShader(Renderer_Shader::reflections_ray_miss_r);
            pso.shaders[RayHit]        = GetShader(Renderer_Shader::reflections_ray_hit_r);
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list, eye_layer);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            
            RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
            tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
            
            // reset and bind the per-hit geometry info ring as uav
            GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
            cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));

            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex),  tex_reflections_position, rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_reflections_normal,   rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), tex_reflections_albedo,   rhi_all_mips, 0, true);

            cmd_list->PushConstants(m_pcb_pass_cpu);

            uint32_t width  = tex_reflections_position->GetWidth();
            uint32_t height = tex_reflections_position->GetHeight();
            cmd_list->TraceRays(width, height);
            cmd_list->InsertBarrier(tex_reflections_position, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_reflections_normal, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_reflections_albedo, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }
    
    void Renderer::Pass_Composite_RayTracedReflections(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        // rt reflections shades the full primary specular lobe at every roughness, restir pt is
        // diffuse only at the primary (restir_primary_specular_blend returns 0 in
        // restir_reservoir.hlsl) so the two never double count specular
        if (!cvar_ray_traced_reflections.GetValueAs<bool>())
        {
            return;
        }
            
        RHI_Texture* tex_reflections          = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_reflections_position = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_position);
        RHI_Texture* tex_reflections_normal   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_normal);
        RHI_Texture* tex_reflections_albedo   = GetRenderTarget(Renderer_RenderTarget::gbuffer_reflections_albedo);
        RHI_Texture* tex_skysphere            = GetRenderTarget(Renderer_RenderTarget::skysphere);
        
        if (!tex_reflections_position)
        {
            return;
        }

        cmd_list->BeginTimeblock("light_reflections");
        {
            tex_reflections->SetLayout(RHI_Image_Layout::General, cmd_list);
            cmd_list->InsertBarrier(tex_reflections, RHI_BarrierType::EnsureReadThenWrite);
            
            tex_reflections_position->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_reflections_normal->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_reflections_albedo->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);

            RHI_PipelineState pso;
            pso.name             = "light_reflections";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_reflections_c);
            cmd_list->SetPipelineState(pso);
            
            SetCommonTextures(cmd_list, eye_layer);
            
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_reflections_position);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_reflections_normal);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_reflections_albedo);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4, tex_skysphere);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_reflections, rhi_all_mips, 0, true);

            // bind tlas for inline ray traced shadows at the hit, every light type uses this
            // path so reflections darken correctly inside enclosed or shadowed geometry
            if (RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure())
            {
                if (tlas->GetRhiResource())
                {
                    cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
                }
            }
            
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(m_count_active_lights), static_cast<float>(tex_skysphere->GetMipCount()));
            cmd_list->PushConstants(m_pcb_pass_cpu);
            
            cmd_list->Dispatch(tex_reflections);
            cmd_list->InsertBarrier(tex_reflections, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Denoise_RayTracedReflections(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        // the reflection ray is jittered across the ggx lobe per frame (see ray_traced_reflections.hlsl)
        // so the raw reflections texture is noisy on rough surfaces, this spatiotemporal denoiser
        // reconstructs it into a clean, roughness proportional blur, smooth surfaces pass through
        // untouched because the spatial kernel strength ramps from zero with roughness
        if (Window::IsMinimized())
        {
            return;
        }

        if (!cvar_ray_traced_reflections.GetValueAs<bool>())
        {
            return;
        }

        // debug bypass, leaves the raw stochastic reflection visible for a/b inspection
        if (!cvar_ray_traced_reflections_denoise.GetValueAs<bool>())
        {
            return;
        }

        RHI_Texture* tex_reflections     = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_history         = GetRenderTarget(Renderer_RenderTarget::reflections_history);
        RHI_Texture* tex_moments         = GetRenderTarget(Renderer_RenderTarget::reflections_moments);
        RHI_Texture* tex_moments_history = GetRenderTarget(Renderer_RenderTarget::reflections_moments_history);
        RHI_Texture* tex_ping            = GetRenderTarget(Renderer_RenderTarget::reflections_ping);
        RHI_Texture* tex_depth_previous  = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous);

        if (!tex_reflections || !tex_history || !tex_moments || !tex_moments_history || !tex_ping)
        {
            return;
        }

        const uint32_t min_rt_dimension = 64;
        if (tex_reflections->GetWidth() < min_rt_dimension || tex_reflections->GetHeight() < min_rt_dimension)
        {
            return;
        }

        RHI_Shader* shader_temporal = GetShader(Renderer_Shader::reflections_denoise_temporal_c);
        RHI_Shader* shader_spatial  = GetShader(Renderer_Shader::reflections_denoise_spatial_c);
        if (!shader_temporal || !shader_spatial || !shader_temporal->IsCompiled() || !shader_spatial->IsCompiled())
        {
            return;
        }

        // temporal accumulation, raw reflections in -> ping holds (accumulated color, variance),
        // moments out for the next frame's ema, history and previous depth gate the reprojection
        cmd_list->BeginTimeblock("reflections_denoise_temporal");
        {
            RHI_PipelineState pso;
            pso.name             = "reflections_denoise_temporal";
            pso.shaders[Compute] = shader_temporal;
            cmd_list->SetPipelineState(pso);

            cmd_list->InsertBarrier(tex_ping,    RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->InsertBarrier(tex_moments, RHI_BarrierType::EnsureReadThenWrite);
            SetCommonTextures(cmd_list, eye_layer);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_reflections);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_history);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_moments_history);
            if (tex_depth_previous)
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex4, tex_depth_previous);
            }
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_ping);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_moments);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(tex_reflections);
            cmd_list->InsertBarrier(tex_ping,    RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_moments, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();

        // a-trous ping pong, step width doubles each level, the routing alternates ping and the
        // reflections texture so the final write lands in reflections which is consumed downstream
        struct denoise_stage { const char* name; float radius; RHI_Texture* src; RHI_Texture* dst; };
        const denoise_stage stages[3] =
        {
            { "reflections_denoise_spatial",   1.0f, tex_ping,        tex_reflections },
            { "reflections_denoise_spatial_2", 2.0f, tex_reflections, tex_ping        },
            { "reflections_denoise_spatial_3", 4.0f, tex_ping,        tex_reflections },
        };

        for (const denoise_stage& s : stages)
        {
            cmd_list->BeginTimeblock(s.name);
            {
                RHI_PipelineState pso;
                pso.name             = s.name;
                pso.shaders[Compute] = shader_spatial;
                cmd_list->SetPipelineState(pso);

                m_pcb_pass_cpu.set_f3_value(s.radius);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                cmd_list->InsertBarrier(s.dst, RHI_BarrierType::EnsureReadThenWrite);
                SetCommonTextures(cmd_list, eye_layer);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, s.src);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, s.dst);
                cmd_list->Dispatch(tex_reflections);
                cmd_list->InsertBarrier(s.dst, RHI_BarrierType::EnsureWriteThenRead);
            }
            cmd_list->EndTimeblock();
        }

        // feed the denoised reflection back as next frame's history and copy the moments forward
        cmd_list->InsertBarrier(tex_history, RHI_BarrierType::EnsureReadThenWrite);
        Pass_Blit(cmd_list, tex_reflections, tex_history);
        cmd_list->InsertBarrier(tex_history,     RHI_BarrierType::EnsureWriteThenRead);
        cmd_list->InsertBarrier(tex_reflections, RHI_BarrierType::EnsureWriteThenRead);

        cmd_list->InsertBarrier(tex_moments_history, RHI_BarrierType::EnsureReadThenWrite);
        Pass_Blit(cmd_list, tex_moments, tex_moments_history);
        cmd_list->InsertBarrier(tex_moments_history, RHI_BarrierType::EnsureWriteThenRead);
    }

    void Renderer::Pass_RayTracedShadows(RHI_CommandList* cmd_list)
    {
        const uint32_t min_rt_dimension = 64;
        if (Window::IsMinimized())
        {
            return;
        }

        RHI_Texture* tex_shadows = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows);

        if (tex_shadows && (tex_shadows->GetWidth() < min_rt_dimension || tex_shadows->GetHeight() < min_rt_dimension))
        {
            return;
        }
        // restir pt traces its own per-light shadow rays inline in the spatial pass, so this pass would
        // be redundant work whose output texture nobody reads, skip it and clear to white once
        bool restir_pt_owns_shadows = cvar_restir_pt.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing();
        if (!cvar_ray_traced_shadows.GetValueAs<bool>() || restir_pt_owns_shadows)
        {
            if (!m_pass_state.cleared_rt_shadows)
            {
                cmd_list->ClearTexture(tex_shadows, Color::standard_white);
                m_pass_state.cleared_rt_shadows = true;
            }
            return;
        }
        m_pass_state.cleared_rt_shadows = false;
        
        if (!RHI_Device::IsSupportedRayTracing())
        {
            return;
        }
            
        RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
        if (!tlas)
        {
            return;
        }
        
        RHI_Shader* shader_rgen = GetShader(Renderer_Shader::shadows_ray_generation_r);
        RHI_Shader* shader_miss = GetShader(Renderer_Shader::shadows_ray_miss_r);
        RHI_Shader* shader_hit  = GetShader(Renderer_Shader::shadows_ray_hit_r);
        if (!shader_rgen || !shader_miss || !shader_hit)
        {
            return;
        }
        if (!shader_rgen->IsCompiled() || !shader_miss->IsCompiled() || !shader_hit->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("ray_traced_shadows");
        {
            RHI_PipelineState pso;
            pso.name                   = "ray_traced_shadows";
            pso.shaders[RayGeneration] = shader_rgen;
            pso.shaders[RayMiss]       = shader_miss;
            pso.shaders[RayHit]        = shader_hit;
            cmd_list->SetPipelineState(pso);
            
            SetCommonTextures(cmd_list);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_shadows, rhi_all_mips, 0, true);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            uint32_t width  = tex_shadows->GetWidth();
            uint32_t height = tex_shadows->GetHeight();
            cmd_list->TraceRays(width, height);
            cmd_list->InsertBarrier(tex_shadows, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ReSTIR_TraceInitial(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* tex_skysphere, RHI_Texture* const* reservoirs, uint32_t width, uint32_t height)
    {
        // amd tdrs when tracerays dispatches with uninitialized push constants, all raygen passes must push constants
        cmd_list->BeginTimeblock("restir_pt_initial");
        {
            RHI_PipelineState pso;
            pso.name                   = "restir_pt_initial";
            pso.shaders[RayGeneration] = GetShader(Renderer_Shader::restir_pt_ray_generation_r);
            pso.shaders[RayMiss]       = GetShader(Renderer_Shader::restir_pt_ray_miss_r);
            pso.shaders[RayHit]        = GetShader(Renderer_Shader::restir_pt_ray_hit_r);
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);

            tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);

            // reset and bind the per-hit geometry info ring as uav
            GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
            cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));

            // emissive triangle nee pool, prefix sum and per-triangle data populated by
            // BuildEmissiveTriangleNeePool, count comes through buffer_frame.restir_pt_emissive_tri_count
            cmd_list->SetBuffer(Renderer_BindingsUav::emissive_triangles, GetBuffer(Renderer_Buffer::EmissiveTriangles));

            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);

            for (uint32_t i = 0; i < 6; i++)
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0) + i, reservoirs[i], rhi_all_mips, 0, true);

            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->TraceRays(width, height);

            for (uint32_t i = 0; i < 6; i++)
                cmd_list->InsertBarrier(reservoirs[i], RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ReSTIR_Temporal(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_prev, uint32_t dispatch_x, uint32_t dispatch_y)
    {
        RHI_Shader* shader_temporal = GetShader(Renderer_Shader::restir_pt_temporal_c);
        if (!shader_temporal || !shader_temporal->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("restir_pt_temporal");
        {
            RHI_PipelineState pso;
            pso.name             = "restir_pt_temporal";
            pso.shaders[Compute] = shader_temporal;
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list);

            // tlas is required for the lin 2022 sample validation step inside the temporal pass,
            // periodically re-traces the chosen reservoir's primary->rc visibility ray to kill
            // stale samples that no longer reach their reconnection vertex (moved lights, opened
            // doors, broken geometry), validation cadence is set by get_restir_validation_period
            if (tlas)
            {
                cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            }

            // bind the per renderable geometry info ring and the emissive triangle nee pool so
            // the random replay shift can do its full suffix retrace inline, the retrace pulls
            // material / geometry data via the bindless arrays which require geometry_infos to
            // be bound, the emissive triangle buffer is bound here too so future use of emtri
            // nee inside the inline retrace (if added) finds the pool populated
            cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info,      GetBuffer(Renderer_Buffer::GeometryInfo));
            cmd_list->SetBuffer(Renderer_BindingsUav::emissive_triangles, GetBuffer(Renderer_Buffer::EmissiveTriangles));

            for (uint32_t i = 0; i < 6; i++)
            {
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs_prev[i]);
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0)      + i, reservoirs[i], rhi_all_mips, 0, true);
            }

            // bind previous frame depth so the temporal validity gate compares against the actual
            // prior surface depth at prev_uv instead of the current depth at prev_uv, the latter
            // mistreats moving objects as disocclusion and is the dominant cause of motion ghosting
            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, depth_prev);
            }

            // previous frame normals for the same gate
            if (RHI_Texture* normal_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal_previous))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex5, normal_prev);
            }

            // skysphere on tex3 so the random replay shift sees the same sky the initial trace did
            if (RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere))
            {
                tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
            }

            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            // pipeline layout always declares the push constant range, vulkan validation requires
            // a vkCmdPushConstants call before every dispatch even when the shader does not read
            // any of the per pass values, this dispatches with whatever the renderer tagged
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);

            for (uint32_t i = 0; i < 6; i++)
                cmd_list->InsertBarrier(reservoirs[i], RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }

    bool Renderer::Pass_ReSTIR_SpatialPair(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_spatial, uint32_t dispatch_x, uint32_t dispatch_y)
    {
        RHI_Shader* shader_spatial = GetShader(Renderer_Shader::restir_pt_spatial_c);
        if (!shader_spatial || !shader_spatial->IsCompiled())
        {
            return false;
        }

        // three a-trous-style passes with progressively expanding radii (lin 2022 §6.2),
        // the first pass operates on a tight neighborhood for sharp signal preservation, the
        // second and third widen the kernel so distant neighbors feed the canonical
        // reservoir, radius progression is 1x, 1.6x, 2.56x (1.6 ^ pass_index) so 3 passes
        // cover the same effective radius as the old 4 level denoiser but in the reservoir
        // domain where the m cap clamps influence to bounded confidence per neighbor
        // ping pong routing leaves the final output in reservoirs_spatial which the swap
        // below moves back into the canonical reservoirs slot
        struct stage { const char* name; float param; RHI_Texture* const* src; RHI_Texture* const* dst; };
        const stage stages[3] = {
            { "restir_pt_spatial",   0.0f, reservoirs,         reservoirs_spatial }, // tight
            { "restir_pt_spatial_2", 1.0f, reservoirs_spatial, reservoirs         }, // wide
            { "restir_pt_spatial_3", 2.0f, reservoirs,         reservoirs_spatial }  // widest
        };

        for (const stage& s : stages)
        {
            cmd_list->BeginTimeblock(s.name);
            {
                RHI_PipelineState pso;
                pso.name             = s.name;
                pso.shaders[Compute] = shader_spatial;
                cmd_list->SetPipelineState(pso);

                m_pcb_pass_cpu.set_f3_value(s.param);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                SetCommonTextures(cmd_list);
                cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);

                // bind the per renderable geometry info ring and the emissive triangle nee pool so
                // the random replay shift can do its full suffix retrace inline, see the matching
                // comment in Pass_ReSTIR_Temporal for details
                cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info,      GetBuffer(Renderer_Buffer::GeometryInfo));
                cmd_list->SetBuffer(Renderer_BindingsUav::emissive_triangles, GetBuffer(Renderer_Buffer::EmissiveTriangles));

                // skysphere on tex3 so the random replay shift sees the same sky the initial trace did
                if (RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere))
                {
                    tex_skysphere->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
                }

                for (uint32_t i = 0; i < 6; i++)
                {
                    cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, s.src[i]);
                    cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0)      + i, s.dst[i], rhi_all_mips, 0, true);
                }

                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
                cmd_list->Dispatch(dispatch_x, dispatch_y, 1);

                for (uint32_t i = 0; i < 6; i++)
                    cmd_list->InsertBarrier(s.dst[i], RHI_BarrierType::EnsureWriteThenRead);
                cmd_list->InsertBarrier(tex_gi, RHI_BarrierType::EnsureWriteThenRead);
            }
            cmd_list->EndTimeblock();
        }

        // 3 stages end with the latest spatial output sitting in reservoirs_spatial, swap the
        // canonical and spatial render target pointers so subsequent passes reading
        // restir_reservoir0..5 see the freshly filtered reservoirs, the pointer swap is free
        // compared to blitting 6 reservoir textures and keeps the per-frame swap chain stable
        auto& render_targets = GetRenderTargets();
        for (uint32_t i = 0; i < 6; i++)
        {
            uint32_t idx_cur     = static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0)         + i;
            uint32_t idx_spatial = static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir_spatial0) + i;
            swap(render_targets[idx_cur], render_targets[idx_spatial]);
        }

        return true;
    }

    void Renderer::Pass_ReSTIR_SwapReservoirs()
    {
        auto& render_targets = GetRenderTargets();
        for (uint32_t i = 0; i < 6; i++)
        {
            uint32_t idx_cur  = static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0)      + i;
            uint32_t idx_prev = static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir_prev0) + i;
            swap(render_targets[idx_cur], render_targets[idx_prev]);
        }
    }

    // cpu only pointer swap between the current and previous gbuffer depth and normal slots,
    // must be called at frame end after every consumer of the current targets has finished
    // recording, so the slot that just held this frame's data becomes next frame's *_previous
    // and the slot with stale data becomes next frame's current target which the gbuffer pass
    // overwrites, this avoids the queue compatibility issue of cmd blit on the compute queue
    // and removes two full screen copies from the per frame cost
    void Renderer::Pass_ReSTIR_SwapGBufferHistory()
    {
        auto& render_targets = GetRenderTargets();

        uint32_t idx_depth      = static_cast<uint32_t>(Renderer_RenderTarget::gbuffer_depth);
        uint32_t idx_depth_prev = static_cast<uint32_t>(Renderer_RenderTarget::gbuffer_depth_previous);
        if (render_targets[idx_depth] && render_targets[idx_depth_prev])
        {
            swap(render_targets[idx_depth], render_targets[idx_depth_prev]);
        }

        uint32_t idx_normal      = static_cast<uint32_t>(Renderer_RenderTarget::gbuffer_normal);
        uint32_t idx_normal_prev = static_cast<uint32_t>(Renderer_RenderTarget::gbuffer_normal_previous);
        if (render_targets[idx_normal] && render_targets[idx_normal_prev])
        {
            swap(render_targets[idx_normal], render_targets[idx_normal_prev]);
        }
    }

    void Renderer::Pass_ReSTIR_PathTracing(RHI_CommandList* cmd_list)
    {
        if (Window::IsMinimized())
            return;

        RHI_Texture* tex_gi     = GetRenderTarget(Renderer_RenderTarget::restir_output);
        RHI_Texture* reservoir0 = GetRenderTarget(Renderer_RenderTarget::restir_reservoir0);

        // restir resources are gated by cvar_restir_pt, nothing to do when they are not allocated
        if (!tex_gi)
            return;

        const uint32_t min_rt_dimension = 64;
        if (tex_gi->GetWidth() < min_rt_dimension || tex_gi->GetHeight() < min_rt_dimension)
            return;

        if (!cvar_restir_pt.GetValueAs<bool>() || !RHI_Device::IsSupportedRayTracing() || !reservoir0)
        {
            if (!m_pass_state.cleared_restir)
            {
                cmd_list->ClearTexture(tex_gi, Color::standard_black);
                m_pass_state.cleared_restir = true;
            }
            return;
        }
        m_pass_state.cleared_restir = false;

        RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
        if (!tlas)
            return;

        RHI_Shader* shader_rgen = GetShader(Renderer_Shader::restir_pt_ray_generation_r);
        RHI_Shader* shader_miss = GetShader(Renderer_Shader::restir_pt_ray_miss_r);
        RHI_Shader* shader_hit  = GetShader(Renderer_Shader::restir_pt_ray_hit_r);
        if (!shader_rgen || !shader_miss || !shader_hit)
        {
            return;
        }
        if (!shader_rgen->IsCompiled() || !shader_miss->IsCompiled() || !shader_hit->IsCompiled())
        {
            return;
        }

        RHI_Texture* reservoirs[6];
        RHI_Texture* reservoirs_prev[6];
        RHI_Texture* reservoirs_spatial[6];
        for (uint32_t i = 0; i < 6; i++)
        {
            reservoirs[i]         = GetRenderTarget(static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0)         + i));
            reservoirs_prev[i]    = GetRenderTarget(static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir_prev0)    + i));
            reservoirs_spatial[i] = GetRenderTarget(static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir_spatial0) + i));
        }
        RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
        const uint32_t width       = tex_gi->GetWidth();
        const uint32_t height      = tex_gi->GetHeight();
        const uint32_t dispatch_x  = (width  + 7) / 8;
        const uint32_t dispatch_y  = (height + 7) / 8;

        // one-shot clear of every reservoir slot after (re)allocation, the rhi cannot guarantee
        // freshly-created textures contain zero-initialized memory and the temporal pass would
        // otherwise read garbage on the first frame; is_reservoir_valid catches most of it but
        // a stray valid-looking sample can propagate noise for several frames before the cap
        // ratchets it out
        //
        // gbuffer_depth_previous is also cleared here on the same one-shot, on the very first
        // frame of a restir enable the temporal pass calls evaluate_disocclusion which reads
        // tex_depth_previous before the end-of-frame swap has copied anything into it,
        // initializing to 1.0 (far) makes disocclusion fail closed so the first frame falls
        // back to the canonical sample instead of merging against garbage history
        if (!m_pass_state.restir_reservoirs_initialized)
        {
            for (uint32_t i = 0; i < 6; i++)
            {
                cmd_list->ClearTexture(reservoirs[i],         Color::standard_transparent);
                cmd_list->ClearTexture(reservoirs_prev[i],    Color::standard_transparent);
                cmd_list->ClearTexture(reservoirs_spatial[i], Color::standard_transparent);
            }

            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                cmd_list->ClearTexture(depth_prev, Color::standard_white, 1.0f);
            }

            // zero normals make the disocclusion gate fail closed until real history exists
            if (RHI_Texture* normal_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal_previous))
            {
                cmd_list->ClearTexture(normal_prev, Color::standard_black);
            }

            m_pass_state.restir_reservoirs_initialized = true;
        }

        Pass_ReSTIR_TraceInitial(cmd_list, tlas, tex_gi, tex_skysphere, reservoirs, width, height);
        Pass_ReSTIR_Temporal(cmd_list, tlas, tex_gi, reservoirs, reservoirs_prev, dispatch_x, dispatch_y);
        const bool ran_spatial = Pass_ReSTIR_SpatialPair(cmd_list, tlas, tex_gi, reservoirs, reservoirs_spatial, dispatch_x, dispatch_y);

        if (!ran_spatial)
        {
            cmd_list->InsertBarrier(tex_gi, RHI_BarrierType::EnsureWriteThenRead);
        }

        Pass_ReSTIR_SwapReservoirs();
    }

    void Renderer::Pass_ReSTIR_Denoising(RHI_CommandList* cmd_list)
    {
        if (Window::IsMinimized())
            return;

        RHI_Texture* tex_gi_raw         = GetRenderTarget(Renderer_RenderTarget::restir_output);
        RHI_Texture* tex_gi_denoised    = GetRenderTarget(Renderer_RenderTarget::restir_denoised);
        RHI_Texture* tex_gi_history     = GetRenderTarget(Renderer_RenderTarget::restir_denoised_history);
        RHI_Texture* tex_gi_ping        = GetRenderTarget(Renderer_RenderTarget::restir_denoised_ping);
        RHI_Texture* tex_moments        = GetRenderTarget(Renderer_RenderTarget::restir_denoised_moments);
        RHI_Texture* tex_moments_hist   = GetRenderTarget(Renderer_RenderTarget::restir_denoised_moments_history);

        // restir resources are gated by cvar_restir_pt, nothing to do when they are not allocated
        if (!tex_gi_raw || !tex_gi_denoised || !tex_gi_history || !tex_gi_ping || !tex_moments || !tex_moments_hist)
            return;

        const uint32_t min_rt_dimension = 64;
        if (tex_gi_raw->GetWidth() < min_rt_dimension || tex_gi_raw->GetHeight() < min_rt_dimension)
            return;

        if (!cvar_restir_pt.GetValueAs<bool>() || !RHI_Device::IsSupportedRayTracing())
        {
            Pass_BlitRestirFallback(cmd_list, tex_gi_raw, tex_gi_denoised, tex_gi_history, true);
            return;
        }

        // debug view, bypass the svgf denoiser entirely and blit the raw gi estimator straight
        // through so the composited image shows the un-denoised, un-demodulated estimator, this
        // isolates resampling artifacts (fireflies in the estimator) from post chain artifacts
        if (cvar_restir_pt_debug.GetValueAs<bool>())
        {
            Pass_BlitRestirFallback(cmd_list, tex_gi_raw, tex_gi_denoised, tex_gi_history, false);
            return;
        }

        RHI_Shader* shader_temporal = GetShader(Renderer_Shader::restir_pt_denoise_temporal_c);
        RHI_Shader* shader_spatial  = GetShader(Renderer_Shader::restir_pt_denoise_spatial_c);
        if (!shader_temporal || !shader_spatial || !shader_temporal->IsCompiled() || !shader_spatial->IsCompiled())
        {
            Pass_BlitRestirFallback(cmd_list, tex_gi_raw, tex_gi_denoised, tex_gi_history, false);
            return;
        }

        RHI_Texture* reservoirs[6];
        for (uint32_t i = 0; i < 6; i++)
        {
            reservoirs[i] = GetRenderTarget(static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir_prev0) + i));
        }

        const uint32_t thread_group_count_x = 8;
        const uint32_t thread_group_count_y = 8;
        const uint32_t width                = tex_gi_raw->GetWidth();
        const uint32_t height               = tex_gi_raw->GetHeight();
        const uint32_t dispatch_x           = (width + thread_group_count_x - 1) / thread_group_count_x;
        const uint32_t dispatch_y           = (height + thread_group_count_y - 1) / thread_group_count_y;

        cmd_list->BeginTimeblock("restir_pt_denoise_temporal");
        {
            RHI_PipelineState pso;
            pso.name             = "restir_pt_denoise_temporal";
            pso.shaders[Compute] = shader_temporal;
            cmd_list->SetPipelineState(pso);

            cmd_list->InsertBarrier(tex_gi_denoised, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->InsertBarrier(tex_moments,     RHI_BarrierType::EnsureReadThenWrite);
            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_gi_raw);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_gi_history);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_moments_hist);
            // bind previous frame depth on tex4 so the denoiser's disocclusion gate uses the
            // same prev-depth-vs-reprojected-expected-depth formulation as the reservoir
            // temporal pass, fixes asymmetric ghosting between the two passes on moving
            // objects (the previous code compared current depth at prev_uv vs current pixel
            // depth which mistreats dynamic surfaces as disocclusions)
            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex4, depth_prev);
            }
            // previous frame normals on tex5 for the same disocclusion gate
            if (RHI_Texture* normal_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal_previous))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex5, normal_prev);
            }
            for (uint32_t i = 0; i < 6; i++)
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs[i]);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_gi_denoised);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_moments);
            // pipeline layout still carries the push constant range so vulkan requires a push
            // before each dispatch even though this shader does not read any per pass values
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
            cmd_list->InsertBarrier(tex_gi_denoised, RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(tex_moments,     RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();

        // ping pong spatial passes, parameter is the kernel radius, src and dst alternate
        // a-trous step doubling at each level matches lin 2022's reference svgf, 4 levels
        // (1, 2, 4, 8) give an effective tap radius of 1+3+7+15 = 26 pixels which is the
        // standard schied 2017 / lin 2022 configuration, the previous 3 level (1, 2, 4)
        // attempt to preserve contact detail left visible firefly clumps in the output
        // because the kernel never reached the scale where bright outliers get averaged
        // down, with the reservoir firefly cap and 4 levels back we trade a small amount
        // of crevice softness for a stable image, ping pong routing ends with the final
        // write in tex_gi_denoised (even pass count) so no extra blit is needed
        struct denoise_stage { const char* name; float radius; RHI_Texture* src; RHI_Texture* dst; };
        const denoise_stage stages[4] = {
            { "restir_pt_denoise_spatial",   1.0f, tex_gi_denoised, tex_gi_ping     },
            { "restir_pt_denoise_spatial_2", 2.0f, tex_gi_ping,     tex_gi_denoised },
            { "restir_pt_denoise_spatial_3", 4.0f, tex_gi_denoised, tex_gi_ping     },
            { "restir_pt_denoise_spatial_4", 8.0f, tex_gi_ping,     tex_gi_denoised },
        };

        for (const denoise_stage& s : stages)
        {
            cmd_list->BeginTimeblock(s.name);
            {
                RHI_PipelineState pso;
                pso.name             = s.name;
                pso.shaders[Compute] = shader_spatial;
                cmd_list->SetPipelineState(pso);

                m_pcb_pass_cpu.set_f3_value(s.radius);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                cmd_list->InsertBarrier(s.dst, RHI_BarrierType::EnsureReadThenWrite);
                SetCommonTextures(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  s.src);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_moments);
                for (uint32_t i = 0; i < 6; i++)
                    cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs[i]);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, s.dst);
                cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
                cmd_list->InsertBarrier(s.dst, RHI_BarrierType::EnsureWriteThenRead);
            }
            cmd_list->EndTimeblock();
        }

        // 4 stages with even count means the final write landed in tex_gi_denoised, no extra
        // blit needed; blit denoised into history for the next frame's svgf temporal pass to
        // use as its ema accumulator
        cmd_list->InsertBarrier(tex_gi_history, RHI_BarrierType::EnsureReadThenWrite);
        Pass_Blit(cmd_list, tex_gi_denoised, tex_gi_history);
        cmd_list->InsertBarrier(tex_gi_history,  RHI_BarrierType::EnsureWriteThenRead);
        cmd_list->InsertBarrier(tex_gi_denoised, RHI_BarrierType::EnsureWriteThenRead);

        // moments history copy for the next frame's svgf temporal accumulation
        cmd_list->InsertBarrier(tex_moments_hist, RHI_BarrierType::EnsureReadThenWrite);
        Pass_Blit(cmd_list, tex_moments, tex_moments_hist);
        cmd_list->InsertBarrier(tex_moments_hist, RHI_BarrierType::EnsureWriteThenRead);
    }

    void Renderer::Pass_ScreenSpaceShadows(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_sss = GetRenderTarget(Renderer_RenderTarget::sss);

        // screen space contact shadows complement the primary shadow term, light.hlsl now combines
        // them with a min() on top of whichever primary occlusion source is active (rasterized
        // shadow map OR ray traced shadow), so this pass runs whenever any directional light has
        // ShadowsScreenSpace enabled, regardless of the rt shadow / restir state, restir path
        // tracing still owns the full direct term at the primary so we skip there to avoid wasted
        // work since light.hlsl's surface lighting block is bypassed under restir
        if (cvar_restir_pt.GetValueAs<bool>())
        {
            return;
        }

        cmd_list->BeginTimeblock("screen_space_shadows");
        {
            cmd_list->InsertBarrier(tex_sss, RHI_BarrierType::EnsureReadThenWrite);

            RHI_PipelineState pso;
            pso.name             = "screen_space_shadows";
            pso.shaders[Compute] = GetShader(Renderer_Shader::sss_c_bend);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, tex_sss);
            uint32_t array_slice_index = 0;
            for (Entity* entity : World::GetEntities())
            {
                if (Light* light = entity->GetComponent<Light>())
                {
                    if (light->GetLightType() != LightType::Directional || !light->GetFlag(LightFlags::Shadows) || !light->GetFlag(LightFlags::ShadowsScreenSpace) || light->GetIntensityRadiometric() == 0.0f)
                    {
                        continue;
                    }

                    if (array_slice_index == tex_sss->GetDepth())
                    {
                        SP_LOG_WARNING("Render target has reached the maximum number of lights it can hold");
                        break;
                    }

                    math::Matrix view_projection = World::GetCamera()->GetViewProjectionMatrix();
                    Vector4 p = {};

                    // todo: why do we need to flip sign?
                    p = Vector4(-light->GetEntity()->GetForward(), 0.0f) * view_projection;

                    float in_light_projection[]      = { p.x, p.y, p.z, p.w };
                    int32_t in_viewport_size[]       = { static_cast<int32_t>(tex_sss->GetWidth()), static_cast<int32_t>(tex_sss->GetHeight()) };
                    int32_t in_min_render_bounds[]   = { 0, 0 };
                    int32_t in_max_render_bounds[]   = { static_cast<int32_t>(tex_sss->GetWidth()), static_cast<int32_t>(tex_sss->GetHeight()) };
                    Bend::DispatchList dispatch_list = Bend::BuildDispatchList(in_light_projection, in_viewport_size, in_min_render_bounds, in_max_render_bounds, false);

                    m_pcb_pass_cpu.set_f4_value
                    (
                        dispatch_list.LightCoordinate_Shader[0],
                        dispatch_list.LightCoordinate_Shader[1],
                        dispatch_list.LightCoordinate_Shader[2],
                        dispatch_list.LightCoordinate_Shader[3]
                    );

                    light->SetScreenSpaceShadowsSliceIndex(array_slice_index);
                    float near = 1.0f;
                    float far  = 0.0f;
                    m_pcb_pass_cpu.set_f3_value(near, far, static_cast<float>(array_slice_index++));
                    m_pcb_pass_cpu.set_f3_value2(1.0f / tex_sss->GetWidth(), 1.0f / tex_sss->GetHeight(), 0.0f);

                    for (int32_t dispatch_index = 0; dispatch_index < dispatch_list.DispatchCount; ++dispatch_index)
                    {
                        const Bend::DispatchData& dispatch = dispatch_list.Dispatch[dispatch_index];
                        m_pcb_pass_cpu.set_f2_value(static_cast<float>(dispatch.WaveOffset_Shader[0]), static_cast<float>(dispatch.WaveOffset_Shader[1]));
                        cmd_list->PushConstants(m_pcb_pass_cpu);
                        cmd_list->Dispatch(dispatch.WaveCount[0], dispatch.WaveCount[1], dispatch.WaveCount[2]);
                    }

                    cmd_list->InsertBarrier(tex_sss, RHI_BarrierType::EnsureWriteThenRead);
                }
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_LightClusterAssign(RHI_CommandList* cmd_list)
    {
        cmd_list->BeginTimeblock("light_cluster_assign");
        {
            // clear the overflow counter every frame, the shader bumps it atomically once per overflowing cluster
            // and the light pass / cpu telemetry reads it back as the count for this frame
            {
                const uint32_t zero = 0;
                cmd_list->UpdateBuffer(GetBuffer(Renderer_Buffer::ClusterStats), 0, sizeof(uint32_t), &zero);
            }

            // when only the directional sun is active there are no clustered lights, the light shader guards
            // with total_lights > 1 so the grid contents are unread, skip the dispatch entirely
            if (m_count_active_lights <= 1)
            {
                cmd_list->EndTimeblock();
                return;
            }

            RHI_PipelineState pso;
            pso.name             = "light_cluster_assign";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_cluster_assign_c);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_light_grid,    GetBuffer(Renderer_Buffer::ClusterLightGrid));
            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_light_indices, GetBuffer(Renderer_Buffer::ClusterLightIndices));
            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_stats,         GetBuffer(Renderer_Buffer::ClusterStats));

            // single dispatch even in vr stereo, the grid lives in the left eye view-projection space which contains
            // the right eye to within the ipd, far less than one cluster tile width, force eye_index = 0 so the shader
            // helpers (get_view, get_projection, world_to_view) select the left eye matrices for the assign math
            const uint32_t saved_eye = m_pcb_pass_cpu.eye_index;
            m_pcb_pass_cpu.eye_index = 0;
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(CLUSTER_COUNT_X, CLUSTER_COUNT_Y, CLUSTER_COUNT_Z);
            m_pcb_pass_cpu.eye_index = saved_eye;

            // the light pass reads both buffers via the cross queue timeline, the barrier keeps
            // any later compute work that touches them on the same queue correctly ordered
            cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::ClusterLightGrid));
            cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::ClusterLightIndices));
            cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::ClusterStats));
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_LightClusterVisualize(RHI_CommandList* cmd_list)
    {
        uint32_t mode = cvar_cluster_visualize.GetValueAs<uint32_t>();
        if (mode == 0)
        {
            return;
        }

        RHI_Texture* tex_debug = GetRenderTarget(Renderer_RenderTarget::debug_output);
        if (!tex_debug)
        {
            return;
        }

        cmd_list->BeginTimeblock("light_cluster_visualize");
        {
            RHI_PipelineState pso;
            pso.name             = "light_cluster_visualize";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_cluster_visualize_c);
            cmd_list->SetPipelineState(pso);

            // depth feeds the per pixel cluster id, debug_output is the heatmap target
            cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            cmd_list->SetTexture(Renderer_BindingsUav::tex,           tex_debug);

            // the visualize shader reads the grid populated by light_cluster_assign in compute batch a
            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_light_grid,    GetBuffer(Renderer_Buffer::ClusterLightGrid));
            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_light_indices, GetBuffer(Renderer_Buffer::ClusterLightIndices));

            // f3.x: visualization mode, f3.y: saturation cap for the count ramp (lights per cluster that maps to full red)
            // the cap is a cvar so users can tune contrast per scene, defaults to 4 which matches typical street-lamp density
            const float cap = max(cvar_cluster_visualize_cap.GetValue(), 1.0f);
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(mode), cap, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // dispatch the full texture so the unrendered region (when resolution scale < 1) gets cleared to black
            // inside the shader, rather than left as stale gpu memory
            cmd_list->Dispatch(tex_debug, 1.0f);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* light_diffuse    = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* light_specular   = GetRenderTarget(Renderer_RenderTarget::light_specular);
        RHI_Texture* light_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric);

        RHI_PipelineState pso;
        pso.name             = is_transparent_pass ? "light_transparent" : "light";
        pso.shaders[Compute] = GetShader(Renderer_Shader::light_c);
    
        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);
    
            SetCommonTextures(cmd_list, eye_layer);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     GetRenderTarget(Renderer_RenderTarget::skysphere));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2,    GetRenderTarget(Renderer_RenderTarget::shadow_atlas));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4,    GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows));
            cmd_list->SetTexture(Renderer_BindingsUav::tex,     light_diffuse);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2,    light_specular);
            cmd_list->SetTexture(Renderer_BindingsUav::tex3,    light_volumetric);

            // clustered lighting grid, written by light_cluster_assign in compute batch a
            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_light_grid,       GetBuffer(Renderer_Buffer::ClusterLightGrid));
            cmd_list->SetBuffer(Renderer_BindingsUav::cluster_light_indices,    GetBuffer(Renderer_Buffer::ClusterLightIndices));
            cmd_list->SetBuffer(Renderer_BindingsUav::volumetric_light_indices, GetBuffer(Renderer_Buffer::VolumetricLightIndices));

            // bind tlas for inline ray traced shadows when ray tracing is supported and the world has geometry
            if (RHI_Device::IsSupportedRayTracing())
            {
                if (RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure())
                {
                    if (tlas->GetRhiResource())
                    {
                        cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
                    }
                }
            }
    
            // active light count now flows through buffer_frame.cluster_light_count, fog density still rides in f3.y
            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            m_pcb_pass_cpu.set_f3_value(0.0f, cvar_fog.GetValue());
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->Dispatch(light_diffuse, Renderer::GetResolutionScale());
            cmd_list->InsertBarrier(light_specular,   RHI_BarrierType::EnsureWriteThenRead);
            cmd_list->InsertBarrier(light_volumetric, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndTimeblock();
    }
    
    void Renderer::Pass_Light_Composition(RHI_CommandList* cmd_list, const bool is_transparent_pass, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Shader* shader_c              = GetShader(Renderer_Shader::light_composition_c);
        RHI_Texture* tex_out              = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_skysphere        = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_light_diffuse    = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* tex_light_specular   = GetRenderTarget(Renderer_RenderTarget::light_specular);
        RHI_Texture* tex_light_volumetric = GetRenderTarget(Renderer_RenderTarget::light_volumetric);
        RHI_Texture* tex_gi               = GetRenderTarget(Renderer_RenderTarget::restir_denoised);

        cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureReadThenWrite);

        cmd_list->BeginTimeblock(is_transparent_pass ? "light_composition_transparent" : "light_composition");
        {
            RHI_PipelineState pso;
            pso.name             = "light_composition";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            m_pcb_pass_cpu.set_f3_value(0.0f, cvar_fog.GetValue(), 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            SetCommonTextures(cmd_list, eye_layer);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_skysphere);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_light_diffuse);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4, tex_light_specular);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex5, tex_light_volumetric);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex6, tex_gi);
            cmd_list->Dispatch(tex_out, Renderer::GetResolutionScale());
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Light_Ibl(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Shader* shader   = GetShader(Renderer_Shader::light_image_based_c);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::frame_render);

        cmd_list->BeginTimeblock("light_image_based");
        {
            RHI_PipelineState pso;
            pso.name             = "light_image_based";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list, eye_layer);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,     tex_out);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_sss, GetRenderTarget(Renderer_RenderTarget::sss));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2,    GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3,    GetRenderTarget(Renderer_RenderTarget::skysphere));

            m_pcb_pass_cpu.set_f3_value(static_cast<float>(GetRenderTarget(Renderer_RenderTarget::skysphere)->GetMipCount()));
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(tex_out, Renderer::GetResolutionScale());
        }
        cmd_list->EndTimeblock();
    }
}
