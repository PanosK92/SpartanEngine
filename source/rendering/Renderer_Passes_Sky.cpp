/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================================
#include "pch.h"
#include "Renderer_Internal.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Light.h"
#include "SurfaceWater.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Shader.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::Pass_Skysphere()
    {
        RHI_Texture* tex_skysphere                    = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);
        RHI_Texture* tex_lut_sky_view                 = GetRenderTarget(Renderer_RenderTarget::lut_sky_view);
        RHI_Texture* tex_cloud_noise                  = GetRenderTarget(Renderer_RenderTarget::cloud_noise);
        RHI_Texture* tex_cloud_shadow                 = GetRenderTarget(Renderer_RenderTarget::cloud_shadow);

        {
            RenderPass pass("skysphere");

            // the periodic refreshes below each own a different frame of a four frame cycle, landing
            // them all on the same frame made every fourth frame visibly longer than the rest
            const uint64_t frame         = Renderer::view().frame.frame;
            const bool sky_state_changed =
                m_pass_state.sky.state_changed_this_frame;
            const bool refresh_sky_view_lut =
                sky_state_changed ||
                m_pass_state.sky.warmup_this_frame ||
                (frame & 7u) == 0u;

            // the cloud shadow map is centered on a texel snapped anchor that the shaders derive from
            // the camera and the sun, see cloud_shadow_plane_uv in cloud_shadow.hlsl. the map has to be
            // baked again on the frame the anchor moves to another texel, a bake on a fixed cadence
            // left it one texel off for up to three frames every time a moving camera crossed one.
            // in between the clouds drift a fraction of a texel, so the cadence itself can be slow
            bool cloud_shadow_anchor_moved = false;
            bool cloud_shadow_clear        = false;
            {
                const float base_altitude = 1500.0f; // cumulus_bottom_alt
                const float half_extent   = 8000.0f; // cloud_shadow_half_extent
                const float min_sun_y     = 0.087f;  // cloud_shadow_min_sun_y
                Light* sun                = World::GetDirectionalLight();
                const Vector3 sun_dir     = sun && sun->GetEntity() ? -sun->GetEntity()->GetForward() : Vector3(0.0f, 1.0f, 0.0f);
                const Vector3 camera      = Renderer::view().frame.camera_position;
                const float texel_size    = 2.0f * half_extent / static_cast<float>(tex_cloud_shadow->GetWidth());
                const float sun_y         = max(sun_dir.y, min_sun_y);
                const float lift          = (base_altitude - camera.y) / sun_y;
                const int32_t anchor_x    = static_cast<int32_t>(floorf((camera.x + sun_dir.x * lift) / texel_size + 0.5f));
                const int32_t anchor_z    = static_cast<int32_t>(floorf((camera.z + sun_dir.z * lift) / texel_size + 0.5f));
                cloud_shadow_anchor_moved = anchor_x != m_pass_state.sky.cloud_shadow_anchor_x || anchor_z != m_pass_state.sky.cloud_shadow_anchor_z;
                m_pass_state.sky.cloud_shadow_anchor_x = anchor_x;
                m_pass_state.sky.cloud_shadow_anchor_z = anchor_z;

                // a clear sky or a sun below the horizon bakes a map of ones, which only has to be done once
                cloud_shadow_clear = Renderer::view().frame.cloud_coverage <= 0.0f || sun_dir.y <= 0.0f;
            }
            const bool cloud_shadow_stays_clear = cloud_shadow_clear && m_pass_state.sky.cloud_shadow_clear;
            const bool refresh_cloud_shadow =
                sky_state_changed ||
                m_pass_state.sky.warmup_this_frame ||
                (!cloud_shadow_stays_clear && (cloud_shadow_anchor_moved || (frame & 15u) == 2u));

            // Slot 0 contains the authored sun, the empty editor's default light, or a
            // zero-intensity sentinel when no lights are visible in a loaded world.
            {
                // sky view lut, one small march per texel instead of integrating the atmosphere per panorama pixel
                if (refresh_sky_view_lut)
                {
                    pass.Shader(Renderer_Shader::skysphere_sky_view_lut_c, "skysphere_sky_view_lut");
                    pass.Bind(Renderer_BindingsUav::tex,  tex_lut_sky_view);
                    pass.Bind(Renderer_BindingsSrv::tex,  tex_lut_atmosphere_transmittance);
                    pass.Bind(Renderer_BindingsSrv::tex2, tex_lut_atmosphere_multiscatter);
                    pass.Dispatch(tex_lut_sky_view);
                    if (!pass.Ready())
                    {
                        m_pass_state.sky.first_frame = true;
                    }
                }

                // cumulus transmittance along the sun on the cloud base plane, the fog march samples it for sun shafts
                if (refresh_cloud_shadow)
                {
                    pass.Shader(Renderer_Shader::clouds_shadow_c, "cloud_shadow");
                    pass.Bind(Renderer_BindingsUav::tex,   tex_cloud_shadow);
                    pass.Bind(Renderer_BindingsSrv::tex3d, tex_cloud_noise);
                    pass.Dispatch(tex_cloud_shadow);
                    if (!pass.Ready())
                    {
                        m_pass_state.sky.first_frame = true;
                    }
                    m_pass_state.sky.cloud_shadow_clear = cloud_shadow_clear && pass.Ready();

                    // the raw bake has texel-rate edges that moire against the screen grid, blur and mips remove them
                    // Two texels cover the same 62.5 m penumbra at the lower bake resolution.
                    Pass_Blur(tex_cloud_shadow, false, 2.0f, 0);
                    Pass_Downscale(tex_cloud_shadow, Renderer_DownsampleFilter::Average);
                }

                pass.Shader(Renderer_Shader::skysphere_c, "skysphere_atmospheric_scattering");
                pass.Bind(Renderer_BindingsUav::tex,  tex_skysphere);
                pass.Bind(Renderer_BindingsSrv::tex,  tex_lut_atmosphere_transmittance);
                pass.Bind(Renderer_BindingsSrv::tex2, tex_lut_atmosphere_multiscatter);
                pass.Bind(Renderer_BindingsSrv::tex3, tex_lut_sky_view); // catalog stars + procedural milky way, drawn into the panorama like before
                RHI_Texture* tex_stars = GetStandardTexture(Renderer_StandardTexture::Sky_stars);
                RHI_Texture* tex_grid  = GetStandardTexture(Renderer_StandardTexture::Sky_star_grid);
                pass.Bind(Renderer_BindingsSrv::tex5, tex_stars ? tex_stars : GetStandardTexture(Renderer_StandardTexture::Black));
                pass.Bind(Renderer_BindingsSrv::tex6, tex_grid  ? tex_grid  : GetStandardTexture(Renderer_StandardTexture::Black));

                // 0.0 in steady state selects the partial dispatch mode in the shader
                pass.Set(pass_skysphere::warmup_blend, m_pass_state.sky.warmup_this_frame ? m_pass_state.sky.warmup_blend : 0.0f);

                if (m_pass_state.sky.warmup_this_frame)
                {
                    pass.Dispatch(tex_skysphere);
                    if (!pass.Ready())
                    {
                        m_pass_state.sky.first_frame = true;
                    }
                }
                else
                {
                    // steady state dispatches one sixteenth of the threads, the shader cycles a 4x4 tile offset so every pixel refreshes every 16 frames
                    const uint32_t thread_group_size = 8;
                    const uint32_t quarter_w         = (tex_skysphere->GetWidth()  + 3) / 4;
                    const uint32_t quarter_h         = (tex_skysphere->GetHeight() + 3) / 4;
                    const uint32_t dispatch_x        = (quarter_w + thread_group_size - 1) / thread_group_size;
                    const uint32_t dispatch_y        = (quarter_h + thread_group_size - 1) / thread_group_size;
                    pass.Dispatch(dispatch_x, dispatch_y);
                    if (!pass.Ready())
                    {
                        m_pass_state.sky.first_frame = true;
                    }
                }
            }

            // averaging downsampler aligned with the 16-frame 4x4 tile refresh cycle, the ggx prefilter below is warmup only since clouds mostly drive diffuse ibl
            {
                const bool do_downscale = m_pass_state.sky.warmup_this_frame || ((frame & 15u) == 1u);
                if (do_downscale)
                {
                    Pass_Downscale(tex_skysphere, Renderer_DownsampleFilter::Average);
                }

                if (m_pass_state.sky.warmup_this_frame)
                {
                    pass.Shader(Renderer_Shader::light_integration_environment_filter_c, "skysphere_filter");

                    const uint32_t mip_count = tex_skysphere->GetMipCount();
                    const uint32_t base_w    = tex_skysphere->GetWidth();
                    const uint32_t base_h    = tex_skysphere->GetHeight();
                    for (uint32_t mip_level = 1; mip_level < mip_count; mip_level++)
                    {
                        pass.Bind(Renderer_BindingsSrv::tex, tex_skysphere, 0, mip_level);
                        pass.Bind(Renderer_BindingsUav::tex, tex_skysphere, mip_level, 1);
                        pass.Set(pass_light_integration::mip_level, mip_level);
                        pass.Set(pass_light_integration::mip_count, mip_count);

                        // sized to the mip, not the base panorama, so no thread launches are wasted on bounds checks
                        const uint32_t mip_w      = max(1u, base_w >> mip_level);
                        const uint32_t mip_h      = max(1u, base_h >> mip_level);
                        const uint32_t dispatch_x = (mip_w + 7) / 8;
                        const uint32_t dispatch_y = (mip_h + 7) / 8;
                        pass.Dispatch(dispatch_x, dispatch_y);
                        if (!pass.Ready())
                        {
                            m_pass_state.sky.first_frame = true;
                        }
                    }
                }

                // rebuild l2 sh when the panorama mips are fresh, once a cloud environment exists
                // the sh comes from it instead and is rebuilt at the end of each of its bakes
                if (do_downscale && !m_pass_state.cloud_environment.environment_ready)
                {
                    Pass_Skysphere_SH_Project();
                }
            }
        }

        Pass_Clouds_Environment();
    }

    void Renderer::Pass_Skysphere_SH_Project()
    {
        // the sky lights the scene through its clouds, the cloud environment is the panorama with
        // the clouds marched into it, the bare panorama only stands in until its first bake is done
        RHI_Texture* tex_skysphere = GetRenderTarget(m_pass_state.cloud_environment.environment_ready ? Renderer_RenderTarget::cloud_environment : Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_sky_sh    = GetRenderTarget(Renderer_RenderTarget::sky_sh);
        RHI_Shader* shader         = GetShader(Renderer_Shader::skysphere_sh_project_c);
        if (!tex_skysphere || !tex_sky_sh || !shader || !shader->IsCompiled())
        {
            return;
        }

        RenderPass pass("skysphere_sh_project", shader);
        pass.Bind(Renderer_BindingsSrv::tex, tex_skysphere);
        pass.Bind(Renderer_BindingsUav::tex, tex_sky_sh);
        pass.Dispatch(1, 1, 1);
    }

    void Renderer::Pass_Clouds_Render(uint32_t eye_layer)
    {
        RHI_Texture* tex_raw      = GetRenderTarget(Renderer_RenderTarget::cloud_raw);
        RHI_Texture* tex_distance = GetRenderTarget(Renderer_RenderTarget::cloud_raw_distance);

        RenderPass pass("clouds_render", Renderer_Shader::clouds_render_c);
        pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex,           Renderer_RenderTarget::lut_atmosphere_transmittance);
        pass.Bind(Renderer_BindingsSrv::tex2,          Renderer_RenderTarget::lut_sky_view); // sky fill on the shaded side and the haze in front of far clouds
        pass.Bind(Renderer_BindingsSrv::tex3d,         Renderer_RenderTarget::cloud_noise);
        pass.Bind(Renderer_BindingsUav::tex,           tex_raw, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsUav::tex2,          tex_distance, rhi_all_mips, 0, eye_layer);
        const uint32_t dispatch_width  = (tex_raw->GetWidth() + 1) / 2;
        const uint32_t dispatch_height = (tex_raw->GetHeight() + 1) / 2;
        pass.Dispatch((dispatch_width + 7) / 8, (dispatch_height + 7) / 8);
    }

    void Renderer::Pass_Clouds_Temporal(uint32_t eye_layer)
    {
        const Renderer_RenderTarget resolved_targets[] = { Renderer_RenderTarget::cloud_resolved_0, Renderer_RenderTarget::cloud_resolved_1 };
        const Renderer_RenderTarget distance_targets[] = { Renderer_RenderTarget::cloud_resolved_distance_0, Renderer_RenderTarget::cloud_resolved_distance_1 };
        const uint32_t history_index = m_pass_state.cloud_history.Read();
        const uint32_t output_index  = m_pass_state.cloud_history.Write();
        RHI_Texture* tex_output          = GetRenderTarget(resolved_targets[output_index]);
        RHI_Texture* tex_output_distance = GetRenderTarget(distance_targets[output_index]);

        RenderPass pass("clouds_temporal", Renderer_Shader::clouds_temporal_c);
        pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex,           Renderer_RenderTarget::cloud_raw, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex2,          Renderer_RenderTarget::cloud_raw_distance, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex3,          resolved_targets[history_index], rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex4,          distance_targets[history_index], rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex5,          Renderer_RenderTarget::gbuffer_depth_previous, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsUav::tex,           tex_output, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsUav::tex2,          tex_output_distance, rhi_all_mips, 0, eye_layer);
        pass.Set(pass_clouds::reset_history, !m_pass_state.cloud_history.valid);
        pass.Dispatch(tex_output);
    }

    void Renderer::Pass_Clouds_Composite(uint32_t eye_layer, RHI_Texture* tex_scene)
    {
        const Renderer_RenderTarget resolved_targets[] = { Renderer_RenderTarget::cloud_resolved_0, Renderer_RenderTarget::cloud_resolved_1 };
        const Renderer_RenderTarget distance_targets[] = { Renderer_RenderTarget::cloud_resolved_distance_0, Renderer_RenderTarget::cloud_resolved_distance_1 };
        const uint32_t output_index = m_pass_state.cloud_history.Write();

        RenderPass pass("clouds_composite", Renderer_Shader::clouds_composite_c);
        // Every invocation reads and writes its own scene pixel. The refraction snapshot needs colour only.
        pass.Set(pass_clouds::write_velocity, tex_scene == GetRenderTarget(Renderer_RenderTarget::frame_render));
        pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex2,          resolved_targets[output_index], rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex3,          distance_targets[output_index], rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsSrv::tex4,          Renderer_RenderTarget::gbuffer_velocity, rhi_all_mips, 0, eye_layer);
        pass.Bind(Renderer_BindingsUav::tex,           tex_scene);
        pass.Bind(Renderer_BindingsUav::tex2,          Renderer_RenderTarget::cloud_velocity, rhi_all_mips, 0, eye_layer);
        pass.Dispatch(tex_scene, GetResolutionScale());
    }

    void Renderer::Pass_Clouds_Environment()
    {
        RHI_Texture* tex_environment = GetRenderTarget(Renderer_RenderTarget::cloud_environment);
        if (!tex_environment)
        {
            return;
        }

        if (!World::GetDirectionalLight())
        {
            if (m_pass_state.cloud_environment.environment_dirty)
            {
                RHI_CommandList::ClearTexture(tex_environment, Color::standard_black);
                m_pass_state.cloud_environment.environment_dirty  = false;
                m_pass_state.cloud_environment.environment_baking = false;
                m_pass_state.cloud_environment.environment_strip  = 0;
            }
            m_pass_state.cloud_environment.environment_ready = false;
            return;
        }

        // a sky change is answered at once, one strip a frame. the rolling refresh that follows
        // the drifting clouds bakes its strips on the frame of the four frame cycle that no
        // other sky refresh uses, see Pass_Skysphere
        const uint64_t frame     = Renderer::view().frame.frame;
        const bool cadence_frame = (frame & 31u) == 3u;
        if (m_pass_state.cloud_environment.environment_dirty)
        {
            m_pass_state.cloud_environment.environment_baking = true;
            m_pass_state.cloud_environment.environment_urgent = true;
            m_pass_state.cloud_environment.environment_strip  = 0;
            m_pass_state.cloud_environment.environment_dirty  = false;
        }
        else if (cadence_frame && !m_pass_state.cloud_environment.environment_baking)
        {
            m_pass_state.cloud_environment.environment_baking = true;
            m_pass_state.cloud_environment.environment_urgent = false;
            m_pass_state.cloud_environment.environment_strip  = 0;
        }

        if (!m_pass_state.cloud_environment.environment_baking)
        {
            return;
        }

        if (!m_pass_state.cloud_environment.environment_urgent && (frame & 3u) != 3u)
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
        const uint32_t strip   = m_pass_state.cloud_environment.environment_strip;
        const uint32_t y0      = strip * strip_h;
        const uint32_t y1      = min(height, y0 + strip_h);
        const uint32_t rows    = y1 > y0 ? (y1 - y0) : 0;
        if (rows == 0)
        {
            m_pass_state.cloud_environment.environment_baking = false;
            m_pass_state.cloud_environment.environment_strip  = 0;
            return;
        }

        {
            RenderPass pass("clouds_environment", shader);
            pass.Set(pass_clouds::environment_row, y0);
            pass.Bind(Renderer_BindingsSrv::tex,   Renderer_RenderTarget::skysphere);
            pass.Bind(Renderer_BindingsSrv::tex2,  Renderer_RenderTarget::lut_atmosphere_transmittance);
            pass.Bind(Renderer_BindingsSrv::tex3,  Renderer_RenderTarget::lut_sky_view);
            pass.Bind(Renderer_BindingsSrv::tex3d, Renderer_RenderTarget::cloud_noise);
            pass.Bind(Renderer_BindingsUav::tex,   tex_environment, 0, 1);
            pass.Dispatch((width + 7) / 8, (rows + 7) / 8);

            // a pipeline that is still compiling drew nothing, the strip is tried again next time
            if (pass.Ready())
            {
                m_pass_state.cloud_environment.environment_strip++;
            }
            if (m_pass_state.cloud_environment.environment_strip >= strips)
            {
                Pass_Downscale(tex_environment, Renderer_DownsampleFilter::Average);

                pass.Shader(Renderer_Shader::light_integration_environment_filter_c, "clouds_environment_filter");
                const uint32_t mip_count = tex_environment->GetMipCount();
                const uint32_t base_w    = tex_environment->GetWidth();
                const uint32_t base_h    = tex_environment->GetHeight();
                for (uint32_t mip_level = 1; mip_level < mip_count; mip_level++)
                {
                    pass.Bind(Renderer_BindingsSrv::tex, tex_environment, 0, mip_level);
                    pass.Bind(Renderer_BindingsUav::tex, tex_environment, mip_level, 1);
                    pass.Set(pass_light_integration::mip_level, mip_level);
                    pass.Set(pass_light_integration::mip_count, mip_count);
                    pass.Dispatch((max(1u, base_w >> mip_level) + 7) / 8, (max(1u, base_h >> mip_level) + 7) / 8);
                }

                m_pass_state.cloud_environment.environment_baking = false;
                m_pass_state.cloud_environment.environment_strip  = 0;
                m_pass_state.cloud_environment.environment_ready  = true;
            }
        }

        // the bake just finished, the diffuse sky light follows the clouds so project the fresh environment
        if (!m_pass_state.cloud_environment.environment_baking)
        {
            Pass_Skysphere_SH_Project();
        }
    }

    bool Renderer::Pass_Clouds_Prepare(uint32_t eye_layer)
    {
        if (!World::GetDirectionalLight())
        {
            m_pass_state.cloud_history.valid = false;
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
            m_pass_state.cloud_history.valid = false;
            return false;
        }

        RenderPass pass("clouds_prepare");
        Pass_Clouds_Render(eye_layer);
        Pass_Clouds_Temporal(eye_layer);
        return true;
    }

    void Renderer::Pass_Clouds(uint32_t eye_layer, bool last_eye)
    {
        Pass_Clouds_Composite(eye_layer, GetRenderTarget(Renderer_RenderTarget::frame_render));

        if (last_eye)
        {
            m_pass_state.cloud_history.Advance();
        }
    }

    bool Renderer::Pass_Lut_BrdfSpecular()
    {
        RHI_Texture* tex_lut_brdf_specular = GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular);

        RenderPass pass("lut_brdf_specular", Renderer_Shader::light_integration_brdf_specular_lut_c);
        pass.Bind(Renderer_BindingsUav::tex, tex_lut_brdf_specular);
        pass.Dispatch(tex_lut_brdf_specular);
        return pass.Ready();
    }

    bool Renderer::Pass_Lut_AtmosphericScattering()
    {
        RHI_Texture* tex_lut_atmosphere_transmittance = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_transmittance);
        RHI_Texture* tex_lut_atmosphere_multiscatter  = GetRenderTarget(Renderer_RenderTarget::lut_atmosphere_multiscatter);

        bool produced = true;
        RenderPass pass("lut_atmospheric_scattering");

        // transmittance lut
        pass.Shader(Renderer_Shader::skysphere_transmittance_lut_c, "lut_atmosphere_transmittance");
        pass.Bind(Renderer_BindingsUav::tex, tex_lut_atmosphere_transmittance);
        pass.Dispatch(tex_lut_atmosphere_transmittance);
        produced &= pass.Ready();

        // multi-scatter lut
        pass.Shader(Renderer_Shader::skysphere_multiscatter_lut_c, "lut_atmosphere_multiscatter");
        pass.Bind(Renderer_BindingsSrv::tex, tex_lut_atmosphere_transmittance);
        pass.Bind(Renderer_BindingsUav::tex, tex_lut_atmosphere_multiscatter);
        pass.Dispatch(tex_lut_atmosphere_multiscatter);
        produced &= pass.Ready();

        return produced;
    }

    bool Renderer::Pass_CloudNoise()
    {
        // bakes the 3d noise volume sampled by the cloud passes
        // expensive but only runs once at startup
        RHI_Texture* tex_cloud_noise = GetRenderTarget(Renderer_RenderTarget::cloud_noise);
        if (!tex_cloud_noise)
        {
            return false;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::clouds_noise_c);
        if (!shader || !shader->IsCompiled())
        {
            return false;
        }

        RenderPass pass("cloud_noise", shader);
        pass.Bind(Renderer_BindingsUav::tex3d, tex_cloud_noise);
        pass.Dispatch(tex_cloud_noise);
        return pass.Ready();
    }

    void Renderer::Pass_WindField()
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

        RenderPass pass("wind_field", shader);
        pass.Bind(Renderer_BindingsUav::tex, tex_wind);
        pass.Dispatch(tex_wind);
    }

    void Renderer::Pass_CarRain()
    {
        SurfaceWaterState& state = m_pass_state.surface_water;
        if (!Renderer::GetSurfaceWater().active)
            return;

        // a new bake or a reset, the textures are rebuilt from the cpu copies, which already hold this frame's changes
        if (!state.uploaded || Renderer::GetSurfaceWater().version != state.version || Renderer::GetSurfaceWater().entity_id != state.entity_id)
        {
            state.version = Renderer::GetSurfaceWater().version;
            state.entity_id = Renderer::GetSurfaceWater().entity_id;
            state.uploaded = true;
            CreateCarRainTargets(Renderer::GetSurfaceWater().width, Renderer::GetSurfaceWater().height, Renderer::GetSurfaceWater().surface.data(), Renderer::GetSurfaceWater().micro.data());

            // the dirt shares the atlas, its targets are sized with it so a wash or a soil only refills them
            CreateCarDirtTargets(Renderer::GetSurfaceWater().width, Renderer::GetSurfaceWater().height, nullptr, nullptr, nullptr);
            state.dirt_uploaded = false;
        }

        RHI_Texture* tex_micro     = GetRenderTarget(Renderer_RenderTarget::car_rain_micro);
        RHI_Texture* tex_ids       = GetRenderTarget(Renderer_RenderTarget::car_rain_ids);
        RHI_Shader* shader_clear   = GetShader(Renderer_Shader::car_rain_clear_c);
        RHI_Shader* shader_splat   = GetShader(Renderer_Shader::car_rain_splat_c);
        RHI_Shader* shader_texels  = GetShader(Renderer_Shader::car_rain_texels_c);
        if (!tex_micro || !tex_ids || !shader_clear || !shader_clear->IsCompiled() || !shader_splat || !shader_splat->IsCompiled() || !shader_texels || !shader_texels->IsCompiled())
            return;

        const uint32_t width       = Renderer::GetSurfaceWater().width;
        const uint32_t height      = Renderer::GetSurfaceWater().height;
        const uint32_t drop_count  = min(static_cast<uint32_t>(Renderer::GetSurfaceWater().drops.size()), SurfaceWater::drops_max);
        const uint32_t texel_count = min(static_cast<uint32_t>(Renderer::GetSurfaceWater().texels.size()), SurfaceWater::texels_max);

        {
            RenderPass pass("car_rain");

            // which drop covers each texel, from scratch every frame
            pass.Shader(shader_clear);
            pass.Bind("tex_car_rain_ids_uav", tex_ids);
            pass.Set(pass_car_rain::atlas_width, width);
            pass.Set(pass_car_rain::atlas_height, height);
            pass.Dispatch((width + 7) / 8, (height + 7) / 8);

            if (drop_count > 0)
            {
                pass.Shader(shader_splat);
                pass.Bind("tex_car_rain_ids_uav", tex_ids);
                pass.Bind("car_rain_drops", Renderer_Buffer::CarRainDrops);
                pass.Set(pass_car_rain::offset, (m_frame_resource_index % renderer_draw_data_buffer_count) * SurfaceWater::drops_max);
                pass.Set(pass_car_rain::count, drop_count);
                pass.Set(pass_car_rain::atlas_width, width);
                pass.Set(pass_car_rain::atlas_height, height);
                pass.Set(pass_car_rain::texel_size, Renderer::GetSurfaceWater().texel_size);
                pass.Dispatch((drop_count + 63) / 64, 1);
            }

            // the micro water the simulation changed this frame
            if (texel_count > 0)
            {
                pass.Shader(shader_texels);
                pass.Bind("tex_car_rain_micro_uav", tex_micro);
                pass.Bind("car_rain_texels", Renderer_Buffer::CarRainTexels);
                pass.Set(pass_car_rain::offset, m_frame_resource_index * SurfaceWater::texels_max);
                pass.Set(pass_car_rain::count, texel_count);
                pass.Set(pass_car_rain::atlas_width, width);
                pass.Set(pass_car_rain::atlas_height, height);
                pass.Dispatch((texel_count + 63) / 64, 1);
            }
        }

        // the dirt and the paint damage in the same atlas, rebuilt from the cpu copy after a wash or a new bake, patched with this frame's changes otherwise
        const SurfaceWater& water = Renderer::GetSurfaceWater();
        if (!water.dirt_active || water.dirt_color.size() != static_cast<size_t>(width) * height || water.dirt_state.size() != water.dirt_color.size() || water.dirt_wear.size() != water.dirt_color.size())
            return;

        if (!state.dirt_uploaded || water.dirt_version != state.dirt_version || state.dirt_width != width || state.dirt_height != height)
        {
            state.dirt_version  = water.dirt_version;
            state.dirt_width    = width;
            state.dirt_height   = height;
            state.dirt_uploaded = true;

            RHI_Texture* targets[] = { GetRenderTarget(Renderer_RenderTarget::car_dirt_color), GetRenderTarget(Renderer_RenderTarget::car_dirt_state), GetRenderTarget(Renderer_RenderTarget::car_dirt_wear) };
            const uint32_t* sources[] = { water.dirt_color.data(), water.dirt_state.data(), water.dirt_wear.data() };
            for (RHI_Texture* target : targets)
            {
                if (!target || target->GetWidth() != width || target->GetHeight() != height)
                {
                    state.dirt_uploaded = false;
                    return;
                }
            }
            for (uint32_t i = 0; i < 3; i++)
            {
                targets[i]->UpdateRegion(0, 0, width, height, sources[i], true);
            }
            return;
        }

        RHI_Texture* tex_dirt_color = GetRenderTarget(Renderer_RenderTarget::car_dirt_color);
        RHI_Texture* tex_dirt_state = GetRenderTarget(Renderer_RenderTarget::car_dirt_state);
        RHI_Texture* tex_dirt_wear  = GetRenderTarget(Renderer_RenderTarget::car_dirt_wear);
        RHI_Shader* shader_dirt     = GetShader(Renderer_Shader::car_dirt_texels_c);
        const uint32_t dirt_count   = min(state.dirt_texels_staged, SurfaceWater::dirt_texels_max);
        if (dirt_count == 0 || !tex_dirt_color || !tex_dirt_state || !tex_dirt_wear || !shader_dirt || !shader_dirt->IsCompiled())
            return;

        for (RHI_Texture* texture : { tex_dirt_color, tex_dirt_state, tex_dirt_wear })
        {
            if (texture->GetWidth() != width || texture->GetHeight() != height)
                return;
        }

        RenderPass pass("car_dirt", shader_dirt);
        pass.Bind("tex_car_dirt_color_uav", tex_dirt_color);
        pass.Bind("tex_car_dirt_state_uav", tex_dirt_state);
        pass.Bind("tex_car_dirt_wear_uav",  tex_dirt_wear);
        pass.Bind("car_dirt_texels", Renderer_Buffer::CarDirtTexels);
        pass.Set(pass_car_rain::offset, m_frame_resource_index * SurfaceWater::dirt_texels_max);
        pass.Set(pass_car_rain::count, dirt_count);
        pass.Set(pass_car_rain::atlas_width, width);
        pass.Set(pass_car_rain::atlas_height, height);
        pass.Dispatch((dirt_count + 63) / 64, 1);
    }
}
