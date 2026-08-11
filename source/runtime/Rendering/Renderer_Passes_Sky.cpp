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
            const bool sky_state_changed =
                m_pass_state.sky_state_changed_this_frame;
            const bool refresh_sky_view_lut =
                sky_state_changed ||
                m_pass_state.sky_warmup_this_frame ||
                (m_cb_frame_cpu.frame & 7u) == 0u;
            const bool refresh_cloud_shadow =
                sky_state_changed ||
                m_pass_state.sky_warmup_this_frame ||
                (m_cb_frame_cpu.frame & 3u) == 0u;

            // the sun always exists in light slot 0, either the world's directional light or the
            // neutral default UpdateLights writes when the world has no lights, so the panorama is
            // built unconditionally instead of falling back to black
            {
                // sky view lut, one small march per texel instead of integrating the atmosphere per panorama pixel
                if (refresh_sky_view_lut)
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

                // cumulus transmittance along the sun on the cloud base plane, the fog march samples it for sun shafts
                if (refresh_cloud_shadow)
                {
                    RHI_PipelineState pso;
                    pso.name             = "cloud_shadow";
                    pso.shaders[Compute] = GetShader(Renderer_Shader::clouds_shadow_c);
                    cmd_list->SetPipelineState(pso);

                    cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_cloud_shadow);
                    cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, tex_cloud_noise);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->Dispatch(tex_cloud_shadow);

                    // the raw bake has texel-rate edges that moire against the screen grid, blur and mips remove them
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

                // values[0].x is the warmup blend, 0.0 in steady state selects the partial dispatch mode in the shader
                m_pcb_pass_cpu.set_f3_value(m_pass_state.sky_warmup_this_frame ? m_pass_state.sky_warmup_blend : 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                if (m_pass_state.sky_warmup_this_frame)
                {
                    cmd_list->Dispatch(tex_skysphere);
                }
                else
                {
                    // steady state dispatches one sixteenth of the threads, the shader cycles a 4x4 tile offset so every pixel refreshes every 16 frames
                    const uint32_t thread_group_size = 8;
                    const uint32_t quarter_w         = (tex_skysphere->GetWidth()  + 3) / 4;
                    const uint32_t quarter_h         = (tex_skysphere->GetHeight() + 3) / 4;
                    const uint32_t dispatch_x        = (quarter_w + thread_group_size - 1) / thread_group_size;
                    const uint32_t dispatch_y        = (quarter_h + thread_group_size - 1) / thread_group_size;
                    cmd_list->Dispatch(dispatch_x, dispatch_y);
                }
            }

            // averaging downsampler every fourth frame, the ggx prefilter below is warmup only since clouds mostly drive diffuse ibl
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

                        // sized to the mip, not the base panorama, so no thread launches are wasted on bounds checks
                        const uint32_t mip_w     = max(1u, base_w >> mip_level);
                        const uint32_t mip_h     = max(1u, base_h >> mip_level);
                        const uint32_t dispatch_x = (mip_w + 7) / 8;
                        const uint32_t dispatch_y = (mip_h + 7) / 8;
                        cmd_list->Dispatch(dispatch_x, dispatch_y);
                    }
                }

                // rebuild l2 sh when the panorama mips are fresh
                if (do_downscale)
                {
                    Pass_Skysphere_SH_Project(cmd_list);
                }
            }
        }
        cmd_list->EndTimeblock();

        Pass_Clouds_Environment(cmd_list);
    }

    void Renderer::Pass_Skysphere_SH_Project(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_sky_sh    = GetRenderTarget(Renderer_RenderTarget::sky_sh);
        RHI_Shader* shader         = GetShader(Renderer_Shader::skysphere_sh_project_c);
        if (!tex_skysphere || !tex_sky_sh || !shader || !shader->IsCompiled())
        {
            return;
        }

        cmd_list->BeginTimeblock("skysphere_sh_project");
        {
            RHI_PipelineState pso;
            pso.name             = "skysphere_sh_project";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_skysphere);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_sky_sh);
            cmd_list->Dispatch(1, 1, 1);
        }
        cmd_list->EndTimeblock();
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
                m_pass_state.cloud_environment_dirty  = false;
                m_pass_state.cloud_environment_baking = false;
                m_pass_state.cloud_environment_strip  = 0;
            }
            return;
        }

        const bool cadence_frame = (m_cb_frame_cpu.frame & 31u) == 0u;
        if (m_pass_state.cloud_environment_dirty)
        {
            m_pass_state.cloud_environment_baking = true;
            m_pass_state.cloud_environment_strip  = 0;
            m_pass_state.cloud_environment_dirty  = false;
        }
        else if (cadence_frame && !m_pass_state.cloud_environment_baking)
        {
            m_pass_state.cloud_environment_baking = true;
            m_pass_state.cloud_environment_strip  = 0;
        }

        if (!m_pass_state.cloud_environment_baking)
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::clouds_environment_c);
        if (!shader || !shader->IsCompiled())
        {
            return;
        }

        const uint32_t width  = tex_environment->GetWidth();
        const uint32_t height = tex_environment->GetHeight();
        const uint32_t strips = 4;
        const uint32_t strip_h = (height + strips - 1) / strips;
        const uint32_t strip   = m_pass_state.cloud_environment_strip;
        const uint32_t y0      = strip * strip_h;
        const uint32_t y1      = min(height, y0 + strip_h);
        const uint32_t rows    = y1 > y0 ? (y1 - y0) : 0;
        if (rows == 0)
        {
            m_pass_state.cloud_environment_baking = false;
            m_pass_state.cloud_environment_strip  = 0;
            return;
        }

        cmd_list->BeginTimeblock("clouds_environment");
        {
            RHI_PipelineState pso;
            pso.name             = "clouds_environment";
            pso.shaders[Compute] = shader;
            cmd_list->SetPipelineState(pso);

            // x = pixel y offset for this strip, y unused
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(y0), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, GetRenderTarget(Renderer_RenderTarget::skysphere));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex3d, GetRenderTarget(Renderer_RenderTarget::cloud_noise));
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_environment, 0, 1);
            cmd_list->Dispatch((width + 7) / 8, (rows + 7) / 8);

            m_pass_state.cloud_environment_strip++;
            if (m_pass_state.cloud_environment_strip >= strips)
            {
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

                m_pass_state.cloud_environment_baking = false;
                m_pass_state.cloud_environment_strip  = 0;
            }
        }
        cmd_list->EndTimeblock();
    }

    bool Renderer::Pass_Clouds_Prepare(RHI_CommandList* cmd_list, uint32_t eye_layer)
    {
        if (!World::GetDirectionalLight())
        {
            m_pass_state.cloud_history_valid = false;
            return false;
        }

        // a secondary view has its own camera, reprojecting the cloud history against it would
        // corrupt the primary camera's clouds, the validity flag is left alone so the primary
        // keeps accumulating where it left off
        if (IsSecondaryViewActive())
        {
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
