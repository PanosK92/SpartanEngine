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
#include "../World/World.h"
#include "../World/Components/Light.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Shader.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::Pass_Skysphere(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_skysphere                    = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);
        RHI_Texture* tex_lut_sky_view                 = GetRenderTarget(Renderer_RenderTarget::lut_sky_view);
        RHI_Texture* tex_cloud_noise                  = GetRenderTarget(Renderer_RenderTarget::cloud_noise);
        RHI_Texture* tex_cloud_shadow                 = GetRenderTarget(Renderer_RenderTarget::cloud_shadow);

        cmd_list->BeginTimeblock("skysphere");
        {
            if (World::GetDirectionalLight())
            {
                // sky view lut, one small march per texel replaces the per panorama pixel
                // atmosphere integration, rebaked every frame since it tracks the sun, the
                // moon and the camera altitude, cost is negligible next to the cloud march
                {
                    RHI_PipelineState pso;
                    pso.name             = "skysphere_sky_view_lut";
                    pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_sky_view_lut_c);
                    cmd_list->SetPipelineState(pso);

                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_sky_view);
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_lut_atmosphere_multiscatter);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->Dispatch(tex_lut_sky_view);
                }

                // cloud shadow map, cumulus transmittance along the sun projected on the cloud
                // base plane, rebaked every frame since it tracks the camera and the sun, the
                // volumetric fog march samples it to carve the sun shafts through cloud gaps
                {
                    RHI_PipelineState pso;
                    pso.name             = "cloud_shadow";
                    pso.shaders[Compute] = GetShader(Renderer_Shader::clouds_shadow_c);
                    cmd_list->SetPipelineState(pso);

                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_cloud_shadow);
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, tex_cloud_noise);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->Dispatch(tex_cloud_shadow);

                    // soft penumbra plus anti-moire, the raw bake carries texel-rate edges that
                    // beat against the screen grid on the ground, a few texels of blur and a mip
                    // chain remove that high frequency before lighting samples the map
                    Pass_Blur(cmd_list, tex_cloud_shadow, false, 4.0f, 0);
                    Pass_Downscale(cmd_list, tex_cloud_shadow, Renderer_DownsampleFilter::Average);
                }

                RHI_PipelineState pso;
                pso.name             = "skysphere_atmospheric_scattering";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_skysphere);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_lut_atmosphere_multiscatter);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex3, tex_lut_sky_view);  // catalog stars + procedural milky way, drawn into the panorama like before
                RHI_Texture* tex_stars = GetStandardTexture(Renderer_StandardTexture::Sky_stars);
                RHI_Texture* tex_grid  = GetStandardTexture(Renderer_StandardTexture::Sky_star_grid);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex5, tex_stars ? tex_stars : GetStandardTexture(Renderer_StandardTexture::Black));
                cmd_list->SetTexture(Renderer_BindingsSrv::tex6, tex_grid  ? tex_grid  : GetStandardTexture(Renderer_StandardTexture::Black));

                // values[0].x carries the warmup blend during the burst (1, 1/2, 1/3 ... so the
                // full bakes average progressively with no ghost of the previous sky), 0.0 in
                // steady state which selects the partial dispatch animation mode in the shader
                m_pcb_pass_cpu.set_f3_value(m_pass_state.sky_warmup_this_frame ? m_pass_state.sky_warmup_blend : 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                if (m_pass_state.sky_warmup_this_frame)
                {
                    cmd_list->Dispatch(tex_skysphere);
                }
                else
                {
                    // steady state coarse dispatch, one sixteenth of the threads
                    //   shader picks a 4x4 tile offset that cycles with the frame counter
                    //   each thread writes exactly one full resolution pixel and every pixel
                    //   refreshes every 16 frames (~267 ms at 60 fps). the previous early
                    //   return scheme launched every wave and paid full cloud march time
                    //   per wave because of gpu lockstep execution, this version actually
                    //   removes the dead waves for a real 16x reduction in bake cost
                    const uint32_t thread_group_size = 8;
                    const uint32_t quarter_w         = (tex_skysphere->GetWidth()  + 3) / 4;
                    const uint32_t quarter_h         = (tex_skysphere->GetHeight() + 3) / 4;
                    const uint32_t dispatch_x        = (quarter_w + thread_group_size - 1) / thread_group_size;
                    const uint32_t dispatch_y        = (quarter_h + thread_group_size - 1) / thread_group_size;
                    cmd_list->Dispatch(dispatch_x, dispatch_y);
                }
            }
            else
            {
                cmd_list->ClearTexture(tex_skysphere, Color::standard_black);
            }

            // mip pyramid rebuild
            //   pass_downscale is a single pass averaging downsampler, only runs every fourth
            //   frame in steady state so the steady state cost stays well under a millisecond
            //   the mip chain ends up at most ~67 ms stale relative to mip 0, which is below
            //   any visible ibl response time for slow-evolving clouds
            //   the ggx prefilter loop below is the expensive part (~512 importance samples
            //   per pixel on mip 1) and is restricted to the warmup burst so steady state
            //   animation pays only the occasional downscale cost. clouds are soft and mostly
            //   drive diffuse-style ibl, so the averaged mips are a good enough approximation
            //   of the proper ggx-filtered specular mips between sun direction changes
            {
                const bool do_downscale = m_pass_state.sky_warmup_this_frame ||
                                          ((m_cb_frame_cpu.frame & 3u) == 0u);
                if (do_downscale)
                {
                    Pass_Downscale(cmd_list, tex_skysphere, Renderer_DownsampleFilter::Average);
                }

                if (m_pass_state.sky_warmup_this_frame)
                {
                    RHI_PipelineState pso;
                    pso.name             = "skysphere_filter";
                    pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_environment_filter_c);
                    cmd_list->SetPipelineState(pso);

                    const uint32_t mip_count = tex_skysphere->GetMipCount();
                    const uint32_t base_w    = tex_skysphere->GetWidth();
                    const uint32_t base_h    = tex_skysphere->GetHeight();
                    for (uint32_t mip_level = 1; mip_level < mip_count; mip_level++)
                    {
                        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_skysphere, 0, mip_level);
                        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_skysphere, mip_level, 1);

                        m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_level), static_cast<float>(mip_count), 0.0f);
                        cmd_list->PushConstants(m_pcb_pass_cpu);

                        // dispatch sized to the mip resolution, not the base panorama
                        // the legacy code dispatched the full 4096x2048 grid for every mip and
                        // relied on per-thread bounds checks, wasting thread launches that this
                        // mip-sized dispatch avoids. matters most when the filter is on the hot
                        // path during the warmup burst
                        const uint32_t mip_w     = max(1u, base_w >> mip_level);
                        const uint32_t mip_h     = max(1u, base_h >> mip_level);
                        const uint32_t dispatch_x = (mip_w + 7) / 8;
                        const uint32_t dispatch_y = (mip_h + 7) / 8;
                        cmd_list->Dispatch(dispatch_x, dispatch_y);
                    }
                }
            }
        }
        cmd_list->EndTimeblock();

        Pass_Clouds_Environment(cmd_list);
    }

    void Renderer::Pass_Clouds_Render(RHI_CommandList* cmd_list, uint32_t eye_layer)
    {
        RHI_Texture* tex_raw      = GetRenderTarget(Renderer_RenderTarget::cloud_raw);
        RHI_Texture* tex_distance = GetRenderTarget(Renderer_RenderTarget::cloud_raw_distance);

        RHI_PipelineState pso;
        pso.name             = "clouds_render";
        pso.shaders[Compute] = GetShader(Renderer_Shader::clouds_render_c);
        cmd_list->SetPipelineState(pso);

        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance));
        cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, GetRenderTarget(Renderer_RenderTarget::cloud_noise));
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_raw, rhi_all_mips, 0, true, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_distance, rhi_all_mips, 0, true, eye_layer);
        cmd_list->PushConstants(m_pcb_pass_cpu);
        const uint32_t dispatch_width = (tex_raw->GetWidth() + 1) / 2;
        const uint32_t dispatch_height = (tex_raw->GetHeight() + 1) / 2;
        cmd_list->Dispatch(
            (dispatch_width + 7) / 8,
            (dispatch_height + 7) / 8
        );
    }

    void Renderer::Pass_Clouds_Temporal(RHI_CommandList* cmd_list, uint32_t eye_layer)
    {
        const Renderer_RenderTarget resolved_targets[] = { Renderer_RenderTarget::cloud_resolved_0, Renderer_RenderTarget::cloud_resolved_1 };
        const Renderer_RenderTarget distance_targets[] = { Renderer_RenderTarget::cloud_resolved_distance_0, Renderer_RenderTarget::cloud_resolved_distance_1 };
        const uint32_t history_index = m_pass_state.cloud_history_index;
        const uint32_t output_index  = 1u - history_index;
        RHI_Texture* tex_output          = GetRenderTarget(resolved_targets[output_index]);
        RHI_Texture* tex_output_distance = GetRenderTarget(distance_targets[output_index]);

        RHI_PipelineState pso;
        pso.name             = "clouds_temporal";
        pso.shaders[Compute] = GetShader(Renderer_Shader::clouds_temporal_c);
        cmd_list->SetPipelineState(pso);

        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, GetRenderTarget(Renderer_RenderTarget::cloud_raw), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::cloud_raw_distance), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex3, GetRenderTarget(resolved_targets[history_index]), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex4, GetRenderTarget(distance_targets[history_index]), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex5, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_output, rhi_all_mips, 0, true, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_output_distance, rhi_all_mips, 0, true, eye_layer);
        m_pcb_pass_cpu.set_f3_value(m_pass_state.cloud_history_valid ? 0.0f : 1.0f);
        cmd_list->PushConstants(m_pcb_pass_cpu);
        cmd_list->Dispatch(tex_output);
    }

    void Renderer::Pass_Clouds_Composite(RHI_CommandList* cmd_list, uint32_t eye_layer, RHI_Texture* tex_scene)
    {
        const Renderer_RenderTarget resolved_targets[] = { Renderer_RenderTarget::cloud_resolved_0, Renderer_RenderTarget::cloud_resolved_1 };
        const Renderer_RenderTarget distance_targets[] = { Renderer_RenderTarget::cloud_resolved_distance_0, Renderer_RenderTarget::cloud_resolved_distance_1 };
        const uint32_t output_index = 1u - m_pass_state.cloud_history_index;
        RHI_Texture* tex_composite  = GetRenderTarget(Renderer_RenderTarget::cloud_composite);

        RHI_PipelineState pso;
        pso.name             = "clouds_composite";
        pso.shaders[Compute] = GetShader(Renderer_Shader::clouds_composite_c);
        cmd_list->SetPipelineState(pso);

        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_scene);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(resolved_targets[output_index]), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex3, GetRenderTarget(distance_targets[output_index]), rhi_all_mips, 0, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_composite);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), GetRenderTarget(Renderer_RenderTarget::cloud_velocity), rhi_all_mips, 0, true, eye_layer);
        cmd_list->PushConstants(m_pcb_pass_cpu);
        cmd_list->Dispatch(tex_composite);
        cmd_list->Blit(tex_composite, tex_scene, false);
    }

    void Renderer::Pass_Clouds_Environment(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_environment = GetRenderTarget(Renderer_RenderTarget::cloud_environment);
        if (!tex_environment)
        {
            return;
        }

        if (!World::GetDirectionalLight())
        {
            if (m_pass_state.cloud_environment_dirty)
            {
                cmd_list->ClearTexture(tex_environment, Color::standard_black);
                m_pass_state.cloud_environment_dirty = false;
            }
            return;
        }

        const bool cadence_frame = (m_cb_frame_cpu.frame & 31u) == 0u;
        if (!m_pass_state.cloud_environment_dirty && !cadence_frame)
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::clouds_environment_c);
        if (!shader || !shader->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("clouds_environment");
        {
            RHI_PipelineState pso;
            pso.name             = "clouds_environment";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, GetRenderTarget(Renderer_RenderTarget::skysphere));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, GetRenderTarget(Renderer_RenderTarget::cloud_noise));
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_environment, 0, 1);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(tex_environment);
            Pass_Downscale(cmd_list, tex_environment, Renderer_DownsampleFilter::Average);

            RHI_PipelineState filter_pso;
            filter_pso.name             = "clouds_environment_filter";
            filter_pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_environment_filter_c);
            cmd_list->SetPipelineState(filter_pso);
            const uint32_t mip_count = tex_environment->GetMipCount();
            const uint32_t base_w    = tex_environment->GetWidth();
            const uint32_t base_h    = tex_environment->GetHeight();
            for (uint32_t mip_level = 1; mip_level < mip_count; mip_level++)
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_environment, 0, mip_level);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_environment, mip_level, 1);
                m_pcb_pass_cpu.set_f3_value(static_cast<float>(mip_level), static_cast<float>(mip_count), 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch((max(1u, base_w >> mip_level) + 7) / 8, (max(1u, base_h >> mip_level) + 7) / 8);
            }
        }
        cmd_list->EndTimeblock();
        m_pass_state.cloud_environment_dirty = false;
    }

    bool Renderer::Pass_Clouds_Prepare(RHI_CommandList* cmd_list, uint32_t eye_layer)
    {
        if (!World::GetDirectionalLight())
        {
            m_pass_state.cloud_history_valid = false;
            return false;
        }

        RHI_Shader* shader_render    = GetShader(Renderer_Shader::clouds_render_c);
        RHI_Shader* shader_temporal  = GetShader(Renderer_Shader::clouds_temporal_c);
        RHI_Shader* shader_composite = GetShader(Renderer_Shader::clouds_composite_c);
        if (!shader_render || !shader_render->IsCompiled() || !shader_temporal || !shader_temporal->IsCompiled() || !shader_composite || !shader_composite->IsCompiled())
        {
            m_pass_state.cloud_history_valid = false;
            return false;
        }

        cmd_list->BeginTimeblock("clouds_prepare");
        {
            cmd_list->BeginTimeblock("clouds_render");
            Pass_Clouds_Render(cmd_list, eye_layer);
            cmd_list->EndTimeblock();

            cmd_list->BeginTimeblock("clouds_temporal");
            Pass_Clouds_Temporal(cmd_list, eye_layer);
            cmd_list->EndTimeblock();
        }
        cmd_list->EndTimeblock();
        return true;
    }

    void Renderer::Pass_Clouds(RHI_CommandList* cmd_list, uint32_t eye_layer, bool last_eye)
    {
        cmd_list->BeginTimeblock("clouds_composite");
        {
            Pass_Clouds_Composite(cmd_list, eye_layer, GetRenderTarget(Renderer_RenderTarget::frame_render));
        }
        cmd_list->EndTimeblock();

        if (last_eye)
        {
            m_pass_state.cloud_history_index = 1u - m_pass_state.cloud_history_index;
            m_pass_state.cloud_history_valid = true;
        }
    }

    void Renderer::Pass_Lut_BrdfSpecular(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_lut_brdf_specular = GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular);

        cmd_list->BeginTimeblock("lut_brdf_specular");
        {
            RHI_PipelineState pso;
            pso.name             = "lut_brdf_specular";
            pso.shaders[Compute] = GetShader(Renderer_Shader::light_integration_brdf_specular_lut_c);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_brdf_specular);
            cmd_list->Dispatch(tex_lut_brdf_specular);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Lut_AtmosphericScattering(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);

        cmd_list->BeginTimeblock("lut_atmospheric_scattering");
        {
            // transmittance lut
            {
                RHI_PipelineState pso;
                pso.name             = "lut_atmosphere_transmittance";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_transmittance_lut_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_atmosphere_transmittance);
                cmd_list->Dispatch(tex_lut_atmosphere_transmittance);
            }

            // multi-scatter lut
            {
                RHI_PipelineState pso;
                pso.name             = "lut_atmosphere_multiscatter";
                pso.shaders[Compute] = GetShader(Renderer_Shader::skysphere_multiscatter_lut_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_lut_atmosphere_multiscatter);
                cmd_list->Dispatch(tex_lut_atmosphere_multiscatter);
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_CloudNoise(RHI_CommandList* cmd_list)
    {
        // bakes the 3d noise volume sampled by the cloud passes
        // expensive but only runs once at startup
        RHI_Texture* tex_cloud_noise = GetRenderTarget(Renderer_RenderTarget::cloud_noise);
        if (!tex_cloud_noise)
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::clouds_noise_c);
        if (!shader || !shader->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("cloud_noise");
        {
            RHI_PipelineState pso;
            pso.name             = "cloud_noise";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex3d, tex_cloud_noise);
            cmd_list->Dispatch(tex_cloud_noise);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_WindField(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_wind = GetRenderTarget(Renderer_RenderTarget::wind_field);
        if (!tex_wind)
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::wind_field_c);
        if (!shader || !shader->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("wind_field");
        {
            RHI_PipelineState pso;
            pso.name             = "wind_field";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_wind);
            cmd_list->Dispatch(tex_wind);
        }
        cmd_list->EndTimeblock();
    }
}
