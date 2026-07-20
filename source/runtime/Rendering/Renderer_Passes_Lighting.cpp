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
#include "../RHI/RHI_VendorTechnology.h"
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
    void Renderer::Pass_Reflections_Apply(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* tex_frame             = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_reflections       = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_refraction_source = GetRenderTarget(Renderer_RenderTarget::frame_render_opaque);

        cmd_list->BeginTimeblock("reflections_apply");
        {
            bool use_ray_traced = cvar_ray_traced_reflections.GetValueAs<bool>();

            if (!m_pass_state.cleared_reflections && !use_ray_traced)
            {
                cmd_list->ClearTexture(tex_reflections, Color::standard_transparent);
                m_pass_state.cleared_reflections = true;
            }

            cmd_list->BeginMarker("apply");
            {
                RHI_PipelineState pso;
                pso.name             = "reflections_apply";
                pso.shaders[Compute] = GetShader(Renderer_Shader::reflections_apply_c);

                cmd_list->SetPipelineState(pso);
                SetCommonTextures(cmd_list, eye_layer);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_reflections);        // in - reflection
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

    void Renderer::Pass_Reflections_Trace(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
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

        cmd_list->BeginTimeblock("reflections_trace");
        {
            RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
            if (!tlas || !tlas->GetRhiResource())
            {
                cmd_list->ClearTexture(tex_reflections, Color(1.0f, 1.0f, 0.0f, 1.0f));
                cmd_list->EndTimeblock();
                return;
            }

            RHI_PipelineState pso;
            pso.name                   = "reflections_trace";
            pso.shaders[RayGeneration] = GetShader(Renderer_Shader::reflections_ray_generation_r);
            pso.shaders[RayMiss]       = GetShader(Renderer_Shader::reflections_ray_miss_r);
            pso.shaders[RayHit]        = GetShader(Renderer_Shader::reflections_ray_hit_r);
            cmd_list->SetPipelineState(pso);

            // phase 2 overlaps async compute ssao writes, skip ssao
            SetCommonTextures(cmd_list, eye_layer, false);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            
            RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
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
        }
        cmd_list->EndTimeblock();
    }
    
    void Renderer::Pass_Reflections_Shade(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
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

        cmd_list->BeginTimeblock("reflections_shade");
        {
            RHI_PipelineState pso;
            pso.name             = "reflections_shade";
            pso.shaders[Compute] = GetShader(Renderer_Shader::reflections_shade_c);
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
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Reflections_Denoise(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        if (Window::IsMinimized() || !cvar_ray_traced_reflections.GetValueAs<bool>())
        {
            return;
        }

        RHI_Texture* tex_reflections = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_mv          = GetRenderTarget(Renderer_RenderTarget::nrd_screen_mv);
        RHI_Texture* tex_normal      = GetRenderTarget(Renderer_RenderTarget::nrd_screen_normal_roughness);
        RHI_Texture* tex_view_z      = GetRenderTarget(Renderer_RenderTarget::nrd_screen_viewz);
        RHI_Texture* tex_in          = GetRenderTarget(Renderer_RenderTarget::nrd_in_spec_radiance);
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::nrd_out_spec_radiance);
        if (!tex_reflections || !tex_mv || !tex_normal || !tex_view_z || !tex_in || !tex_out)
        {
            return;
        }

        const uint32_t min_rt_dimension = 64;
        if (tex_reflections->GetWidth() < min_rt_dimension || tex_reflections->GetHeight() < min_rt_dimension)
        {
            return;
        }

        RHI_Shader* shader_pack   = GetShader(Renderer_Shader::nrd_pack_reflections_c);
        RHI_Shader* shader_unpack = GetShader(Renderer_Shader::nrd_unpack_reflections_c);
        if (!shader_pack || !shader_unpack || !shader_pack->IsCompiled() || !shader_unpack->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("reflections_denoise");
        {
            cmd_list->BeginMarker("nrd_pack");
            {
                RHI_PipelineState pso;
                pso.name             = "nrd_pack_reflections";
                pso.shaders[Compute] = shader_pack;
                cmd_list->SetPipelineState(pso);

                SetCommonTextures(cmd_list, eye_layer);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex,   tex_reflections);
                cmd_list->SetTexture(Renderer_BindingsUav::tex,   tex_mv);
                cmd_list->SetTexture(Renderer_BindingsUav::tex2,  tex_normal);
                cmd_list->SetTexture(Renderer_BindingsUav::tex3,  tex_view_z);
                cmd_list->SetTexture(Renderer_BindingsUav::tex4,  tex_in);
                cmd_list->Dispatch(tex_reflections);
            }
            cmd_list->EndMarker();

            cmd_list->BeginMarker("nrd_dispatch");
            {
                if (!RHI_VendorTechnology::NRD_Dispatch(cmd_list, Nrd_Preset::Reflections, tex_mv, tex_normal, tex_view_z, tex_in, tex_out))
                {
                    cmd_list->EndMarker();
                    cmd_list->EndTimeblock();
                    return;
                }
            }
            cmd_list->EndMarker();

            cmd_list->BeginMarker("nrd_unpack");
            {
                RHI_PipelineState pso;
                pso.name             = "nrd_unpack_reflections";
                pso.shaders[Compute] = shader_unpack;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_out);
                cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_reflections);
                cmd_list->Dispatch(tex_reflections);
            }
            cmd_list->EndMarker();
        }
        cmd_list->EndTimeblock();
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

        // the trace and its spatiotemporal denoiser are wrapped in one timeblock so they show up
        // as a single chunk in the profiler, the sub stages below are plain gpu markers
        cmd_list->BeginTimeblock("ray_traced_shadows");
        {
            cmd_list->BeginMarker("trace");
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

                // x tells the raygen whether transparents exist, opaque scenes take a single accept first hit ray
                m_pcb_pass_cpu.set_f3_value(m_transparents_present ? 1.0f : 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                uint32_t width  = tex_shadows->GetWidth();
                uint32_t height = tex_shadows->GetHeight();
                cmd_list->TraceRays(width, height);
            }
            cmd_list->EndMarker();

            Pass_Denoise_RayTracedShadows(cmd_list);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Denoise_RayTracedShadows(RHI_CommandList* cmd_list)
    {
        if (Window::IsMinimized())
        {
            return;
        }

        bool restir_pt_owns_shadows = cvar_restir_pt.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing();
        if (!cvar_ray_traced_shadows.GetValueAs<bool>() || restir_pt_owns_shadows || !RHI_Device::IsSupportedRayTracing())
        {
            return;
        }

        if (!GetTopLevelAccelerationStructure())
        {
            return;
        }

        RHI_Texture* tex_shadows = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows);
        RHI_Texture* tex_mv      = GetRenderTarget(Renderer_RenderTarget::nrd_screen_mv);
        RHI_Texture* tex_normal  = GetRenderTarget(Renderer_RenderTarget::nrd_screen_normal_roughness);
        RHI_Texture* tex_view_z  = GetRenderTarget(Renderer_RenderTarget::nrd_screen_viewz);
        RHI_Texture* tex_in      = GetRenderTarget(Renderer_RenderTarget::nrd_in_penumbra);
        RHI_Texture* tex_out     = GetRenderTarget(Renderer_RenderTarget::nrd_out_shadow);
        if (!tex_shadows || !tex_mv || !tex_normal || !tex_view_z || !tex_in || !tex_out)
        {
            return;
        }

        const uint32_t min_rt_dimension = 64;
        if (tex_shadows->GetWidth() < min_rt_dimension || tex_shadows->GetHeight() < min_rt_dimension)
        {
            return;
        }

        RHI_Shader* shader_pack   = GetShader(Renderer_Shader::nrd_pack_shadows_c);
        RHI_Shader* shader_unpack = GetShader(Renderer_Shader::nrd_unpack_shadows_c);
        if (!shader_pack || !shader_unpack || !shader_pack->IsCompiled() || !shader_unpack->IsCompiled())
        {
            return;
        }

        // directional light for sigma light direction and angular size
        Vector3 light_direction = Vector3::Down;
        float tan_light_angular_radius = 0.00465f;
        for (Entity* entity : World::GetEntities())
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType::Directional && light->GetFlag(LightFlags::Shadows) && light->GetIntensityRadiometric() > 0.0f)
                {
                    light_direction = -light->GetEntity()->GetForward();
                    tan_light_angular_radius = tanf(max(light->GetAngle() * 0.5f, 0.0001f));
                    break;
                }
            }
        }

        cmd_list->BeginMarker("nrd_pack");
        {
            RHI_PipelineState pso;
            pso.name             = "nrd_pack_shadows";
            pso.shaders[Compute] = shader_pack;
            cmd_list->SetPipelineState(pso);

            m_pcb_pass_cpu.set_f3_value(tan_light_angular_radius);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_shadows);
            cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_mv);
            cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_normal);
            cmd_list->SetTexture(Renderer_BindingsUav::tex3, tex_view_z);
            cmd_list->SetTexture(Renderer_BindingsUav::tex4, tex_in);
            cmd_list->Dispatch(tex_shadows);
        }
        cmd_list->EndMarker();

        cmd_list->BeginMarker("nrd_dispatch");
        {
            if (!RHI_VendorTechnology::NRD_Dispatch(cmd_list, Nrd_Preset::Shadows, tex_mv, tex_normal, tex_view_z, tex_in, tex_out, &light_direction))
            {
                cmd_list->EndMarker();
                return;
            }
        }
        cmd_list->EndMarker();

        cmd_list->BeginMarker("nrd_unpack");
        {
            RHI_PipelineState pso;
            pso.name             = "nrd_unpack_shadows";
            pso.shaders[Compute] = shader_unpack;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_shadows);
            cmd_list->Dispatch(tex_shadows);
        }
        cmd_list->EndMarker();
    }

    // build one self inverting pairing table, lin 2026 3.1, each link index starts as two
    // horizontally adjacent copies, repeated random 2x2 shuffles move each copy by a gaussian
    // of sigma / sqrt(2), pairing the copies yields deltas with standard deviation sigma,
    // deltas wrap at half the table size so the table tiles across the screen
    static void build_restir_pairing_table(uint32_t size, float sigma, uint32_t seed, uint32_t* out)
    {
        const uint32_t n = size * size;
        vector<uint32_t> link(n);
        for (uint32_t i = 0; i < n; i++)
        {
            link[i] = i / 2;
        }

        // shuffle count from the paper's fit, equation 3
        float inv_sigma        = 1.0f / sigma;
        uint32_t shuffle_count = max(static_cast<uint32_t>(sigma * sigma * 0.5f + 1.46f * inv_sigma + 1.76f * inv_sigma * inv_sigma + 0.656f * inv_sigma * inv_sigma * inv_sigma + 0.5f), 1u);

        uint32_t rng = seed;
        auto next_random = [&rng]()
        {
            rng = rng * 747796405u + 2891336453u;
            uint32_t word = ((rng >> ((rng >> 28u) + 4u)) ^ rng) * 277803737u;
            return (word >> 22u) ^ word;
        };

        for (uint32_t shuffle = 0; shuffle < shuffle_count; shuffle++)
        {
            uint32_t offset = shuffle & 1u;
            for (uint32_t by = 0; by < size / 2; by++)
            {
                for (uint32_t bx = 0; bx < size / 2; bx++)
                {
                    uint32_t x0     = (bx * 2 + offset) % size;
                    uint32_t y0     = (by * 2 + offset) % size;
                    uint32_t x1     = (x0 + 1) % size;
                    uint32_t y1     = (y0 + 1) % size;
                    uint32_t idx[4] = { y0 * size + x0, y0 * size + x1, y1 * size + x0, y1 * size + x1 };
                    for (uint32_t k = 3; k > 0; k--)
                    {
                        uint32_t j = next_random() % (k + 1);
                        swap(link[idx[k]], link[idx[j]]);
                    }
                }
            }
        }

        // pair the two pixels sharing each link index, wrapped deltas pack into 8 bits per axis
        vector<int32_t> first(n / 2, -1);
        const int32_t side = static_cast<int32_t>(size);
        const int32_t half = side / 2;
        for (uint32_t i = 0; i < n; i++)
        {
            uint32_t l = link[i];
            if (first[l] < 0)
            {
                first[l] = static_cast<int32_t>(i);
                continue;
            }
            int32_t ax = first[l] % side;
            int32_t ay = first[l] / side;
            int32_t bx = static_cast<int32_t>(i) % side;
            int32_t by = static_cast<int32_t>(i) / side;
            int32_t dx = bx - ax;
            int32_t dy = by - ay;
            if (dx > half)
            {
                dx -= side;
            }
            if (dx < -half)
            {
                dx += side;
            }
            if (dy > half)
            {
                dy -= side;
            }
            if (dy < -half)
            {
                dy += side;
            }
            out[ay * side + ax] = (static_cast<uint32_t>( dx + 128) << 8) | static_cast<uint32_t>( dy + 128);
            out[by * side + bx] = (static_cast<uint32_t>(-dx + 128) << 8) | static_cast<uint32_t>(-dy + 128);
        }
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

            if (RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
            }

            // duplication score of the previous frame's reservoirs, drives the adaptive m cap, lin 2026 5
            if (RHI_Texture* tex_duplication = GetRenderTarget(Renderer_RenderTarget::restir_duplication))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_duplication);
            }

            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            // pipeline layout always declares the push constant range, vulkan validation requires
            // a vkCmdPushConstants call before every dispatch even when the shader does not read
            // any of the per pass values, this dispatches with whatever the renderer tagged
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
        }
        cmd_list->EndTimeblock();
    }

    bool Renderer::Pass_ReSTIR_SpatialPair(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_spatial, uint32_t dispatch_x, uint32_t dispatch_y)
    {
        RHI_Shader* shader_shift   = GetShader(Renderer_Shader::restir_pt_spatial_shift_c);
        RHI_Shader* shader_spatial = GetShader(Renderer_Shader::restir_pt_spatial_c);
        if (!shader_shift || !shader_spatial || !shader_shift->IsCompiled() || !shader_spatial->IsCompiled())
        {
            return false;
        }

        RHI_Texture* shift[3];
        for (uint32_t i = 0; i < 3; i++)
        {
            shift[i] = GetRenderTarget(static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_shift0) + i));
        }
        if (!shift[0] || !shift[1] || !shift[2])
        {
            return false;
        }

        // paired spatial reuse, lin 2026 3, the pairing tables couple every pixel with one
        // mutual partner per table, the pre-pass shifts each pixel's path to its partners once
        // and the resample pass reads both directions of every pair from the shift textures,
        // halving the shift mapping and visibility cost versus per-neighbor bidirectional shifts
        cmd_list->BeginTimeblock("restir_pt_spatial_shift");
        {
            RHI_PipelineState pso;
            pso.name             = "restir_pt_spatial_shift";
            pso.shaders[Compute] = shader_shift;
            cmd_list->SetPipelineState(pso);

            cmd_list->PushConstants(m_pcb_pass_cpu);

            SetCommonTextures(cmd_list);
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);

            // bind the per renderable geometry info ring and the emissive triangle nee pool so
            // the random replay shift can do its full suffix retrace inline, see the matching
            // comment in Pass_ReSTIR_Temporal for details
            cmd_list->SetBuffer(Renderer_BindingsUav::geometry_info,      GetBuffer(Renderer_Buffer::GeometryInfo));
            cmd_list->SetBuffer(Renderer_BindingsUav::emissive_triangles, GetBuffer(Renderer_Buffer::EmissiveTriangles));
            cmd_list->SetBuffer(Renderer_BindingsUav::restir_pairing,     GetBuffer(Renderer_Buffer::RestirPairing));

            if (RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere))
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_skysphere);
            }

            for (uint32_t i = 0; i < 6; i++)
            {
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs[i]);
            }

            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex),  shift[0], rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), shift[1], rhi_all_mips, 0, true);
            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), shift[2], rhi_all_mips, 0, true);
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
        }
        cmd_list->EndTimeblock();

        cmd_list->BeginTimeblock("restir_pt_spatial");
        {
            RHI_PipelineState pso;
            pso.name             = "restir_pt_spatial";
            pso.shaders[Compute] = shader_spatial;
            cmd_list->SetPipelineState(pso);

            cmd_list->PushConstants(m_pcb_pass_cpu);

            SetCommonTextures(cmd_list);

            // tlas for the periodic sample validation ray, the pairing buffer resolves partners
            cmd_list->SetAccelerationStructure(Renderer_BindingsSrv::tlas, tlas);
            cmd_list->SetBuffer(Renderer_BindingsUav::restir_pairing, GetBuffer(Renderer_Buffer::RestirPairing));

            // pre-pass shift results, one per pairing table
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,  shift[0]);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, shift[1]);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex4, shift[2]);

            for (uint32_t i = 0; i < 6; i++)
            {
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs[i]);
                cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0)      + i, reservoirs_spatial[i], rhi_all_mips, 0, true);
            }

            cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
        }
        cmd_list->EndTimeblock();

        // the resample leaves its output in reservoirs_spatial, swap the canonical and spatial
        // render target pointers so subsequent passes reading restir_reservoir0..5 see the
        // freshly resampled reservoirs, the pointer swap is free compared to blitting 6 textures
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

            // zero duplication keeps the temporal m cap at its default until real history exists
            if (RHI_Texture* tex_duplication = GetRenderTarget(Renderer_RenderTarget::restir_duplication))
            {
                cmd_list->ClearTexture(tex_duplication, Color::standard_black);
            }

            // paired spatial reuse tables, lin 2026 3, sigma is the paper's 16 px at full
            // resolution scaled by the restir resolution factor, the init flag resets on
            // scale changes so the tables regenerate together with the reservoirs
            if (RHI_Buffer* pairing_buffer = GetBuffer(Renderer_Buffer::RestirPairing))
            {
                float sigma = min(max(16.0f * cvar_restir_pt_scale.GetValue(), 2.0f), 16.0f);
                vector<uint32_t> pairing(restir_pairing_element_count);
                uint32_t base = 0;
                for (uint32_t t = 0; t < 3; t++)
                {
                    build_restir_pairing_table(restir_pairing_sizes[t], sigma, 0x9E3779B9u * (t + 1), pairing.data() + base);
                    base += restir_pairing_sizes[t] * restir_pairing_sizes[t];
                }
                pairing_buffer->ResetOffset();
                pairing_buffer->Update(cmd_list, pairing.data(), static_cast<uint32_t>(pairing.size() * sizeof(uint32_t)));
            }

            m_pass_state.restir_reservoirs_initialized = true;
        }

        Pass_ReSTIR_TraceInitial(cmd_list, tlas, tex_gi, tex_skysphere, reservoirs, width, height);
        Pass_ReSTIR_Temporal(cmd_list, tlas, tex_gi, reservoirs, reservoirs_prev, dispatch_x, dispatch_y);
        const bool ran_spatial = Pass_ReSTIR_SpatialPair(cmd_list, tlas, tex_gi, reservoirs, reservoirs_spatial, dispatch_x, dispatch_y);

        // sample duplication map, lin 2026 5, counts shifted copies of the same initial candidate
        // around each pixel in the final reservoirs, next frame's temporal pass reads it at the
        // backprojected pixel to adaptively reduce the confidence cap in correlated regions
        if (RHI_Texture* tex_duplication = GetRenderTarget(Renderer_RenderTarget::restir_duplication))
        {
            RHI_Shader* shader_duplication = GetShader(Renderer_Shader::restir_pt_duplication_c);
            if (shader_duplication && shader_duplication->IsCompiled())
            {
                cmd_list->BeginTimeblock("restir_pt_duplication");
                {
                    RHI_PipelineState pso;
                    pso.name             = "restir_pt_duplication";
                    pso.shaders[Compute] = shader_duplication;
                    cmd_list->SetPipelineState(pso);

                    // reservoir texture 2 carries the replay seed in its x channel, the spatial
                    // pass left the final reservoirs in the spatial slot when it ran
                    RHI_Texture* reservoir_seed = ran_spatial ? reservoirs_spatial[2] : reservoirs[2];
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex, reservoir_seed);
                    cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_duplication, rhi_all_mips, 0, true);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->Dispatch(dispatch_x, dispatch_y, 1);
                }
                cmd_list->EndTimeblock();
            }
        }

        Pass_ReSTIR_SwapReservoirs();
    }

    void Renderer::Pass_ReSTIR_Denoising(RHI_CommandList* cmd_list)
    {
        if (Window::IsMinimized())
        {
            return;
        }

        RHI_Texture* tex_gi_raw      = GetRenderTarget(Renderer_RenderTarget::restir_output);
        RHI_Texture* tex_gi_denoised = GetRenderTarget(Renderer_RenderTarget::restir_denoised);
        RHI_Texture* tex_mv          = GetRenderTarget(Renderer_RenderTarget::nrd_in_mv);
        RHI_Texture* tex_normal      = GetRenderTarget(Renderer_RenderTarget::nrd_in_normal_roughness);
        RHI_Texture* tex_view_z      = GetRenderTarget(Renderer_RenderTarget::nrd_in_viewz);
        RHI_Texture* tex_in          = GetRenderTarget(Renderer_RenderTarget::nrd_in_diff_radiance);
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::nrd_out_diff_radiance);
        if (!tex_gi_raw || !tex_gi_denoised || !tex_mv || !tex_normal || !tex_view_z || !tex_in || !tex_out)
        {
            return;
        }

        const uint32_t min_rt_dimension = 64;
        if (tex_gi_raw->GetWidth() < min_rt_dimension || tex_gi_raw->GetHeight() < min_rt_dimension)
        {
            return;
        }

        RHI_Shader* shader_pack   = GetShader(Renderer_Shader::restir_pt_nrd_pack_c);
        RHI_Shader* shader_unpack = GetShader(Renderer_Shader::restir_pt_nrd_unpack_c);
        if (!shader_pack || !shader_unpack || !shader_pack->IsCompiled() || !shader_unpack->IsCompiled())
        {
            Pass_BlitRestirFallback(cmd_list, tex_gi_raw, tex_gi_denoised);
            return;
        }

        cmd_list->BeginTimeblock("restir_pt_denoise");
        {
            cmd_list->BeginMarker("nrd_pack");
            {
                RHI_PipelineState pso;
                pso.name             = "restir_pt_nrd_pack";
                pso.shaders[Compute] = shader_pack;
                cmd_list->SetPipelineState(pso);

                SetCommonTextures(cmd_list);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_gi_raw);
                cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_mv);
                cmd_list->SetTexture(Renderer_BindingsUav::tex2, tex_normal);
                cmd_list->SetTexture(Renderer_BindingsUav::tex3, tex_view_z);
                cmd_list->SetTexture(Renderer_BindingsUav::tex4, tex_in);
                cmd_list->Dispatch(tex_gi_raw);
            }
            cmd_list->EndMarker();

            cmd_list->BeginMarker("nrd_dispatch");
            {
                if (!RHI_VendorTechnology::NRD_Dispatch(cmd_list, Nrd_Preset::Gi, tex_mv, tex_normal, tex_view_z, tex_in, tex_out))
                {
                    Pass_BlitRestirFallback(cmd_list, tex_gi_raw, tex_gi_denoised);
                    cmd_list->EndMarker();
                    cmd_list->EndTimeblock();
                    return;
                }
            }
            cmd_list->EndMarker();

            cmd_list->BeginMarker("nrd_unpack");
            {
                RHI_PipelineState pso;
                pso.name             = "restir_pt_nrd_unpack";
                pso.shaders[Compute] = shader_unpack;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_out);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_gi_denoised);
                cmd_list->Dispatch(tex_gi_denoised);
            }
            cmd_list->EndMarker();
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ScreenSpaceShadows(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_sss = GetRenderTarget(Renderer_RenderTarget::sss);

        // screen space contact shadows complement the rasterized shadow map, light.hlsl combines
        // them with a min() on the primary term, only directional lights march here so the pass
        // is dead work when either restir owns the full direct term or the ray traced shadow owns
        // the sun, the rt trace already captures exact contact occlusion and light.hlsl skips the
        // contact term in that case, the gate below must mirror is_ray_traced_shadows_enabled()
        const bool rt_shadows_active = cvar_ray_traced_shadows.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr;
        if (cvar_restir_pt.GetValueAs<bool>() || rt_shadows_active)
        {
            return;
        }

        cmd_list->BeginTimeblock("screen_space_shadows");
        {
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
                cmd_list->UpdateBuffer(GetBuffer(Renderer_Buffer::ClusterStats), 0, sizeof(uint32_t), &zero, false);
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

    void Renderer::Pass_LightFlares(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        if (!cvar_light_flares.GetValueAs<bool>() || m_count_active_lights <= 1)
        {
            return;
        }

        RHI_Shader* shader_v = GetShader(Renderer_Shader::light_flare_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::light_flare_p);
        if (!shader_v || !shader_p || !shader_v->IsCompiled() || !shader_p->IsCompiled())
        {
            return;
        }

        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::frame_render);
        if (!tex_out)
        {
            return;
        }

        cmd_list->BeginTimeblock("light_flares");
        {
            RHI_PipelineState pso;
            pso.name                             = "light_flares";
            pso.shaders[RHI_Shader_Type::Vertex] = shader_v;
            pso.shaders[RHI_Shader_Type::Pixel]  = shader_p;
            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Additive);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::Off);
            pso.render_target_color_textures[0]  = tex_out;
            pso.clear_color[0]                   = rhi_color_load;
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list, eye_layer);

            const float near_distance   = clamp(cvar_light_flares_near_distance.GetValue(), 0.0f, 500.0f);
            const float fade_length     = clamp(cvar_light_flares_fade_length.GetValue(), 0.1f, 500.0f);
            const float size_scale      = clamp(cvar_light_flares_size_scale.GetValue(), 0.01f, 5.0f);
            const float intensity_scale = clamp(cvar_light_flares_intensity_scale.GetValue(), 0.01f, 5.0f);
            const float max_size_px     = clamp(cvar_light_flares_max_size_px.GetValue(), 1.0f, 16.0f);
            const float occlusion       = cvar_light_flares_occlusion.GetValueAs<bool>() ? 1.0f : 0.0f;

            const float disc_size_px  = min(max_size_px * 2.0f + 2.0f, 128.0f);

            for (uint32_t i = 1; i < m_count_active_lights; i++)
            {
                m_pcb_pass_cpu.set_f3_value(near_distance, size_scale, intensity_scale);
                m_pcb_pass_cpu.set_f3_value2(max_size_px, occlusion, static_cast<float>(i));
                m_pcb_pass_cpu.set_f2_value(disc_size_px, fade_length);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Draw(6);
            }
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
            // sun-projected cloud transmittance, sampled by the volumetric fog march
            cmd_list->SetTexture(Renderer_BindingsSrv::tex5,    GetRenderTarget(Renderer_RenderTarget::cloud_shadow));
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
