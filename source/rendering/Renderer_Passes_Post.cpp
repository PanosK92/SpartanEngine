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
#include "Renderer_Internal.h"
#include "../world/Entity.h"
#include "../world/World.h"
#include "../world/components/Camera.h"
#include "../world/components/ParticleSystem.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Queue.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_VendorTechnology.h"
#include "../xr/Xr.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    template<typename F>
    void Renderer::Pass_Compute(const char* name, Renderer_Shader shader_enum,
                                RHI_Texture* tex_in, RHI_Texture* tex_out, F setup)
    {
        RHI_CommandList::BeginPass(name);
        {
            RHI_CommandList::SetShader(GetShader(shader_enum));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);

            if constexpr (!std::is_null_pointer_v<F>)
            {
                setup();
            }

            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_ScreenSpaceAmbientOcclusion()
    {
        if (!cvar_ssao.GetValueAs<bool>())
        {
            return;
        }

        // ssao uses a single depth view, skip in stereo so the right eye is not darkened by left eye occlusion
        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            return;
        }

        RHI_Texture* tex_ssao = GetRenderTarget(Renderer_RenderTarget::ssao);
        RHI_Texture* tex_hist_0 = GetRenderTarget(Renderer_RenderTarget::ssao_history_0);
        RHI_Texture* tex_hist_1 = GetRenderTarget(Renderer_RenderTarget::ssao_history_1);
        if (!tex_ssao || !tex_hist_0 || !tex_hist_1)
        {
            return;
        }

        const uint32_t history_write = m_pass_state.ssao_history.Write();
        const uint32_t history_read  = m_pass_state.ssao_history.Read();
        RHI_Texture* tex_history_read  = history_read == 0 ? tex_hist_0 : tex_hist_1;
        RHI_Texture* tex_history_write = history_write == 0 ? tex_hist_0 : tex_hist_1;

        Renderer::BeginPass("screen_space_ambient_occlusion", rhi_all_mips, false);
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::ssao_c));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_history_read);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_ssao, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_history_write, rhi_all_mips, 0, true);
            // x above 0.5 resets temporal history, first frame or after rt recreate
            m_pcb_pass_cpu.set_f3_value(m_pass_state.ssao_history.valid ? 0.0f : 1.0f, 0.0f, 0.0f);
            RHI_CommandList::Dispatch(tex_ssao, GetResolutionScale());

            m_pass_state.ssao_history.Advance();
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_PostProcess_Color(RHI_Texture*& tex_in, RHI_Texture*& tex_out, uint32_t eye_layer)
    {
        RHI_Texture* rt_frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);

        // underwater tint and waterline meniscus, only runs when the camera is near or below the waves
        if (Camera* camera = World::GetCamera())
        {
            const Vector3 camera_position = camera->GetEntity()->GetPosition();
            float ocean_height            = 0.0f;
            if (GetOceanHeight(camera_position.x, camera_position.z, ocean_height) && camera_position.y < ocean_height + 1.0f)
            {
                Renderer::BeginPass("underwater", eye_layer);
                {
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::underwater_c));
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::skysphere));
                    RHI_CommandList::Dispatch(tex_out);
                }
                RHI_CommandList::EndPass();
                swap(tex_in, tex_out);
            }
        }

        if (cvar_depth_of_field.GetValueAs<bool>())
        {
            RHI_Texture* tex_dof_focus          = GetRenderTarget(Renderer_RenderTarget::dof_focus);
            RHI_Texture* tex_dof_focus_previous = GetRenderTarget(Renderer_RenderTarget::dof_focus_previous);
            const bool update_focus_history =
                tex_dof_focus &&
                tex_dof_focus_previous &&
                !IsSecondaryViewActive() &&
                (
                    eye_layer == rhi_all_mips ||
                    eye_layer == 0
                );

            Renderer::BeginPass("depth_of_field", eye_layer);
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::depth_of_field_c));
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
                if (tex_dof_focus_previous)
                {
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_dof_focus_previous);
                }
                if (update_focus_history)
                {
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_dof_focus, rhi_all_mips, 0, true);
                }
                // y flags whether this dispatch owns the focus history write
                m_pcb_pass_cpu.set_f3_value(
                    World::GetCamera()->GetAperture(),
                    update_focus_history ? 1.0f : 0.0f,
                    0.0f
                );
                RHI_CommandList::Dispatch(tex_out);
            }
            RHI_CommandList::EndPass();
            swap(tex_in, tex_out);

            if (update_focus_history)
            {
                RHI_CommandList::Blit(tex_dof_focus, tex_dof_focus_previous, false);
            }
        }

        if (cvar_motion_blur.GetValueAs<bool>())
        {
            Renderer::BeginPass("motion_blur", eye_layer);
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::motion_blur_c));
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
                // a secondary view never runs the cloud passes, its velocity lives in the gbuffer
                const bool use_cloud_velocity =
                    m_pass_state.cloud_history.valid && !IsSecondaryViewActive();
                RHI_CommandList::SetTexture(
                    static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_velocity),
                    GetRenderTarget(
                        use_cloud_velocity ?
                        Renderer_RenderTarget::cloud_velocity :
                        Renderer_RenderTarget::gbuffer_velocity
                    ),
                    rhi_all_mips,
                    0,
                    false,
                    eye_layer
                );
                // y above 1.5 enables the radial mask debug view
                m_pcb_pass_cpu.set_f3_value(
                    World::GetCamera()->GetShutterSpeed(),
                    cvar_motion_blur.GetValue(),
                    0.0f
                );
                RHI_CommandList::Dispatch(tex_out);
            }
            RHI_CommandList::EndPass();
            swap(tex_in, tex_out);
        }

        Camera* camera = World::GetCamera();
        const bool auto_exposure_enabled =
            camera &&
            camera->GetExposureMode() == CameraExposureMode::automatic;
        const bool meter_auto_exposure =
            auto_exposure_enabled &&
            (
                eye_layer == rhi_all_mips ||
                eye_layer == 0
            );
        RHI_Texture* tex_exposure_previous =
            GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);

        if (meter_auto_exposure)
        {
            RHI_Texture* tex_exposure = tex_in;
            if (!tex_exposure->HasPerMipViews())
            {
                tex_exposure = tex_out->HasPerMipViews() ? tex_out : rt_frame_output;
                if (tex_exposure != tex_in)
                {
                    RHI_CommandList::Blit(tex_in, tex_exposure, false);
                }
            }
            Pass_Downscale(tex_exposure, Renderer_DownsampleFilter::Average);
            Pass_AutoExposure(tex_exposure);
            m_pass_state.exposure_history_reset = false;
        }
        // a secondary view must not claim the exposure history, the primary camera would then
        // look like it changed and auto exposure would reset on every preview frame
        if (!IsSecondaryViewActive())
        {
            m_pass_state.exposure_camera          = camera;
            m_pass_state.exposure_history_texture = tex_exposure_previous;
            m_pass_state.exposure_was_automatic   = auto_exposure_enabled;
        }

        if (cvar_bloom.GetValueAs<bool>())
        {
            Pass_Bloom(tex_in, tex_out);
            swap(tex_in, tex_out);
        }

        RHI_Texture* tex_pre_tonemap = tex_in;

        // hmd swapchain is sdr srgb, desktop hdr encode (pq/scrgb) reads as washed out in the headset
        const bool xr_stereo = Xr::IsSessionRunning() && Xr::GetStereoMode();
        Pass_Tonemap(tex_in, tex_out, xr_stereo);
        swap(tex_in, tex_out);

        // vr captures after both eyes from the stereo buffer, mid eye would race the right eye overwrite
        if (!xr_stereo)
        {
            Pass_Screenshot(tex_pre_tonemap);
        }

        Pass_PostProcess_DisplayEffects(tex_in, tex_out);

        if (tex_in != rt_frame_output)
        {
            RHI_CommandList::Copy(tex_in, rt_frame_output, false);
        }
    }

    void Renderer::Pass_PostProcess_DisplayEffects(RHI_Texture*& tex_in, RHI_Texture*& tex_out, bool apply_dithering)
    {
        auto run_effect = [&](const char* name, Renderer_Shader shader, auto setup)
        {
            Pass_Compute(name, shader, tex_in, tex_out, setup);
            swap(tex_in, tex_out);
        };

        if (apply_dithering && cvar_dithering.GetValueAs<bool>())
        {
            run_effect("dithering", Renderer_Shader::dithering_c, [&]()
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetStandardTexture(Renderer_StandardTexture::Noise_blue));
            });
        }

        if (cvar_sharpness.GetValueAs<bool>())
        {
            run_effect("sharpening", Renderer_Shader::ffx_cas_c, [&]()
            {
                m_pcb_pass_cpu.set_f3_value(cvar_sharpness.GetValue(), 0.0f, 0.0f);
            });
        }

        if (cvar_film_grain.GetValueAs<bool>())
        {
            run_effect("film_grain", Renderer_Shader::film_grain_c, [&]()
            {
                m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetIso(), 0.0f, 0.0f);
            });
        }

        if (cvar_chromatic_aberration.GetValueAs<bool>())
        {
            run_effect("chromatic_aberration", Renderer_Shader::chromatic_aberration_c, [&]()
            {
                m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetAperture(), 0.0f, 0.0f);
            });
        }

        if (cvar_vhs.GetValueAs<bool>())
        {
            run_effect("vhs", Renderer_Shader::vhs_c, nullptr);
        }
    }

    void Renderer::Pass_PostProcess_EditorOverlays(RHI_Texture* tex_out)
    {
        Pass_Grid   (tex_out);
        Pass_Lines  (tex_out);
        Pass_Outline(tex_out);
        Pass_Icons  (tex_out);
    }

    void Renderer::Pass_PostProcess(uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* rt_frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_in          = rt_frame_output;
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::frame_output_2);

        RHI_CommandList::BeginMarker("post_process");
        Pass_PostProcess_Color(tex_in, tex_out, eye_layer);
        // grid, lines, icons and editor overlays stay on the desktop mirror, not in the hmd
        if (!(Xr::IsSessionRunning() && Xr::GetStereoMode()))
        {
            Pass_PostProcess_EditorOverlays(rt_frame_output);
        }
        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        RHI_Shader* shader_luminance          = GetShader(Renderer_Shader::bloom_luminance_c);
        RHI_Shader* shader_upsample_blend_mip = GetShader(Renderer_Shader::bloom_upsample_blend_mip_c);
        RHI_Shader* shader_blend_frame        = GetShader(Renderer_Shader::bloom_blend_frame_c);
        RHI_Texture* tex_bloom                = GetRenderTarget(Renderer_RenderTarget::bloom);

        // stop when dimensions drop below 32px to avoid instability
        uint32_t bloom_mip_count = 0;
        for (uint32_t i = 0; i < tex_bloom->GetMipCount(); i++)
        {
            uint32_t mip_width  = tex_bloom->GetWidth() >> i;
            uint32_t mip_height = tex_bloom->GetHeight() >> i;
            
            if (mip_width < 32 || mip_height < 32)
            {
                break;
            }
            
            bloom_mip_count++;
        }
    
        RHI_CommandList::BeginTimeblock("bloom");
    
        // luminance
        RHI_CommandList::BeginMarker("luminance");
        {
            RHI_CommandList::SetShader(shader_luminance, "bloom_luminance");
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_bloom, 0, 1, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::Dispatch(tex_bloom);
        }
        RHI_CommandList::EndMarker();
    
        if (bloom_mip_count > 1)
        {
            Pass_Downscale(tex_bloom, Renderer_DownsampleFilter::Average);
        }
    
        // upsample & blend chain
        RHI_CommandList::BeginMarker("upsample_chain");
        {
            RHI_CommandList::SetShader(shader_upsample_blend_mip, "bloom_upsample_blend_mip");

            for (int i = bloom_mip_count - 1; i > 0; i--)
            {
                int small_mip_idx = i;
                int big_mip_idx   = i - 1;
                
                uint32_t big_width  = tex_bloom->GetWidth() >> big_mip_idx;
                uint32_t big_height = tex_bloom->GetHeight() >> big_mip_idx;

                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_bloom, small_mip_idx, 1);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_bloom, big_mip_idx, 1, true);

                uint32_t thread_group_count = 8;
                uint32_t dispatch_x = (big_width + thread_group_count - 1) / thread_group_count;
                uint32_t dispatch_y = (big_height + thread_group_count - 1) / thread_group_count;
                
                RHI_CommandList::Dispatch(dispatch_x, dispatch_y);
            }
        }
        RHI_CommandList::EndMarker();
    
        // composite
        RHI_CommandList::BeginMarker("blend_with_frame");
        {
            RHI_CommandList::SetShader(shader_blend_frame, "bloom_blend_frame");
            m_pcb_pass_cpu.set_f3_value(cvar_bloom.GetValue(), 0.0f, 0.0f);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_bloom, 0, 1);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndMarker();
    
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_Tonemap(RHI_Texture* tex_in, RHI_Texture* tex_out, bool force_sdr)
    {
        RHI_Shader* shader_c = GetShader(Renderer_Shader::output_c);

        RHI_CommandList::BeginPass("tonemap");
        {
            RHI_CommandList::SetShader(shader_c);
            m_pcb_pass_cpu.set_f3_value(
                cvar_tonemapping.GetValue(),
                0.0f,
                force_sdr ? 1.0f : 0.0f
            );
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Upscaler_Reactivity()
    {
        RHI_Texture* tex_reactivity = GetRenderTarget(Renderer_RenderTarget::dlss_reactivity);
        RHI_Shader* shader = GetShader(Renderer_Shader::dlss_reactivity_c);
        if (!tex_reactivity || !shader || !shader->IsCompiled())
        {
            return;
        }

        RHI_Texture* tex_velocity = GetRenderTarget(
            m_pass_state.cloud_history.valid && !IsSecondaryViewActive() ?
            Renderer_RenderTarget::cloud_velocity :
            Renderer_RenderTarget::gbuffer_velocity
        );
        RHI_Texture* tex_depth_previous = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous);
        if (!tex_velocity || !tex_depth_previous)
        {
            return;
        }

        Renderer::BeginPass("upscaler_reactivity", rhi_all_mips);
        {
            RHI_CommandList::SetShader(shader);
            RHI_CommandList::SetTexture(
                static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_velocity),
                tex_velocity
            );
            RHI_CommandList::SetTexture(
                static_cast<uint32_t>(Renderer_BindingsSrv::tex3),
                tex_depth_previous
            );
            RHI_CommandList::SetTexture(
                static_cast<uint32_t>(Renderer_BindingsUav::tex),
                tex_reactivity,
                rhi_all_mips,
                0,
                true
            );
            m_pcb_pass_cpu.set_f3_value(std::clamp(cvar_dlss_reactivity.GetValue(), 0.0f, 1.0f), 0.0f, 0.0f);
            RHI_CommandList::Dispatch(tex_reactivity, GetResolutionScale());
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_AA_Upscale(uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* tex_in          = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_velocity    = GetRenderTarget(m_pass_state.cloud_history.valid && !IsSecondaryViewActive() ? Renderer_RenderTarget::cloud_velocity : Renderer_RenderTarget::gbuffer_velocity);
        RHI_Texture* tex_depth       = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        const float resolution_scale = Renderer::GetResolutionScale();

        RHI_CommandList::BeginTimeblock("aa_upscale");
        {
            bool is_stereo = eye_layer != rhi_all_mips;
            Renderer_AntiAliasing_Upsampling method = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();

            // a secondary view renders a single isolated frame, a temporal upscaler has no
            // history for it so it resolves as a soft upscale, and its history copy would
            // overwrite the primary camera's history and make the main viewport flicker
            if (IsSecondaryViewActive())
            {
                method = Renderer_AntiAliasing_Upsampling::AA_Off_Upscale_Linear;
            }

            // vendor upscalers keep one global temporal context, cannot run twice per frame for two eyes
            if (is_stereo &&
                (method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess ||
                 method == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss))
            {
                method = Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau;
            }

            if (!is_stereo && method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
            {
                RHI_VendorTechnology::XeSS_Dispatch(
                    tex_in,
                    tex_depth,
                    tex_velocity,
                    tex_out
                );
            }
            else if (!is_stereo && method == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss)
            {
                RHI_VendorTechnology::DLSS_Dispatch(
                    tex_in,
                    tex_depth,
                    tex_velocity,
                    tex_out,
                    GetRenderTarget(Renderer_RenderTarget::dlss_reactivity)
                );
            }
            else if (method == Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau)
            {
                RHI_Texture* tex_history = GetRenderTarget(Renderer_RenderTarget::taau_history);

                Renderer::SetPass("taau", eye_layer);
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::taau_c));
                RHI_CommandList::SetTexture(
                    static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_velocity),
                    tex_velocity,
                    rhi_all_mips,
                    0,
                    false,
                    eye_layer
                );
                m_pcb_pass_cpu.set_f3_value(m_taau_reset_history ? 1.0f : 0.0f, 0.0f, 0.0f);

                RHI_CommandList::SetTexture(
                    static_cast<uint32_t>(Renderer_BindingsSrv::tex),
                    tex_history,
                    rhi_all_mips,
                    0,
                    false,
                    eye_layer
                );
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_in);
                RHI_CommandList::SetTexture(
                    static_cast<uint32_t>(Renderer_BindingsSrv::tex3),
                    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous),
                    rhi_all_mips,
                    0,
                    false,
                    eye_layer
                );
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
                RHI_CommandList::Dispatch(tex_out);

                if (is_stereo)
                {
                    RHI_CommandList::BlitToArrayLayer(tex_out, tex_history, eye_layer);
                    if (eye_layer + 1 >= Xr::eye_count)
                    {
                        m_taau_reset_history = false;
                    }
                }
                else
                {
                    RHI_CommandList::Copy(tex_out, tex_history, false);
                    m_taau_reset_history = false;
                }
            }
            else if (method == Renderer_AntiAliasing_Upsampling::AA_Fxaa_Upscale_Linear)
            {
                Pass_Compute("fxaa", Renderer_Shader::fxaa_c, tex_in, tex_out);
                Pass_Compute("fxaa", Renderer_Shader::fxaa_c, tex_out, tex_in);
                RHI_CommandList::Blit(tex_in, tex_out, false, resolution_scale);
            }
            else
            {
                RHI_CommandList::Blit(tex_in, tex_out, false, resolution_scale);
            }

            // generate mips for refraction roughness
            Pass_Downscale(tex_out, Renderer_DownsampleFilter::Average);
        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_AutoExposure(RHI_Texture* tex_in)
    {
        RHI_Texture* tex_exposure          = GetRenderTarget(Renderer_RenderTarget::auto_exposure);
        RHI_Texture* tex_exposure_previous = GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);

        RHI_CommandList::BeginPass("auto_exposure");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::auto_exposure_c));

            Camera* camera = World::GetCamera();
            m_pcb_pass_cpu.set_f3_value(
                camera->GetAutoExposureAdaptationSpeed(),
                camera->GetAutoExposureCompensation(),
                0.0f
            );

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_exposure_previous);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_exposure, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(1, 1, 1);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Blit(RHI_Texture* tex_in, RHI_Texture* tex_out, const bool gpu_timing /*= true*/)
    {
        // compute blit: vulkan can't blit depth to float, amd uav requires float
        RHI_Shader* shader_c = GetShader(Renderer_Shader::blit_c);

        // gpu_timing false emits a marker instead of a timeblock so a caller can fold this blit
        // into its own single profiler chunk, the gpu debugger label is still kept
        if (gpu_timing)
        {
            RHI_CommandList::BeginTimeblock("blit");
        }
        else
        {
            RHI_CommandList::BeginMarker("blit");
        }
        {
            RHI_CommandList::SetShader(shader_c, "blit");
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::Dispatch(tex_out);
        }
        if (gpu_timing)
        {
            RHI_CommandList::EndTimeblock();
        }
        else
        {
            RHI_CommandList::EndMarker();
        }
    }

    void Renderer::Pass_BlitRestirFallback(RHI_Texture* tex_raw, RHI_Texture* tex_denoised)
    {
        Pass_Blit(tex_raw, tex_denoised);
    }

    void Renderer::Pass_Downscale(RHI_Texture* tex, const Renderer_DownsampleFilter filter)
    {
        // amd fidelityfx spd caps at 4096 and 12 mips per dispatch, chain passes and
        // fall back to a 2x2 reduce for any source mip that still exceeds that size
        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(tex->GetMipCount() > 1);

        constexpr uint32_t spd_max_size = 4096;
        constexpr uint32_t spd_max_mips = 12;

        Renderer_Shader shader_spd = Renderer_Shader::ffx_spd_average_c;
        shader_spd = filter == Renderer_DownsampleFilter::Min ? Renderer_Shader::ffx_spd_min_c : shader_spd;
        shader_spd = filter == Renderer_DownsampleFilter::Max ? Renderer_Shader::ffx_spd_max_c : shader_spd;

        Renderer_Shader shader_one = Renderer_Shader::ffx_spd_average_one_c;
        shader_one = filter == Renderer_DownsampleFilter::Min ? Renderer_Shader::ffx_spd_min_one_c : shader_one;
        shader_one = filter == Renderer_DownsampleFilter::Max ? Renderer_Shader::ffx_spd_max_one_c : shader_one;

        RHI_Buffer* spd_counter = (RHI_Device::GetBoundQueueType() == RHI_Queue_Type::Compute)
            ? GetBuffer(Renderer_Buffer::SpdCounterCompute)
            : GetBuffer(Renderer_Buffer::SpdCounter);

        RHI_CommandList::BeginMarker("downscale");

        uint32_t mip_start         = 0;
        const uint32_t mip_count   = tex->GetMipCount();

        while (mip_start + 1 < mip_count)
        {
            const uint32_t width  = max(tex->GetWidth()  >> mip_start, 1u);
            const uint32_t height = max(tex->GetHeight() >> mip_start, 1u);

            if (width > spd_max_size || height > spd_max_size)
            {
                const uint32_t dst_w = max(width  >> 1, 1u);
                const uint32_t dst_h = max(height >> 1, 1u);

                RHI_CommandList::SetShader(GetShader(shader_one), "downscale_one_mip");
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex, mip_start, 1);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex, mip_start + 1, 1, true);
                constexpr uint32_t thread_group = 8;
                RHI_CommandList::Dispatch(
                    (dst_w + thread_group - 1) / thread_group,
                    (dst_h + thread_group - 1) / thread_group
                );

                mip_start++;
                continue;
            }

            const uint32_t remaining        = mip_count - (mip_start + 1);
            const uint32_t output_mip_count = min(remaining, spd_max_mips);
            const uint32_t thread_group_x   = (width  + 63) >> 6;
            const uint32_t thread_group_y   = (height + 63) >> 6;

            RHI_CommandList::SetShader(GetShader(shader_spd), "downscale");

            m_pcb_pass_cpu.set_f3_value(
                static_cast<float>(output_mip_count),
                static_cast<float>(thread_group_x * thread_group_y),
                0.0f
            );
            m_pcb_pass_cpu.set_f3_value2(static_cast<float>(width), static_cast<float>(height), 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::sb_spd), spd_counter);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex, mip_start, 1);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex_spd), tex, mip_start + 1, output_mip_count, true);
            RHI_CommandList::Dispatch(thread_group_x, thread_group_y);

            mip_start += output_mip_count;
        }

        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_Blur(RHI_Texture* tex_in, const bool bilateral, const float radius, const uint32_t mip /*= rhi_all_mips*/)
    {
        RHI_Shader* shader_c = GetShader(bilateral ? Renderer_Shader::blur_gaussian_bilateral_c : Renderer_Shader::blur_gaussian_c);

        const bool mip_requested            = mip != rhi_all_mips;
        const uint32_t mip_range            = mip_requested ? 1 : 0;
        const uint32_t bit_shift            = mip_requested ? mip : 0;
        const uint32_t width                = tex_in->GetWidth()  >> bit_shift;
        const uint32_t height               = tex_in->GetHeight() >> bit_shift;
        const uint32_t thread_group_count   = 8;
        const uint32_t thread_group_count_x = (width + thread_group_count - 1) / thread_group_count;
        const uint32_t thread_group_count_y = (height + thread_group_count - 1) / thread_group_count;

        RHI_Texture* tex_blur = GetRenderTarget(Renderer_RenderTarget::blur);
        SP_ASSERT_MSG(width <= tex_blur->GetWidth() && height <= tex_blur->GetHeight(), "Input texture is larger than the blur scratch buffer");

        RHI_CommandList::BeginMarker("blur");
        RHI_CommandList::SetPass("blur");
        RHI_CommandList::SetShader(shader_c);

        // horizontal
        {
            m_pcb_pass_cpu.set_f3_value(radius, 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in, mip, mip_range);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_blur, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(thread_group_count_x, thread_group_count_y);
        }

        // vertical
        {
            m_pcb_pass_cpu.set_f3_value(radius, 1.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_blur);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_in, mip, mip_range, true);
            RHI_CommandList::Dispatch(thread_group_count_x, thread_group_count_y);
        }

        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_Particles()
    {
        // gather every active emitter, cached at world resolve
        vector<ParticleSystem*> emitters;
        emitters.reserve(World::GetEntitiesWithParticles().size());
        for (Entity* entity : World::GetEntitiesWithParticles())
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }

            if (ParticleSystem* ps = entity->GetComponent<ParticleSystem>())
            {
                emitters.push_back(ps);
            }
        }

        if (emitters.empty())
        {
            return;
        }

        RHI_Shader* shader_emit     = GetShader(Renderer_Shader::particle_emit_c);
        RHI_Shader* shader_simulate = GetShader(Renderer_Shader::particle_simulate_c);
        RHI_Shader* shader_render_v = GetShader(Renderer_Shader::particle_render_v);
        RHI_Shader* shader_render_p = GetShader(Renderer_Shader::particle_render_p);
        RHI_Shader* shader_volume_clear     = GetShader(Renderer_Shader::particle_volume_clear_c);
        RHI_Shader* shader_volume_splat     = GetShader(Renderer_Shader::particle_volume_splat_c);
        RHI_Shader* shader_volume_resolve   = GetShader(Renderer_Shader::particle_volume_resolve_c);
        RHI_Shader* shader_volume_composite = GetShader(Renderer_Shader::particle_volume_composite_c);
        if (!shader_emit || !shader_emit->IsCompiled() ||
            !shader_simulate || !shader_simulate->IsCompiled() ||
            !shader_render_v || !shader_render_v->IsCompiled() ||
            !shader_render_p || !shader_render_p->IsCompiled())
        {
            return;
        }
        bool volume_shaders_ready =
            shader_volume_clear && shader_volume_clear->IsCompiled() &&
            shader_volume_splat && shader_volume_splat->IsCompiled() &&
            shader_volume_resolve && shader_volume_resolve->IsCompiled() &&
            shader_volume_composite && shader_volume_composite->IsCompiled();

        RHI_Buffer* buf_a       = GetBuffer(Renderer_Buffer::ParticleBufferA);
        RHI_Buffer* buf_counter = GetBuffer(Renderer_Buffer::ParticleCounter);
        RHI_Buffer* buf_emitter = GetBuffer(Renderer_Buffer::ParticleEmitter);
        RHI_Buffer* buf_volume_density  = GetBuffer(Renderer_Buffer::ParticleVolumeDensity);
        RHI_Buffer* buf_volume_color    = GetBuffer(Renderer_Buffer::ParticleVolumeColor);
        RHI_Texture* tex_volume         = GetRenderTarget(Renderer_RenderTarget::particle_volume);
        RHI_Texture* tex_volume_history = GetRenderTarget(Renderer_RenderTarget::particle_volume_history);
        if (!buf_a || !buf_counter || !buf_emitter)
        {
            return;
        }
        volume_shaders_ready = volume_shaders_ready && buf_volume_density && buf_volume_color && tex_volume && tex_volume_history;

        // clamp the emitter count to the emitter buffer capacity
        uint32_t emitter_capacity = static_cast<uint32_t>(buf_emitter->GetObjectSize() / sizeof(Sb_EmitterParams));
        uint32_t emitter_count    = std::min(static_cast<uint32_t>(emitters.size()), emitter_capacity);

        // each emitter gets a stable slice of the shared buffer, avoiding cross emitter stomping
        uint32_t buffer_capacity = static_cast<uint32_t>(buf_a->GetObjectSize() / sizeof(Sb_Particle));
        uint32_t total_particles = 0;
        vector<uint32_t> range_starts(emitter_count, 0);
        vector<uint32_t> range_counts(emitter_count, 0);
        vector<uint32_t> emit_counts(emitter_count, 0);
        bool volume_present = false;
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            range_starts[i] = total_particles;
            range_counts[i] = std::min(emitters[i]->GetMaxParticles(), buffer_capacity - total_particles);
            total_particles += range_counts[i];
            if (total_particles >= buffer_capacity)
            {
                break;
            }
        }

        if (total_particles == 0)
        {
            return;
        }

        auto to_blend_state = [](ParticleBlendMode mode)
        {
            switch (mode)
            {
                case ParticleBlendMode::Alpha:
                    return Renderer_BlendState::Alpha;
                case ParticleBlendMode::Premultiplied:
                    return Renderer_BlendState::Premultiplied;
                case ParticleBlendMode::Additive:
                default:
                    return Renderer_BlendState::Additive;
            }
        };

        // a spawn hitch can hand us a multi hundred millisecond frame, every other simulation in the
        // engine clamps to 0.1 and the particle sim has to match or it integrates one huge step
        const float delta_time = std::clamp(m_cb_frame_cpu.delta_time, 0.0f, 0.1f);

        // one params entry per emitter, the ring size and frame data are shared so every entry carries the same copy
        vector<Sb_EmitterParams> emitter_params(emitter_count);
        vector<float> emitter_distance(emitter_count, 0.0f);
        math::Vector3 camera_position = math::Vector3::Zero;
        if (Camera* camera = World::GetCamera())
        {
            camera_position = camera->GetEntity()->GetPosition();
        }

        for (uint32_t i = 0; i < emitter_count; i++)
        {
            ParticleSystem* emitter = emitters[i];
            math::Vector3 position = emitter->GetEntity()->GetPosition();
            emitter->UpdateRuntime(position, delta_time);
            emit_counts[i]      = std::min(emitter->ConsumeEmissionCount(delta_time), range_counts[i]);
            emitter_distance[i] = math::Vector3::Distance(position, camera_position);

            Sb_EmitterParams& params    = emitter_params[i];
            params.position             = position;
            params.emission_rate        = emitter->GetEmissionRate();
            params.lifetime             = emitter->GetLifetime();
            params.start_speed          = emitter->GetStartSpeed();
            params.start_size           = emitter->GetStartSize();
            params.end_size             = emitter->GetEndSize();
            params.start_color          = emitter->GetStartColor();
            params.end_color            = emitter->GetEndColor();
            params.gravity_modifier     = emitter->GetGravityModifier();
            params.radius               = emitter->GetEmissionRadius();
            params.delta_time           = delta_time;
            params.max_particles        = total_particles;
            params.range_start          = range_starts[i];
            params.range_count          = range_counts[i];
            params.emit_count           = emit_counts[i];
            params.frame                = m_cb_frame_cpu.frame;
            params.emitter_count        = emitter_count;
            params.blend_mode           = static_cast<uint32_t>(emitter->GetBlendMode());
            params.lighting_mode        = static_cast<uint32_t>(emitter->GetLightingMode());
            params.render_mode          = static_cast<uint32_t>(emitter->GetRenderMode());
            params.volume_density       = emitter->GetVolumeDensity();
            params.volume_anisotropy    = emitter->GetVolumeAnisotropy();
            params.volume_shadowing     = emitter->GetVolumeShadowing();
            params.emission_direction   = emitter->GetEmissionDirection();
            params.emission_cone_angle  = emitter->GetEmissionConeAngle();
            params.directional_blend    = emitter->GetDirectionalBlend();
            params.soft_depth_scale     = emitter->GetSoftDepthScale();
            params.drag                 = emitter->GetDrag();
            params.turbulence_strength  = emitter->GetTurbulenceStrength();
            params.wind_influence       = emitter->GetWindInfluence();
            params.velocity_inheritance = emitter->GetVelocityInheritance();
            params.velocity_stretch     = emitter->GetVelocityStretch();
            params.vortex_center        = emitter->GetVortexCenter();
            params.vortex_axis          = emitter->GetVortexAxis();
            params.vortex_strength      = emitter->GetVortexStrength();
            params.vortex_radius        = emitter->GetVortexRadius();
            params.thermal_strength     = emitter->GetThermalStrength();
            params.thermal_decay        = emitter->GetThermalDecay();
            params.rollup_strength      = emitter->GetRollupStrength();
            params.wake_strength        = emitter->GetWakeStrength();
            params.churn_strength       = emitter->GetChurnStrength();
            params.collision_clearance  = emitter->GetCollisionClearance();
            params.emissive_strength    = emitter->GetEmissiveStrength();
            params.flipbook_rows        = emitter->GetFlipbookRows();
            params.flipbook_columns     = emitter->GetFlipbookColumns();
            params.flipbook_fps         = emitter->GetFlipbookFps();
            params.emitter_velocity     = emitter->GetEmitterVelocity();
        }

        // the volumetric path costs a clear, a scatter, a full grid resolve and a full resolution ray
        // march every frame, and that cost is the same whether one emitter is smoking or forty are, a
        // world full of cars hands us dozens of volumetric emitters and the pass then overruns the
        // driver watchdog, so only the closest few live emitters keep it and the rest fall back to billboards
        {
            const float volume_max_distance    = 64.0f; // must match volume_max_distance in particles_volumetric.hlsl
            const uint32_t volumetric_budget   = 8;

            vector<uint32_t> candidates;
            candidates.reserve(emitter_count);
            for (uint32_t i = 0; i < emitter_count; i++)
            {
                bool eligible =
                    range_counts[i] > 0 &&
                    emitters[i]->GetRenderMode() == ParticleRenderMode::Volumetric &&
                    emitters[i]->HasLiveParticles() &&
                    emitter_distance[i] < volume_max_distance;

                if (eligible)
                {
                    candidates.push_back(i);
                }
            }

            std::sort(candidates.begin(), candidates.end(), [&emitter_distance](uint32_t a, uint32_t b)
            {
                return emitter_distance[a] < emitter_distance[b];
            });

            if (candidates.size() > volumetric_budget)
            {
                candidates.resize(volumetric_budget);
            }

            // demote everything that did not win a slot, the splat and the render loop both key off this
            for (uint32_t i = 0; i < emitter_count; i++)
            {
                if (emitter_params[i].render_mode == static_cast<uint32_t>(ParticleRenderMode::Volumetric))
                {
                    emitter_params[i].render_mode = static_cast<uint32_t>(ParticleRenderMode::Billboard);
                }
            }

            for (uint32_t index : candidates)
            {
                emitter_params[index].render_mode = static_cast<uint32_t>(ParticleRenderMode::Volumetric);
            }

            volume_present = !candidates.empty();

            // every splatted particle scatters into a block of voxels with four atomics each, so a drifting
            // car at six hundred particles per second per wheel alone runs into the billions, splat a fixed
            // size subset and let it carry the missing density
            //
            // the depth extent is derived from the particle radius now rather than floored at a slice, so a
            // puff covers about three slices instead of eleven and a splat costs well under half what it
            // did, and the subset rotates each frame with the resolve accumulating the frames, so a larger
            // budget buys coverage that compounds instead of just costing more
            const uint32_t splat_budget = 6000;

            uint32_t live_estimate = 0;
            for (uint32_t index : candidates)
            {
                live_estimate += emitters[index]->GetEstimatedLiveParticles();
            }

            uint32_t splat_stride = 1;
            if (live_estimate > splat_budget)
            {
                splat_stride = (live_estimate + splat_budget - 1) / splat_budget;
            }

            for (uint32_t index : candidates)
            {
                emitter_params[index].volume_splat_stride = splat_stride;
            }
        }

        // the depth buffer only ever knew about the front layer of what was on screen, so a particle
        // behind geometry, off screen or behind the camera passed straight through the world, trace
        // against the acceleration structure instead whenever one is available
        RHI_AccelerationStructure* tlas_collision = nullptr;
        if (RHI_Device::IsSupportedRayTracing())
        {
            if (RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure())
            {
                if (tlas->GetRhiResource())
                {
                    tlas_collision = tlas;
                }
            }
        }

        for (uint32_t i = 0; i < emitter_count; i++)
        {
            emitter_params[i].collision_traced = tlas_collision ? 1u : 0u;
        }

        buf_emitter->ResetOffset();
        buf_emitter->Update(emitter_params.data(), sizeof(Sb_EmitterParams) * emitter_count);

        uint32_t thread_group = 256;

        RHI_CommandList::BeginTimeblock("particles");

        // emit, one dispatch per emitter so each spawns from its own position and rate
        RHI_CommandList::BeginMarker("particle_emit");
        RHI_CommandList::SetShader(shader_emit, "particle_emit");
        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_buffer_a), buf_a);
        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_counter), buf_counter);
        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_emitter), buf_emitter);
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            if (emit_counts[i] == 0 || range_counts[i] == 0)
            {
                continue;
            }

            m_pcb_pass_cpu.set_f3_value(static_cast<float>(i), 0.0f, 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::Dispatch((emit_counts[i] + thread_group - 1) / thread_group, 1, 1);
        }
        RHI_CommandList::EndMarker();

        // simulate
        RHI_CommandList::BeginMarker("particle_simulate");
        {
            RHI_CommandList::SetShader(shader_simulate, "particle_simulate");
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_buffer_a), buf_a);
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_counter), buf_counter);
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_emitter), buf_emitter);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_normal), GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
            if (tlas_collision)
            {
                RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas_collision);
            }
            RHI_CommandList::Dispatch((total_particles + thread_group - 1) / thread_group, 1, 1);
        }
        RHI_CommandList::EndMarker();

        // render, each particle becomes a camera facing quad with the emitter selected blend mode
        // one draw per emitter so each can bind its own smoke texture and only its own range is drawn
        RHI_Texture* tex_white  = GetStandardTexture(Renderer_StandardTexture::White);
        RHI_Texture* tex_render = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_CommandList::BeginMarker("particle_render");
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            if (range_counts[i] == 0)
            {
                continue;
            }
            // demoted emitters draw as billboards, only the ones that kept a volumetric slot are skipped here
            if (volume_shaders_ready && emitter_params[i].render_mode == static_cast<uint32_t>(ParticleRenderMode::Volumetric))
            {
                continue;
            }

            RHI_Texture* tex_particle = emitters[i]->GetTexture();
            bool has_texture          = tex_particle != nullptr;

            RHI_CommandList::SetPass("particle_render");
            RHI_CommandList::SetShaders(shader_render_v, shader_render_p);
            RHI_CommandList::SetBlendState(GetBlendState(to_blend_state(emitters[i]->GetBlendMode())));
            RHI_CommandList::SetColorTarget(tex_render);
            RHI_CommandList::SetResolutionScale(true);

            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_buffer_a), buf_a);
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_emitter), buf_emitter);
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_grid), GetBuffer(Renderer_Buffer::ClusterLightGrid));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_indices), GetBuffer(Renderer_Buffer::ClusterLightIndices));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), has_texture ? tex_particle : tex_white);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::shadow_atlas));

            if (RHI_Device::IsSupportedRayTracing())
            {
                if (RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure())
                {
                    if (tlas->GetRhiResource())
                    {
                        RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
                    }
                }
            }

            m_pcb_pass_cpu.set_f3_value(static_cast<float>(i), has_texture ? 1.0f : 0.0f, 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::SetCullMode(RHI_CullMode::None);
            RHI_CommandList::Draw(range_counts[i] * 6);
        }

        RHI_CommandList::EndMarker();

        if (volume_shaders_ready && volume_present)
        {
            const uint32_t voxel_count = renderer_particle_volume_width * renderer_particle_volume_height * renderer_particle_volume_depth;

            // the resolve accumulates against the previous frame's grid, so the two swap roles each frame,
            // a secondary view has no history of its own and its reprojection belongs to the primary
            const bool use_history      = m_pass_state.particle_volume_history.valid && !IsSecondaryViewActive();
            RHI_Texture* tex_volume_write = m_pass_state.particle_volume_history.SelectWrite(tex_volume, tex_volume_history);
            RHI_Texture* tex_volume_read  = m_pass_state.particle_volume_history.SelectRead(tex_volume, tex_volume_history);

            RHI_CommandList::BeginMarker("particle_volume_clear");
            {
                RHI_CommandList::SetShader(shader_volume_clear, "particle_volume_clear");
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_volume_density), buf_volume_density);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_volume_color), buf_volume_color);
                RHI_CommandList::Dispatch((voxel_count + thread_group - 1) / thread_group, 1, 1);
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("particle_volume_splat");
            {
                RHI_CommandList::SetShader(shader_volume_splat, "particle_volume_splat");
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_buffer_a), buf_a);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_emitter), buf_emitter);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_volume_density), buf_volume_density);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_volume_color), buf_volume_color);
                RHI_CommandList::Dispatch((total_particles + thread_group - 1) / thread_group, 1, 1);
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("particle_volume_resolve");
            {
                RHI_CommandList::SetShader(shader_volume_resolve, "particle_volume_resolve");
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_volume_density), buf_volume_density);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::particle_volume_color), buf_volume_color);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3d), tex_volume_write, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3d), tex_volume_read);
                m_pcb_pass_cpu.set_f3_value(use_history ? 0.0f : 1.0f, 0.0f, 0.0f);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Dispatch((renderer_particle_volume_width + 7) / 8, (renderer_particle_volume_height + 7) / 8, (renderer_particle_volume_depth + 3) / 4);
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("particle_volume_composite");
            {
                RHI_CommandList::SetShader(shader_volume_composite, "particle_volume_composite");
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_render, rhi_all_mips, 0, true);
                if (RHI_Texture* tex_reactivity = GetRenderTarget(Renderer_RenderTarget::dlss_reactivity))
                {
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_reactivity, rhi_all_mips, 0, true);
                }
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3d), tex_volume_write);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::shadow_atlas));
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_grid), GetBuffer(Renderer_Buffer::ClusterLightGrid));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_indices), GetBuffer(Renderer_Buffer::ClusterLightIndices));
                RHI_CommandList::Dispatch(tex_render);
            }
            RHI_CommandList::EndMarker();

            if (!IsSecondaryViewActive())
            {
                m_pass_state.particle_volume_history.Advance();
            }
        }

        RHI_CommandList::EndTimeblock();
    }
}
