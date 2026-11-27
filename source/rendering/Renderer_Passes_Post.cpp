/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================================
#include "pch.h"
#include "Renderer_Internal.h"
#include "../profiling/Profiler.h"
#include "../world/Entity.h"
#include "../world/World.h"
#include "../world/components/Camera.h"
#include "../world/components/ParticleSystem.h"
#include "../world/ParticleWake.h"
#include "../world/ParticleFluid.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Queue.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_VendorTechnology.h"
#include "../rhi/RHI_FrameGeneration.h"
#include "../xr/Xr.h"
#include "../../data/shaders/shared_taau.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        struct cinematic_overlay
        {
            float fade     = 0.0f;
            Vector3 color  = Vector3::Zero;
            float aspect   = 0.0f;
        } cinematic;
    }

    void Renderer::SetCinematicOverlay(float fade, const Vector3& fade_color, float letterbox_aspect)
    {
        cinematic.fade   = isfinite(fade) ? clamp(fade, 0.0f, 1.0f) : 0.0f;
        cinematic.color  = fade_color;
        cinematic.aspect = isfinite(letterbox_aspect) && letterbox_aspect >= 0.5f ? letterbox_aspect : 0.0f;
    }

    template<typename F>
    void Renderer::Pass_Compute(const char* name, Renderer_Shader shader_enum,
                                RHI_Texture* tex_in, RHI_Texture* tex_out, F setup)
    {
        RHI_CommandList::BeginPass(name);
        {
            RHI_CommandList::SetShader(GetShader(shader_enum));
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);

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
        if (IsStereoView())
        {
            return;
        }

        RHI_Texture* tex_ssao   = GetRenderTarget(Renderer_RenderTarget::ssao);
        RHI_Texture* tex_hist_0 = GetRenderTarget(Renderer_RenderTarget::ssao_history_0);
        RHI_Texture* tex_hist_1 = GetRenderTarget(Renderer_RenderTarget::ssao_history_1);
        RHI_Texture* tex_meta_0 = GetRenderTarget(Renderer_RenderTarget::ssao_history_meta_0);
        RHI_Texture* tex_meta_1 = GetRenderTarget(Renderer_RenderTarget::ssao_history_meta_1);
        RHI_Texture* tex_raw    = GetRenderTarget(Renderer_RenderTarget::ssao_raw);
        RHI_Texture* tex_packed = GetRenderTarget(Renderer_RenderTarget::ssao_packed);
        if (!tex_ssao || !tex_hist_0 || !tex_hist_1 || !tex_meta_0 || !tex_meta_1 || !tex_raw || !tex_packed)
        {
            return;
        }

        const uint32_t history_write = m_pass_state.ssao_history.Write();
        const uint32_t history_read  = m_pass_state.ssao_history.Read();
        RHI_Texture* tex_history_read  = history_read == 0 ? tex_hist_0 : tex_hist_1;
        RHI_Texture* tex_history_write = history_write == 0 ? tex_hist_0 : tex_hist_1;
        RHI_Texture* tex_meta_read     = history_read == 0 ? tex_meta_0 : tex_meta_1;
        RHI_Texture* tex_meta_write    = history_write == 0 ? tex_meta_0 : tex_meta_1;

        RHI_CommandList::BeginPass("screen_space_ambient_occlusion");
        SetCommonTextures(rhi_all_mips, false);
        {
            // cortao prepass, linear depth with a coarse normal packed into every mip in one dispatch
            const uint32_t packed_mip_count = tex_packed->GetMipCount();
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::ssao_prepass_c));
            Renderer::BindTexture(Renderer_BindingsUav::tex_spd, tex_packed, 0, packed_mip_count);
            m_pcb_pass_cpu.set(pass_ssao::mip_count, packed_mip_count);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch(tex_packed, GetResolutionScale());

            // cortao main pass, this frame's occlusion and bent normal
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::ssao_c));
            Renderer::BindTexture(Renderer_BindingsSrv::tex3, tex_packed);
            Renderer::BindTexture(Renderer_BindingsSrv::tex4, GetStandardTexture(Renderer_StandardTexture::Cortao_bent_normal_lut));
            Renderer::BindTexture(Renderer_BindingsSrv::tex3d, GetStandardTexture(Renderer_StandardTexture::Cortao_occluder_lut));
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_raw);
            RHI_CommandList::Dispatch(tex_raw, GetResolutionScale());

            // temporal accumulation into the history
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::ssao_temporal_c));
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_raw);
            Renderer::BindTexture(Renderer_BindingsSrv::tex2, tex_history_read);
            Renderer::BindTexture(Renderer_BindingsSrv::tex3, tex_meta_read);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_history_write);
            Renderer::BindTexture(Renderer_BindingsUav::tex2, tex_meta_write);
            // history resets on the first frame or after rt recreate, a secondary view has none of its own,
            // it only writes the slot the primary overwrites next, so the primary's history survives it
            const bool secondary_view = IsSecondaryViewActive();
            m_pcb_pass_cpu.set(pass_ssao::reset_history, !m_pass_state.ssao_history.valid || secondary_view);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch(tex_history_write, GetResolutionScale());

            // cross bilateral denoise of the accumulated result
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::ssao_denoise_c));
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_history_write);
            Renderer::BindTexture(Renderer_BindingsSrv::tex2, tex_meta_write);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_ssao);
            RHI_CommandList::Dispatch(tex_ssao, GetResolutionScale());

            if (!secondary_view)
            {
                m_pass_state.ssao_history.Advance();
            }
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_PostProcess_Color(RHI_Texture*& tex_in, RHI_Texture*& tex_out, uint32_t eye_layer)
    {
        RHI_Texture* rt_frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);

        // the eyes do their own focusing and head motion would smear the whole view, so the hmd gets neither
        const bool xr_stereo = IsStereoView();

        // Air/water transport and the displaced waterline are resolved before upscaling.
        if (cvar_depth_of_field.GetValueAs<bool>() && !xr_stereo)
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

            RHI_CommandList::BeginPass("depth_of_field_focus");
            SetCommonTextures(eye_layer);
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::depth_of_field_focus_c));
                Renderer::BindTexture(Renderer_BindingsSrv::tex2, tex_dof_focus_previous);
                Renderer::BindTexture(Renderer_BindingsUav::tex2, tex_dof_focus);
                RHI_CommandList::Dispatch(1, 1, 1);
            }
            RHI_CommandList::EndPass();

            if (tex_in->HasPerMipViews() && tex_in->GetMipCount() > 1)
            {
                Pass_Downscale(tex_in, Renderer_DownsampleFilter::Average);
            }

            RHI_CommandList::BeginPass("depth_of_field");
            SetCommonTextures(eye_layer);
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::depth_of_field_c));
                Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
                Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);
                Renderer::BindTexture(Renderer_BindingsSrv::tex2, tex_dof_focus);
                m_pcb_pass_cpu.set(pass_depth_of_field::aperture, Renderer::GetViewCamera()->GetAperture());
                RHI_CommandList::Dispatch(tex_out);
            }
            RHI_CommandList::EndPass();
            swap(tex_in, tex_out);

            if (update_focus_history)
            {
                RHI_CommandList::Blit(tex_dof_focus, tex_dof_focus_previous, false);
            }
        }

        if (cvar_motion_blur.GetValueAs<bool>() && !xr_stereo)
        {
            RHI_CommandList::BeginPass("motion_blur");
            SetCommonTextures(eye_layer);
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::motion_blur_c));
                Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
                Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);
                // a secondary view never runs the cloud passes, its velocity lives in the gbuffer
                const bool use_cloud_velocity = m_pass_state.cloud_history.valid && !IsSecondaryViewActive();
                Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_velocity, GetRenderTarget(use_cloud_velocity ? Renderer_RenderTarget::cloud_velocity : Renderer_RenderTarget::gbuffer_velocity), rhi_all_mips, 0, eye_layer);
                // mode 2 enables the radial mask debug view
                m_pcb_pass_cpu.set(pass_motion_blur::shutter_speed, Renderer::GetViewCamera()->GetShutterSpeed());
                m_pcb_pass_cpu.set(pass_motion_blur::mode, cvar_motion_blur.GetValue());
                RHI_CommandList::Dispatch(tex_out);
            }
            RHI_CommandList::EndPass();
            swap(tex_in, tex_out);
        }

        // the drops sit on the lens itself, the scene's focus and motion blur never reach them, the bloom does
        bool lens_ready = true;
        for (Renderer_Shader kernel : { Renderer_Shader::lens_rain_c, Renderer_Shader::lens_rain_blur_c, Renderer_Shader::lens_rain_composite_c, Renderer_Shader::lens_drops_clear_c, Renderer_Shader::lens_drops_simulate_c, Renderer_Shader::lens_drops_spawn_c, Renderer_Shader::lens_drops_insert_c, Renderer_Shader::lens_drops_target_c, Renderer_Shader::lens_drops_merge_c, Renderer_Shader::lens_drops_lights_c })
        {
            RHI_Shader* shader = GetShader(kernel);
            lens_ready         = lens_ready && shader && shader->IsCompiled();
        }
        RHI_Texture* lens_layer   = GetRenderTarget(Renderer_RenderTarget::lens_rain_layer);
        RHI_Texture* lens_scratch = GetRenderTarget(Renderer_RenderTarget::lens_rain_scratch);
        lens_ready                = lens_ready && lens_layer && lens_scratch;
        if (lens_ready && Renderer::view().frame.lens_rain.x > 0.0f && !xr_stereo && !IsSecondaryViewActive())
        {
            RHI_Buffer* lens_drops = GetBuffer(Renderer_Buffer::LensDrops);
            RHI_Buffer* lens_state = GetBuffer(Renderer_Buffer::LensState);

            // a drop sees a wide field through a tiny cap, each of its pixels gathers many of the scene's, the mips integrate them so small bright lights keep their energy
            if (!tex_in->HasPerMipViews() && tex_out->HasPerMipViews())
            {
                RHI_CommandList::Blit(tex_in, tex_out, false);
                swap(tex_in, tex_out);
            }
            if (tex_in->HasPerMipViews() && tex_in->GetMipCount() > 1)
            {
                Pass_Downscale(tex_in, Renderer_DownsampleFilter::Average);
            }

            // the step, every kernel runs over a fixed span and reads its counts on the gpu
            RHI_CommandList::BeginPass("lens_drops");
            {
                const uint32_t spawn_count = static_cast<uint32_t>(Renderer::view().frame.lens_spawn.x + Renderer::view().frame.lens_spawn.y);
                const uint32_t span        = max(LENS_DROPS_MAX, LENS_GRID_WIDTH * LENS_GRID_HEIGHT);
                const pair<Renderer_Shader, uint32_t> kernels[] =
                {
                    { Renderer_Shader::lens_drops_clear_c,    span            },
                    { Renderer_Shader::lens_drops_simulate_c, LENS_DROPS_MAX  },
                    { Renderer_Shader::lens_drops_spawn_c,    spawn_count     },
                    { Renderer_Shader::lens_drops_insert_c,   LENS_DROPS_MAX  },
                    { Renderer_Shader::lens_drops_target_c,   LENS_DROPS_MAX  },
                    { Renderer_Shader::lens_drops_merge_c,    LENS_DROPS_MAX  },
                    { Renderer_Shader::lens_drops_lights_c,   1               },
                };
                for (const auto& [kernel, threads] : kernels)
                {
                    if (threads == 0)
                        continue;

                    RHI_CommandList::SetShader(GetShader(kernel));
                    RHI_CommandList::SetBuffer("lens_drops", lens_drops);
                    RHI_CommandList::SetBuffer("lens_state", lens_state);
                    Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
                    RHI_CommandList::Dispatch((threads + 63) / 64, 1, 1);
                }
            }
            RHI_CommandList::EndPass();

            // the water as it would look in focus, then spread over the circle of confusion the way the camera sees it, the car is what it focuses on
            Pass_Compute("lens_rain", Renderer_Shader::lens_rain_c, tex_in, lens_layer, [&]()
            {
                RHI_CommandList::SetBuffer("lens_drops", lens_drops);
                RHI_CommandList::SetBuffer("lens_state", lens_state);
            });
            RHI_CommandList::BeginPass("lens_rain_defocus");
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::lens_rain_blur_c));
                for (bool vertical : { false, true })
                {
                    m_pcb_pass_cpu.set(pass_blur::vertical, vertical);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    Renderer::BindTexture(Renderer_BindingsSrv::tex, vertical ? lens_scratch : lens_layer);
                    Renderer::BindTexture(Renderer_BindingsUav::tex, vertical ? lens_layer : lens_scratch);
                    RHI_CommandList::Dispatch(lens_layer);
                }
            }
            RHI_CommandList::EndPass();
            Pass_Compute("lens_rain_composite", Renderer_Shader::lens_rain_composite_c, tex_in, tex_out, [&]()
            {
                Renderer::BindTexture(Renderer_BindingsSrv::tex2, lens_layer);
            });
            swap(tex_in, tex_out);
        }

        Camera* camera = Renderer::GetViewCamera();
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
            RHI_Texture* tex_exposure       = tex_in;
            RHI_Texture* tex_exposure_mip_0 = nullptr;
            if (!tex_exposure->HasPerMipViews())
            {
                tex_exposure = tex_out->HasPerMipViews() ? tex_out : rt_frame_output;
                if (tex_exposure != tex_in)
                {
                    // the meter reads a mip of at most 128 pixels (metering_max_dimension in auto_exposure.hlsl), so the chain is
                    // reduced straight from the frame, copying the whole frame over first is only needed when the meter reads the top mip
                    const bool same_size      = tex_in->GetWidth() == tex_exposure->GetWidth() && tex_in->GetHeight() == tex_exposure->GetHeight();
                    const bool meters_top_mip = max(tex_exposure->GetWidth(), tex_exposure->GetHeight()) <= 128 || tex_exposure->GetMipCount() < 2;
                    if (same_size && !meters_top_mip)
                    {
                        tex_exposure_mip_0 = tex_in;
                    }
                    else
                    {
                        RHI_CommandList::Blit(tex_in, tex_exposure, false);
                    }
                }
            }
            Pass_Downscale(tex_exposure, Renderer_DownsampleFilter::Average, tex_exposure_mip_0);
            Pass_AutoExposure(tex_exposure);
            Renderer::view().exposure.history_reset = false;
        }
        Renderer::view().exposure.camera          = camera;
        Renderer::view().exposure.history_texture = tex_exposure_previous;
        Renderer::view().exposure.was_automatic   = auto_exposure_enabled;

        if (cvar_bloom.GetValueAs<bool>())
        {
            Pass_Bloom(tex_in, tex_out);
            swap(tex_in, tex_out);
        }

        RHI_Texture* tex_pre_tonemap = tex_in;

        // the hmd is sdr and its srgb swapchain encodes on write, so the eyes stay linear sdr,
        // the desktop mirror and the vr screenshot encode for their own targets
        Pass_Tonemap(tex_in, tex_out, xr_stereo, xr_stereo);
        swap(tex_in, tex_out);

        // vr captures after both eyes from the stereo buffer, mid eye would race the right eye overwrite
        if (!xr_stereo)
        {
            Pass_Screenshot(tex_pre_tonemap);
        }

        Pass_PostProcess_DisplayEffects(tex_in, tex_out, true, xr_stereo);

        if (tex_in != rt_frame_output)
        {
            RHI_CommandList::Copy(tex_in, rt_frame_output, false);
        }
    }

    void Renderer::Pass_PostProcess_DisplayEffects(RHI_Texture*& tex_in, RHI_Texture*& tex_out, bool apply_dithering, bool force_sdr)
    {
        auto run_effect = [&](const char* name, Renderer_Shader shader, auto setup)
        {
            Pass_Compute(name, shader, tex_in, tex_out, setup);
            swap(tex_in, tex_out);
        };

        if (cvar_sharpness.GetValueAs<bool>())
        {
            run_effect("sharpening", Renderer_Shader::ffx_cas_c, [&]()
            {
                m_pcb_pass_cpu.set(pass_cas::sharpness, cvar_sharpness.GetValue());
            });
        }

        if (cvar_film_grain.GetValueAs<bool>())
        {
            run_effect("film_grain", Renderer_Shader::film_grain_c, [&]()
            {
                m_pcb_pass_cpu.set(pass_film_grain::iso, Renderer::GetViewCamera()->GetIso());
            });
        }

        if (cvar_chromatic_aberration.GetValueAs<bool>())
        {
            run_effect("chromatic_aberration", Renderer_Shader::chromatic_aberration_c, [&]()
            {
                m_pcb_pass_cpu.set(pass_chromatic_aberration::aperture, Renderer::GetViewCamera()->GetAperture());
            });
        }

        if (cvar_vhs.GetValueAs<bool>())
        {
            run_effect("vhs", Renderer_Shader::vhs_c, [&]()
            {
                m_pcb_pass_cpu.set(pass_vhs::force_sdr, force_sdr);
            });
        }

        // asset previews and the headset never show the sequencer's overlay
        const bool cinematic_drawn = (cinematic.fade > 0.0f || cinematic.aspect > 0.0f) && !IsSecondaryViewActive() && !IsStereoView();
        if (!IsSecondaryViewActive() && !IsStereoView())
        {
            RHI_FrameGeneration::SetLetterbox(cinematic_drawn ? cinematic.aspect : 0.0f);
        }
        if (cinematic_drawn)
        {
            run_effect("cinematic", Renderer_Shader::cinematic_c, [&]()
            {
                m_pcb_pass_cpu.set(pass_cinematic::fade, cinematic.fade);
                m_pcb_pass_cpu.set(pass_cinematic::fade_color, cinematic.color);
                m_pcb_pass_cpu.set(pass_cinematic::letterbox, cinematic.aspect);
                m_pcb_pass_cpu.set(pass_cinematic::force_sdr, force_sdr);
            });
        }

        // last, so nothing filters the noise before the swapchain rounds, and only when the frame is blitted to the swapchain
        // as is, the ui draws the frame at the viewport's size and dithers it there, per swapchain pixel
        if (apply_dithering && m_present_in_renderer && cvar_dithering.GetValueAs<bool>())
        {
            run_effect("dithering", Renderer_Shader::dithering_c, [&]()
            {
                Renderer::BindTexture(Renderer_BindingsSrv::tex2, GetStandardTexture(Renderer_StandardTexture::Noise_blue));
            });
        }
    }

    void Renderer::Pass_PostProcess_EditorOverlays(RHI_Texture* tex_out)
    {
        Pass_Grid   (tex_out);
        Pass_DebugTriangles(tex_out);
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
        // grid, lines, icons and editor overlays stay on the desktop mirror, not in the hmd or in an asset preview
        if (!(IsStereoView()) && !IsSecondaryViewActive())
        {
            RHI_FrameGeneration::CaptureScene(rt_frame_output);
            Pass_PostProcess_EditorOverlays(rt_frame_output);
        }
        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        RHI_Texture* pyramid = GetRenderTarget(Renderer_RenderTarget::bloom);
        const uint32_t mip_count = pyramid->GetMipCount();
        // Post-effect parity can leave the single-mip frame_output_2 as input.
        // It has a full SRV but no per-mip SRV; explicitly requesting mip zero
        // supplies no valid view. Both bloom shaders sample level zero.
        const uint32_t input_mip = tex_in->HasPerMipViews() ? 0 : rhi_all_mips;
        const uint32_t input_mip_range = tex_in->HasPerMipViews() ? 1 : 0;
        auto dispatch_mip = [pyramid](uint32_t mip)
        {
            const uint32_t width = max(1u, pyramid->GetWidth() >> mip);
            const uint32_t height = max(1u, pyramid->GetHeight() >> mip);
            RHI_CommandList::Dispatch((width + 7) / 8, (height + 7) / 8);
        };

        // Establish a named pass: automatic exposure ends its pass immediately
        // before bloom. A timing marker alone leaves the pending pipeline unnamed
        // and prevents SetShader from replacing the previous compute pipeline.
        RHI_CommandList::BeginPass("bloom");

        RHI_CommandList::BeginMarker("bloom_prefilter");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::bloom_prefilter_c), "bloom_prefilter");
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in, input_mip, input_mip_range);
            Renderer::BindTexture(Renderer_BindingsUav::tex, pyramid, 0, 1);
            dispatch_mip(0);
        }
        RHI_CommandList::EndMarker();

        // Every 2:1 reduction uses the overlapping bloom filter. Generic mip
        // generation is a box reduction and exposes its grid under motion.
        // Explicit, disjoint per-mip views let the RHI track each dependency.
        RHI_CommandList::BeginMarker("bloom_downsample");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::bloom_downsample_c), "bloom_downsample");
            for (uint32_t mip = 1; mip < mip_count; mip++)
            {
                Renderer::BindTexture(Renderer_BindingsSrv::tex, pyramid, mip - 1, 1);
                Renderer::BindTexture(Renderer_BindingsUav::tex, pyramid, mip, 1);
                dispatch_mip(mip);
            }
        }
        RHI_CommandList::EndMarker();

        RHI_CommandList::BeginMarker("bloom_reconstruct");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::bloom_upsample_blend_mip_c), "bloom_reconstruct");
            m_pcb_pass_cpu.set(pass_bloom::scatter, cvar_bloom_scatter.GetValue());
            for (uint32_t mip = mip_count - 1; mip > 0; mip--)
            {
                // Each thread reads/writes only its own high-resolution texel;
                // all filtered reads are from the completed, separate lower mip.
                Renderer::BindTexture(Renderer_BindingsSrv::tex, pyramid, mip, 1);
                Renderer::BindTexture(Renderer_BindingsUav::tex, pyramid, mip - 1, 1);
                dispatch_mip(mip - 1);
            }
        }
        RHI_CommandList::EndMarker();

        RHI_CommandList::BeginMarker("bloom_composite");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::bloom_blend_frame_c), "bloom_composite");
            m_pcb_pass_cpu.set(pass_bloom::intensity, cvar_bloom.GetValue());
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in, input_mip, input_mip_range);
            Renderer::BindTexture(Renderer_BindingsSrv::tex2, pyramid, 0, 1);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndMarker();
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Tonemap(RHI_Texture* tex_in, RHI_Texture* tex_out, bool force_sdr, bool output_linear)
    {
        RHI_Shader* shader_c = GetShader(Renderer_Shader::output_c);

        RHI_CommandList::BeginPass("tonemap");
        {
            RHI_CommandList::SetShader(shader_c);
            m_pcb_pass_cpu.set(pass_output::tonemapping, cvar_tonemapping.GetValueAs<uint32_t>());
            m_pcb_pass_cpu.set(pass_output::force_sdr, force_sdr);
            m_pcb_pass_cpu.set(pass_output::output_linear, output_linear);
            RHI_CommandList::SetTexture("tex_effective_exposure", GetRenderTarget(Renderer_RenderTarget::auto_exposure));
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Upscaler_Reactivity(uint32_t eye_layer)
    {
        RHI_Texture* tex_reactivity = GetRenderTarget(Renderer_RenderTarget::dlss_reactivity);
        RHI_Shader* shader = GetShader(Renderer_Shader::dlss_reactivity_c);
        if (!tex_reactivity || !shader || !shader->IsCompiled())
        {
            return;
        }

        const float reactivity_scale = std::clamp(cvar_dlss_reactivity.GetValue(), 0.0f, 1.0f);
        if (reactivity_scale <= 0.0f)
        {
            if (!m_pass_state.cleared_reactivity)
            {
                RHI_CommandList::ClearTexture(tex_reactivity, Color::standard_black);
                m_pass_state.cleared_reactivity = true;
            }
            return;
        }
        m_pass_state.cleared_reactivity = false;

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

        RHI_CommandList::BeginPass("upscaler_reactivity");
        SetCommonTextures(eye_layer);
        {
            RHI_CommandList::SetShader(shader);
            Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_velocity, tex_velocity, rhi_all_mips, 0, eye_layer);
            Renderer::BindTexture(Renderer_BindingsSrv::tex3, tex_depth_previous, rhi_all_mips, 0, eye_layer);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_reactivity);
            m_pcb_pass_cpu.set(pass_dlss_reactivity::scale, reactivity_scale);
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
        const bool is_stereo         = eye_layer != rhi_all_mips;
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
             method == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss ||
             method == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr ||
             method == Renderer_AntiAliasing_Upsampling::AA_MetalFx_Upscale_MetalFx))
        {
            method = Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau;
        }

        // the first frames after launch show while dlss is still loading in the background
        if (method == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss && !RHI_VendorTechnology::DLSS_IsReady())
        {
            method = Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau;
        }

        // the frame is radiometric, at night it sits around 1e-4, vendor upscalers round values that small into bands and
        // lose the dim end next to a lamp, so they get the frame in display range with its highlights compressed and their
        // output is brought back, taau works in float and needs neither
        RHI_Shader* shader_exposure = GetShader(Renderer_Shader::upscaler_exposure_c);
        const bool scale_exposure   = shader_exposure && shader_exposure->IsCompiled() &&
            (method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess ||
             method == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss ||
             method == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr ||
             method == Renderer_AntiAliasing_Upsampling::AA_MetalFx_Upscale_MetalFx);
        const auto pass_exposure = [&](const char* name, RHI_Texture* texture, const bool inverse, const float scale)
        {
            RHI_CommandList::BeginPass(name);
            SetCommonTextures(eye_layer);
            {
                RHI_CommandList::SetShader(shader_exposure);
                Renderer::BindTexture(Renderer_BindingsUav::tex, texture);
                m_pcb_pass_cpu.set(pass_upscaler_exposure::inverse, inverse);
                RHI_CommandList::Dispatch(texture, scale);
            }
            RHI_CommandList::EndPass();
        };
        if (scale_exposure)
        {
            pass_exposure("upscaler_exposure_in", tex_in, false, resolution_scale);
        }

        RHI_CommandList::BeginPass("aa_upscale");
        {
            SP_PROFILE_CPU_START("upscale_dispatch");

            bool fsr_dispatched = false;
            if (method == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr)
            {
                fsr_dispatched = RHI_VendorTechnology::FSR_Dispatch(tex_in, tex_depth, tex_velocity, tex_out,
                    GetRenderTarget(Renderer_RenderTarget::dlss_reactivity));
                if (!fsr_dispatched)
                {
                    method = Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau;
                    cvar_antialiasing_upsampling.SetValue(static_cast<float>(method));
                    m_taau_reset_history = true;
                }
            }

            bool metalfx_dispatched = false;
            if (method == Renderer_AntiAliasing_Upsampling::AA_MetalFx_Upscale_MetalFx)
            {
                metalfx_dispatched = RHI_VendorTechnology::MetalFx_Dispatch(tex_in, tex_depth, tex_velocity, tex_out);
                if (!metalfx_dispatched)
                {
                    method = Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau;
                    cvar_antialiasing_upsampling.SetValue(static_cast<float>(method));
                    m_taau_reset_history = true;
                }
            }

            if (fsr_dispatched || metalfx_dispatched)
            {
                // FSR or MetalFX already wrote the output above.
            }
            else if (!is_stereo && method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
            {
                RHI_VendorTechnology::XeSS_Dispatch(
                    tex_in,
                    tex_depth,
                    tex_velocity,
                    tex_out,
                    GetRenderTarget(Renderer_RenderTarget::dlss_reactivity)
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

                SetCommonTextures(eye_layer);
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::taau_c), "taau");
                Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_velocity, tex_velocity, rhi_all_mips, 0, eye_layer);
                m_pcb_pass_cpu.set(pass_taau::reset_history, m_taau_reset_history);
                m_pcb_pass_cpu.set(pass_taau::write_history, !is_stereo);
                // stereo still needs a valid descriptor although its shader branch skips this write.
                Renderer::BindTexture(Renderer_BindingsUav::tex2, GetRenderTarget(Renderer_RenderTarget::frame_output_2));
                Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_history, rhi_all_mips, 0, eye_layer);
                Renderer::BindTexture(Renderer_BindingsSrv::tex2, tex_in);
                Renderer::BindTexture(Renderer_BindingsSrv::tex4, GetRenderTarget(Renderer_RenderTarget::dlss_reactivity));
                Renderer::BindTexture(Renderer_BindingsSrv::tex3, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous), rhi_all_mips, 0, eye_layer);
                Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);
                RHI_CommandList::Dispatch(
                    (tex_out->GetWidth() + TAAU_GROUP_X - 1) / TAAU_GROUP_X,
                    (tex_out->GetHeight() + TAAU_GROUP_Y - 1) / TAAU_GROUP_Y,
                    1
                );

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
                    // protect the new history from post-processing; the old history becomes scratch.
                    auto& targets = GetRenderTargets();
                    std::swap(
                        targets[static_cast<uint32_t>(Renderer_RenderTarget::taau_history)],
                        targets[static_cast<uint32_t>(Renderer_RenderTarget::frame_output_2)]
                    );
                    m_taau_reset_history = false;
                }
            }
            else if (method == Renderer_AntiAliasing_Upsampling::AA_Fxaa_Upscale_Linear)
            {
                Pass_Compute("fxaa", Renderer_Shader::fxaa_c, tex_in, tex_out);
            }
            else
            {
                RHI_CommandList::Blit(tex_in, tex_out, false, resolution_scale);
            }

            SP_PROFILE_CPU_END();
            SP_PROFILE_CPU_START("frame_generation_prepare");
            // frame generation interpolates the presented frame, so only the primary view feeds it
            if (!is_stereo && !IsSecondaryViewActive() && RHI_FrameGeneration::IsActive())
            {
                RHI_FrameGeneration::Prepare(tex_depth, tex_velocity);
            }

            SP_PROFILE_CPU_END();
        }
        RHI_CommandList::EndPass();

        if (scale_exposure)
        {
            pass_exposure("upscaler_exposure_out", tex_out, true, 1.0f);
        }
    }

    void Renderer::Pass_AutoExposure(RHI_Texture* tex_in)
    {
        RHI_Texture* tex_exposure          = GetRenderTarget(Renderer_RenderTarget::auto_exposure);
        RHI_Texture* tex_exposure_previous = GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);

        RHI_CommandList::BeginPass("auto_exposure");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::auto_exposure_c));

            Camera* camera = Renderer::GetViewCamera();
            m_pcb_pass_cpu.set(pass_auto_exposure::adaptation_speed, camera->GetAutoExposureAdaptationSpeed());
            m_pcb_pass_cpu.set(pass_auto_exposure::exposure_compensation, camera->GetAutoExposureCompensation());

            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
            Renderer::BindTexture(Renderer_BindingsSrv::tex2, tex_exposure_previous);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_exposure);
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
            RHI_CommandList::BeginPass("blit");
        }
        else
        {
            RHI_CommandList::BeginMarker("blit");
        }
        {
            RHI_CommandList::SetShader(shader_c, "blit");
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_out);
            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in);
            RHI_CommandList::Dispatch(tex_out);
        }
        if (gpu_timing)
        {
            RHI_CommandList::EndPass();
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

    void Renderer::Pass_Downscale(RHI_Texture* tex, const Renderer_DownsampleFilter filter, RHI_Texture* tex_mip_0 /*= nullptr*/)
    {
        // amd fidelityfx spd caps at 4096 and 12 mips per dispatch, chain passes and
        // fall back to a 2x2 reduce for any source mip that still exceeds that size
        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(tex->GetMipCount() > 1);

        // tex_mip_0 stands in for the top mip, a same sized texture the chain is reduced from, the top mip of tex is left untouched
        SP_ASSERT(!tex_mip_0 || (tex_mip_0->GetWidth() == tex->GetWidth() && tex_mip_0->GetHeight() == tex->GetHeight()));
        auto set_source = [&](const uint32_t mip)
        {
            if (mip == 0 && tex_mip_0)
            {
                const bool per_mip = tex_mip_0->HasPerMipViews();
                Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_mip_0, per_mip ? 0 : rhi_all_mips, per_mip ? 1 : 0);
            }
            else
            {
                Renderer::BindTexture(Renderer_BindingsSrv::tex, tex, mip, 1);
            }
        };

        constexpr uint32_t spd_max_size = 4096;
    #if defined(API_GRAPHICS_WEBGPU)
        constexpr uint32_t spd_max_mips = 8; // webgpu stages see at most 8 storage textures, the rest go in another pass
    #else
        constexpr uint32_t spd_max_mips = 12;
    #endif

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
            const uint32_t remaining = mip_count - (mip_start + 1);

            // Conservative min/max pyramids need exact footprints on odd-sized mips.
            // Use AMD SPD for any leading prefix of strictly-even dimensions (e.g. 3840x2088 -> 1920x1044 -> 960x522 -> 480x261 in one dispatch),
            // then fall back to one-mip exact reduction for odd-sized tail mips.
            uint32_t output_mip_count = min(remaining, spd_max_mips);
            if (filter != Renderer_DownsampleFilter::Average)
            {
                uint32_t even_mips = 0;
                uint32_t w = width;
                uint32_t h = height;
                while (even_mips < output_mip_count && (w & 1u) == 0 && (h & 1u) == 0 && w >= 2 && h >= 2)
                {
                    w >>= 1;
                    h >>= 1;
                    ++even_mips;
                }
                output_mip_count = even_mips;
            }

            if (output_mip_count < (filter == Renderer_DownsampleFilter::Average ? 1u : 2u) || width > spd_max_size || height > spd_max_size)
            {
                const uint32_t dst_w = max(width  >> 1, 1u);
                const uint32_t dst_h = max(height >> 1, 1u);

                RHI_CommandList::SetShader(GetShader(shader_one), "downscale_one_mip");
                set_source(mip_start);
                Renderer::BindTexture(Renderer_BindingsUav::tex, tex, mip_start + 1, 1);
                constexpr uint32_t thread_group = 8;
                RHI_CommandList::Dispatch(
                    (dst_w + thread_group - 1) / thread_group,
                    (dst_h + thread_group - 1) / thread_group
                );

                mip_start++;
                continue;
            }

            const uint32_t thread_group_x   = (width  + 63) >> 6;
            const uint32_t thread_group_y   = (height + 63) >> 6;

            RHI_CommandList::SetShader(GetShader(shader_spd), "downscale");

            m_pcb_pass_cpu.set(pass_spd::mip_count, output_mip_count);
            m_pcb_pass_cpu.set(pass_spd::work_group_count, thread_group_x * thread_group_y);
            m_pcb_pass_cpu.set(pass_spd::resolution, Vector2(static_cast<float>(width), static_cast<float>(height)));
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            Renderer::BindBuffer(Renderer_BindingsUav::sb_spd, spd_counter);
            set_source(mip_start);
            Renderer::BindTexture(Renderer_BindingsUav::tex_spd, tex, mip_start + 1, output_mip_count);
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

        RHI_CommandList::BeginPass("blur", false);
        RHI_CommandList::SetShader(shader_c);

        // horizontal
        {
            m_pcb_pass_cpu.set(pass_blur::radius, radius);
            m_pcb_pass_cpu.set(pass_blur::vertical, false);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_in, mip, mip_range);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_blur);
            RHI_CommandList::Dispatch(thread_group_count_x, thread_group_count_y);
        }

        // vertical
        {
            m_pcb_pass_cpu.set(pass_blur::radius, radius);
            m_pcb_pass_cpu.set(pass_blur::vertical, true);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            Renderer::BindTexture(Renderer_BindingsSrv::tex, tex_blur);
            Renderer::BindTexture(Renderer_BindingsUav::tex, tex_in, mip, mip_range);
            RHI_CommandList::Dispatch(thread_group_count_x, thread_group_count_y);
        }

        RHI_CommandList::EndPass();
    }

    namespace Renderer
    {
        namespace
        {
            // the smoke fluid, see particles_fluid.hlsl
            constexpr float particle_fluid_cell_size = 0.14f; // metres, the box comes out about twenty across and under seven tall
            // odd, so the relaxation ends in the texture the projection and the next warm start read
            constexpr uint32_t particle_fluid_pressure_iterations = 6;
            // seconds without an emitter feeding it before the air is left alone, long enough for the smoke to thin out
            constexpr float particle_fluid_quiet_limit = 3.5f;
            // matches the record layout in particles_fluid.hlsl
            constexpr uint32_t particle_fluid_box_offset      = 0;
            constexpr uint32_t particle_fluid_wheel_offset    = 16;
            constexpr uint32_t particle_fluid_source_offset   = 32;
            constexpr uint32_t particle_fluid_source_capacity = 16;
            constexpr uint32_t particle_fluid_record_count    = 48;
            static_assert(particle_fluid::box_capacity * 2 <= particle_fluid_wheel_offset - particle_fluid_box_offset);
            static_assert(particle_fluid::wheel_capacity <= particle_fluid_source_offset - particle_fluid_wheel_offset);

            // one emitter as the fluid sees it
            struct ParticleFluidSource
            {
                uint64_t id = 0;
                Vector3 position;
                Vector3 velocity;
                float radius       = 0.0f;
                float density_rate = 0.0f; // density times cubic metres per second
                float heat         = 0.0f; // the lift it gives, metres per second squared
                float shade        = 1.0f;
            };

            // steps the air around the watched car by one frame, returns false while the fluid is not running
            bool step_particle_fluid(const vector<ParticleFluidSource>& sources, const float delta_time)
            {
                auto& state = m_pass_state.particle_fluid;
                const particle_fluid::Scene& scene = particle_fluid::scene;

                const Renderer_Shader kernels[] =
                {
                    Renderer_Shader::particle_fluid_advect_c,
                    Renderer_Shader::particle_fluid_vorticity_c,
                    Renderer_Shader::particle_fluid_confine_c,
                    Renderer_Shader::particle_fluid_divergence_c,
                    Renderer_Shader::particle_fluid_pressure_c,
                    Renderer_Shader::particle_fluid_project_c
                };
                bool ready = true;
                for (const Renderer_Shader kernel : kernels)
                {
                    RHI_Shader* shader = GetShader(kernel);
                    ready = ready && shader && shader->IsCompiled();
                }

                // a paused game still renders, and the cars in it still report the speed they were frozen at,
                // stepping the air then would blow the smoke along a car that is not going anywhere
                const bool frozen = delta_time <= 0.0f;

                // nothing has fed it for a while, or there is nobody to follow
                if (!frozen)
                {
                    state.quiet_time = sources.empty() ? state.quiet_time + delta_time : 0.0f;
                }
                const bool wanted = ready && scene.valid && (!sources.empty() || (state.valid && state.quiet_time < particle_fluid_quiet_limit));
                if (!wanted)
                {
                    state.valid     = false;
                    state.cell_size = 0.0f;
                    state.source_positions.clear();
                    return false;
                }

                const uint32_t flags = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit;
                const uint32_t width  = renderer_particle_fluid_width;
                const uint32_t height = renderer_particle_fluid_height;
                const uint32_t depth  = renderer_particle_fluid_depth;
                if (!state.velocity[0] || state.velocity[0]->GetWidth() != width)
                {
                    for (uint32_t i = 0; i < 2; ++i)
                    {
                        state.velocity[i] = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, width, height, depth, 1, RHI_Format::R16G16B16A16_Float, flags, i == 0 ? "particle_fluid_velocity_a" : "particle_fluid_velocity_b");
                        state.scalar[i]   = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, width, height, depth, 1, RHI_Format::R16G16B16A16_Float, flags, i == 0 ? "particle_fluid_scalar_a" : "particle_fluid_scalar_b");
                        // the pressure gradient is a difference of large neighbouring values, half floats would turn it into noise
                        state.pressure[i] = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, width, height, depth, 1, RHI_Format::R32_Float, flags, i == 0 ? "particle_fluid_pressure_a" : "particle_fluid_pressure_b");
                        RHI_CommandList::ClearTexture(state.velocity[i].get(), Color(0.0f, 0.0f, 0.0f, 0.0f));
                        RHI_CommandList::ClearTexture(state.scalar[i].get(), Color(0.0f, 0.0f, 0.0f, 0.0f));
                        RHI_CommandList::ClearTexture(state.pressure[i].get(), Color(0.0f, 0.0f, 0.0f, 0.0f));
                    }
                    state.aux   = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, width, height, depth, 1, RHI_Format::R16G16B16A16_Float, flags, "particle_fluid_aux");
                    RHI_CommandList::ClearTexture(state.aux.get(), Color(0.0f, 0.0f, 0.0f, 0.0f));
                    state.valid = false;
                }
                if (!state.scene)
                {
                    state.scene = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, sizeof(Sb_ParticleFluidRecord), particle_fluid_record_count, nullptr, false, "particle_fluid_scene");
                }

                // the air holds still and stays readable
                if (frozen)
                {
                    return state.valid;
                }

                // the box sits on the ground under the focus and trails a moving car, since behind it is where its smoke is,
                // it moves in whole cells so nothing in it has to be resampled when it does
                const float cell = particle_fluid_cell_size;
                const Vector3 size(width * cell, height * cell, depth * cell);
                Vector3 center = scene.focus;
                Vector3 travel(scene.focus_velocity.x, 0.0f, scene.focus_velocity.z);
                const float speed = travel.Length();
                if (speed > 1.0f)
                {
                    center -= travel / speed * min(speed * 0.22f, size.x * 0.24f);
                }
                // low enough to hold the ground across the whole footprint, a slope drops away from the focus
                const Vector3 normal(scene.ground_plane.x, scene.ground_plane.y, scene.ground_plane.z);
                const float slope = normal.y > 0.2f ? (fabsf(normal.x) + fabsf(normal.z)) / normal.y * size.x * 0.5f : 0.0f;
                const float floor_y = scene.focus.y - clamp(slope, 0.0f, 1.4f) - cell * 2.5f;
                const Vector3 origin(floorf((center.x - size.x * 0.5f) / cell) * cell, floorf(floor_y / cell) * cell, floorf((center.z - size.z * 0.5f) / cell) * cell);

                // a teleport leaves nothing in common with the previous box
                const bool history = state.valid && (origin - state.origin).LengthSquared() < size.x * size.x * 0.25f;

                array<Sb_ParticleFluidRecord, particle_fluid_record_count> records{};
                for (uint32_t i = 0; i < scene.box_count; ++i)
                {
                    const particle_fluid::Box& box = scene.boxes[i];
                    Sb_ParticleFluidRecord& shape  = records[particle_fluid_box_offset + i * 2];
                    Sb_ParticleFluidRecord& motion = records[particle_fluid_box_offset + i * 2 + 1];
                    shape.a  = Vector4(box.center.x, box.center.y, box.center.z, 0.0f);
                    shape.b  = Vector4(box.right.x, box.right.y, box.right.z, box.extents.x);
                    shape.c  = Vector4(box.up.x, box.up.y, box.up.z, box.extents.y);
                    shape.d  = Vector4(box.forward.x, box.forward.y, box.forward.z, box.extents.z);
                    motion.a = Vector4(box.velocity.x, box.velocity.y, box.velocity.z, 0.0f);
                    motion.b = Vector4(box.angular_velocity.x, box.angular_velocity.y, box.angular_velocity.z, 0.0f);
                    motion.c = Vector4(box.pivot.x, box.pivot.y, box.pivot.z, 0.0f);
                }
                for (uint32_t i = 0; i < scene.wheel_count; ++i)
                {
                    const particle_fluid::Wheel& wheel = scene.wheels[i];
                    Sb_ParticleFluidRecord& record = records[particle_fluid_wheel_offset + i];
                    record.a = Vector4(wheel.center.x, wheel.center.y, wheel.center.z, wheel.radius);
                    record.b = Vector4(wheel.axis.x, wheel.axis.y, wheel.axis.z, wheel.half_width);
                    record.c = Vector4(wheel.velocity.x, wheel.velocity.y, wheel.velocity.z, wheel.spin);
                }
                const uint32_t source_count = min(static_cast<uint32_t>(sources.size()), particle_fluid_source_capacity);
                unordered_map<uint64_t, Vector3> source_positions;
                for (uint32_t i = 0; i < source_count; ++i)
                {
                    const ParticleFluidSource& source = sources[i];
                    // where it fed the fluid last frame, a first sighting or a jump has no path to deposit along
                    Vector3 previous = source.position;
                    if (const auto it = state.source_positions.find(source.id); it != state.source_positions.end())
                    {
                        if ((it->second - source.position).LengthSquared() < 9.0f)
                        {
                            previous = it->second;
                        }
                    }
                    source_positions[source.id] = source.position;

                    // a kernel narrower than a cell would land in one cell or another as the emitter moves
                    const float radius = max(source.radius, cell * 1.15f);
                    Sb_ParticleFluidRecord& record = records[particle_fluid_source_offset + i];
                    record.a = Vector4(source.position.x, source.position.y, source.position.z, radius);
                    record.b = Vector4(source.velocity.x, source.velocity.y, source.velocity.z, source.density_rate);
                    record.c = Vector4(source.heat, source.shade, 18.0f, 0.0f);
                    record.d = Vector4(previous.x, previous.y, previous.z, 0.0f);
                }
                state.source_positions = move(source_positions);

                const uint32_t read  = state.current;
                const uint32_t write = 1 - read;
                const uint32_t groups_x = (width + 7) / 8;
                const uint32_t groups_y = (height + 7) / 8;
                const uint32_t groups_z = (depth + 3) / 4;

                m_pcb_pass_cpu = {};
                m_pcb_pass_cpu.set(pass_particles_fluid::origin, origin);
                m_pcb_pass_cpu.set(pass_particles_fluid::cell_size, cell);
                m_pcb_pass_cpu.set(pass_particles_fluid::origin_previous, history ? state.origin : origin);
                // long frames are stepped short, the air then runs a little slow rather than blowing up
                m_pcb_pass_cpu.set(pass_particles_fluid::delta_time, min(delta_time, 1.0f / 30.0f));
                m_pcb_pass_cpu.set(pass_particles_fluid::ground_plane, scene.ground_plane);
                m_pcb_pass_cpu.set(pass_particles_fluid::history_valid, history);
                m_pcb_pass_cpu.set(pass_particles_fluid::body_count, scene.box_count);
                m_pcb_pass_cpu.set(pass_particles_fluid::wheel_count, scene.wheel_count);
                m_pcb_pass_cpu.set(pass_particles_fluid::source_count, source_count);

                bool all_ready = true;

                RHI_CommandList::BeginPass("particle_fluid_advect");
                {
                    RHI_CommandList::UpdateBuffer(state.scene.get(), 0, sizeof(Sb_ParticleFluidRecord) * particle_fluid_record_count, records.data(), false);
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::particle_fluid_advect_c), "particle_fluid_advect");
                    RHI_CommandList::SetBuffer("particle_fluid_scene", state.scene.get());
                    RHI_CommandList::SetTexture("tex_fluid_velocity", state.velocity[read].get());
                    RHI_CommandList::SetTexture("tex_fluid_scalar", state.scalar[read].get());
                    RHI_CommandList::SetTexture("tex_fluid_velocity_uav", state.velocity[write].get());
                    RHI_CommandList::SetTexture("tex_fluid_scalar_uav", state.scalar[write].get());
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
                    all_ready = all_ready && RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();

                RHI_CommandList::BeginPass("particle_fluid_vorticity");
                {
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::particle_fluid_vorticity_c), "particle_fluid_vorticity");
                    RHI_CommandList::SetTexture("tex_fluid_velocity", state.velocity[write].get());
                    RHI_CommandList::SetTexture("tex_fluid_aux_uav", state.aux.get());
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
                    all_ready = all_ready && RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();

                RHI_CommandList::BeginPass("particle_fluid_confine");
                {
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::particle_fluid_confine_c), "particle_fluid_confine");
                    RHI_CommandList::SetTexture("tex_fluid_velocity", state.velocity[write].get());
                    RHI_CommandList::SetTexture("tex_fluid_aux", state.aux.get());
                    RHI_CommandList::SetTexture("tex_fluid_velocity_uav", state.velocity[read].get());
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
                    all_ready = all_ready && RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();

                RHI_CommandList::BeginPass("particle_fluid_divergence");
                {
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::particle_fluid_divergence_c), "particle_fluid_divergence");
                    RHI_CommandList::SetTexture("tex_fluid_velocity", state.velocity[read].get());
                    RHI_CommandList::SetTexture("tex_fluid_pressure", state.pressure[0].get());
                    RHI_CommandList::SetTexture("tex_fluid_aux_uav", state.aux.get());
                    RHI_CommandList::SetTexture("tex_fluid_pressure_uav", state.pressure[1].get());
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
                    all_ready = all_ready && RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();

                RHI_CommandList::BeginPass("particle_fluid_pressure");
                for (uint32_t i = 0; i < particle_fluid_pressure_iterations; ++i)
                {
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::particle_fluid_pressure_c), "particle_fluid_pressure");
                    RHI_CommandList::SetTexture("tex_fluid_aux", state.aux.get());
                    RHI_CommandList::SetTexture("tex_fluid_pressure", state.pressure[1 - (i & 1)].get());
                    RHI_CommandList::SetTexture("tex_fluid_pressure_uav", state.pressure[i & 1].get());
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
                    all_ready = all_ready && RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();

                RHI_CommandList::BeginPass("particle_fluid_project");
                {
                    RHI_CommandList::SetShader(GetShader(Renderer_Shader::particle_fluid_project_c), "particle_fluid_project");
                    RHI_CommandList::SetTexture("tex_fluid_velocity", state.velocity[read].get());
                    RHI_CommandList::SetTexture("tex_fluid_aux", state.aux.get());
                    RHI_CommandList::SetTexture("tex_fluid_pressure", state.pressure[0].get());
                    RHI_CommandList::SetTexture("tex_fluid_velocity_uav", state.velocity[write].get());
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
                    all_ready = all_ready && RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();

                if (!all_ready)
                {
                    // one or more solver pipelines are still compiling asynchronously, do not publish unwritten targets or mark history valid
                    state.valid     = false;
                    state.cell_size = 0.0f;
                    return false;
                }

                state.current   = write;
                state.origin    = origin;
                state.cell_size = cell;
                state.valid     = true;
                return true;
            }

            // the fluid grid for a kernel that samples it, the textures have to be bound even while it is not running
            void bind_particle_fluid()
            {
                auto& state = m_pass_state.particle_fluid;
                if (!state.velocity[0])
                {
                    // nothing has smoked yet, a single texel stands in so no kernel is left with an empty slot
                    const uint32_t flags = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit;
                    if (!state.idle)
                    {
                        state.idle = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 1, 1, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "particle_fluid_idle");
                        RHI_CommandList::ClearTexture(state.idle.get(), Color(0.0f, 0.0f, 0.0f, 0.0f));
                    }
                    RHI_CommandList::SetTexture("tex_fluid_velocity", state.idle.get());
                    RHI_CommandList::SetTexture("tex_fluid_scalar", state.idle.get());
                    return;
                }
                RHI_CommandList::SetTexture("tex_fluid_velocity", state.velocity[state.current].get());
                RHI_CommandList::SetTexture("tex_fluid_scalar", state.scalar[state.current].get());
            }
        }
    }

    namespace
    {
        struct ParticlePreparedFrame
        {
            bool simulated                  = false;
            bool has_work                   = false;
            bool volume_ready               = false;
            bool volume_resolved            = false;
            bool volume_rect_full           = false;
            uint32_t emitter_count          = 0;
            uint32_t total_particles        = 0;
            math::Vector2 volume_uv_min     = math::Vector2(1.0f, 1.0f);
            math::Vector2 volume_uv_max     = math::Vector2(0.0f, 0.0f);
            RHI_Texture* tex_volume_write   = nullptr;
            RHI_AccelerationStructure* tlas_collision = nullptr;
            vector<ParticleSystem*> emitters;
            vector<uint32_t> range_counts;
            vector<Sb_EmitterParams> emitter_params;
            vector<float> emitter_distance;
            Vector3 fluid_origin            = Vector3::Zero;
            float fluid_cell_size           = 0.0f;
        };
        ParticlePreparedFrame s_particle_frame;
    }

    void Renderer::Pass_Particles_Simulate()
    {
        s_particle_frame.simulated       = true;
        s_particle_frame.has_work        = false;
        s_particle_frame.volume_ready    = false;
        s_particle_frame.volume_resolved = false;
        s_particle_frame.tex_volume_write = nullptr;

        // gather every active emitter, cached at world resolve
        vector<ParticleSystem*>& emitters = s_particle_frame.emitters;
        emitters.clear();
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

        // Keep the small environmental slices ahead of vehicle effects. Their
        // offsets must not change when traffic adds an emitter: settled leaves
        // remain in these GPU slots for as long as the player stays nearby.
        auto environmental = [](ParticleSystem* emitter) {
            return emitter->GetRenderMode() >= ParticleRenderMode::Leaf;
        };
        std::stable_partition(emitters.begin(), emitters.end(), environmental);

        // clamp the emitter count to the emitter buffer capacity
        uint32_t emitter_capacity = static_cast<uint32_t>(buf_emitter->GetObjectSize() / sizeof(Sb_EmitterParams));
        uint32_t emitter_count    = std::min(static_cast<uint32_t>(emitters.size()), emitter_capacity);

        // each emitter gets a stable slice of the shared buffer, avoiding cross emitter stomping
        uint32_t buffer_capacity = static_cast<uint32_t>(buf_a->GetObjectSize() / sizeof(Sb_Particle));
        uint32_t total_particles = 0;
        static vector<uint32_t> range_starts;
        range_starts.assign(emitter_count, 0);
        vector<uint32_t>& range_counts = s_particle_frame.range_counts;
        range_counts.assign(emitter_count, 0);
        static vector<uint32_t> emit_counts;
        emit_counts.assign(emitter_count, 0);
        bool volume_present = false;
        uint64_t requested_particles[2] = {};
        uint32_t nonempty_emitters[2] = {};
        for (uint32_t i = 0; i < emitter_count; ++i)
        {
            const uint32_t group = environmental(emitters[i]) ? 0u : 1u;
            requested_particles[group] += emitters[i]->GetMaxParticles();
            nonempty_emitters[group] += emitters[i]->GetMaxParticles() > 0 ? 1u : 0u;
        }
        // Authored capacities can exceed the shared pool (two cars already do).
        // Divide oversubscribed capacity by requested size, not current emission:
        // idle smoke must not starve later effects, nor move slices when revs change.
        // Nature gets its authored capacity, capped at half of the
        // pool. The rest remains available for smoke, weather and vehicle debris.
        uint32_t budgets[2];
        budgets[0] = static_cast<uint32_t>(std::min<uint64_t>(requested_particles[0], buffer_capacity / 2u));
        budgets[1] = buffer_capacity - budgets[0];
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            range_starts[i] = total_particles;
            const uint32_t group = environmental(emitters[i]) ? 0u : 1u;
            const uint32_t budget = budgets[group];
            const uint32_t guaranteed = std::min(nonempty_emitters[group], budget);
            uint32_t requested = emitters[i]->GetMaxParticles();
            uint32_t allocation = requested;
            if (requested_particles[group] > budget && requested > 0)
            {
                allocation = budget >= nonempty_emitters[group] ? 1u + static_cast<uint32_t>(
                    static_cast<uint64_t>(requested - 1u) * (budget - guaranteed) /
                    std::max<uint64_t>(requested_particles[group] - nonempty_emitters[group], 1u)) : 0u;
            }
            range_counts[i] = std::min(allocation, buffer_capacity - total_particles);
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

        // a spawn hitch can hand us a multi hundred millisecond frame, every other simulation in the
        // engine clamps to 0.1 and the particle sim has to match or it integrates one huge step
        //
        // a paused game holds its particles where they are, like everything else it simulates, the air they
        // ride is frozen with the cars that stir it and smoke left running would stream away from a statue
        const bool paused      = Engine::IsFlagSet(EngineMode::Paused);
        const float delta_time = paused ? 0.0f : std::clamp(Renderer::view().frame.delta_time, 0.0f, 0.1f);

        // one params entry per emitter, the ring size and frame data are shared so every entry carries the same copy
        vector<Sb_EmitterParams>& emitter_params = s_particle_frame.emitter_params;
        emitter_params.assign(emitter_count, Sb_EmitterParams{});
        vector<float>& emitter_distance = s_particle_frame.emitter_distance;
        emitter_distance.assign(emitter_count, 0.0f);
        math::Vector3 camera_position = math::Vector3::Zero;
        if (Camera* camera = Renderer::GetViewCamera())
        {
            camera_position = camera->GetEntity()->GetPosition();
        }

        for (uint32_t i = 0; i < emitter_count; i++)
        {
            ParticleSystem* emitter = emitters[i];
            math::Vector3 position = emitter->GetEntity()->GetPosition();
            if (!paused)
            {
                emitter->UpdateRuntime(position, delta_time);
                emit_counts[i] = std::min(emitter->ConsumeEmissionCount(delta_time), range_counts[i]);
            }
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
            params.frame                = Renderer::view().frame.frame;
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
            params.ground_plane         = emitter->GetGroundPlane();
            params.rain_occluded        = emitter->GetRainOccluded() ? 1u : 0u;
            params.leaf_material        = emitter->GetLeafMaterial();
            params.leaf_instance        = emitter->GetLeafInstance();
            params.seed_visible         = emitter->GetSeedVisible() ? 1u : 0u;
            // decided by what the effect was authored as, a plume demoted to billboards below still rides the fluid
            params.fluid_tracer         = emitter->GetRenderMode() == ParticleRenderMode::Volumetric ? 1u : 0u;
            const uint64_t owner_id     = emitter->GetEntity()->GetObjectId();
            params.owner_id_low         = static_cast<uint32_t>(owner_id);
            params.owner_id_high        = static_cast<uint32_t>(owner_id >> 32u);
            static_assert(sizeof(Sb_EmitterParams) % 16 == 0);
        }

        // what the volumetric emitters pour into the smoke fluid this frame
        vector<ParticleFluidSource> fluid_sources;

        // Project active volumetric emitters (and active fluid box) to a tight screen-space UV rect
        // so particle_volume_composite skips empty sky/road tiles instead of raymarching the full screen.
        math::Vector2 volume_uv_min(1.0f, 1.0f);
        math::Vector2 volume_uv_max(0.0f, 0.0f);
        bool volume_rect_full = IsSecondaryViewActive();
        auto expand_volume_screen_rect = [&](const math::BoundingBox& box, Camera* cam)
        {
            if (volume_rect_full || !cam)
            {
                volume_rect_full = true;
                return;
            }
            const math::Matrix& vp = cam->GetViewProjectionMatrix();
            const math::Vector3 bmin = box.GetMin();
            const math::Vector3 bmax = box.GetMax();
            for (uint32_t c = 0; c < 8u; ++c)
            {
                const math::Vector3 p(
                    (c & 1u) ? bmax.x : bmin.x,
                    (c & 2u) ? bmax.y : bmin.y,
                    (c & 4u) ? bmax.z : bmin.z
                );
                const math::Vector4 clip = math::Vector4(p.x, p.y, p.z, 1.0f) * vp;
                if (clip.w <= 1e-3f)
                {
                    // Box crosses the camera near plane: conservatively composite full screen.
                    volume_rect_full = true;
                    return;
                }
                const float inv_w = 1.0f / clip.w;
                const float u = clip.x * inv_w * 0.5f + 0.5f;
                const float v = 0.5f - clip.y * inv_w * 0.5f;
                volume_uv_min.x = std::min(volume_uv_min.x, u);
                volume_uv_min.y = std::min(volume_uv_min.y, v);
                volume_uv_max.x = std::max(volume_uv_max.x, u);
                volume_uv_max.y = std::max(volume_uv_max.y, v);
            }
        };

        // the volumetric path costs a clear, a scatter, a full grid resolve and a full resolution ray
        // march every frame, and that cost is the same whether one emitter is smoking or forty are, a
        // world full of cars hands us dozens of volumetric emitters and the pass then overruns the
        // driver watchdog, so only the closest few live emitters keep it and the rest fall back to billboards
        {
            const float volume_max_distance    = 64.0f; // must match volume_max_distance in particles_volumetric.hlsl
            const uint32_t volumetric_budget   = 8;
            Camera* view_camera                = Renderer::GetViewCamera();

            vector<uint32_t> candidates;
            candidates.reserve(emitter_count);
            for (uint32_t i = 0; i < emitter_count; i++)
            {
                bool eligible =
                    range_counts[i] > 0 &&
                    emitters[i]->GetRenderMode() == ParticleRenderMode::Volumetric &&
                    emitters[i]->HasLiveParticles() &&
                    emitter_distance[i] < volume_max_distance;

                if (eligible && view_camera)
                {
                    // Conservative plume bounding box so volumetric emitters behind or outside the frustum
                    // do not pay for full-screen clear/splat/resolve/raymarch passes when no smoke is visible.
                    const float plume_radius = max(4.0f, emitters[i]->GetEmissionRadius() * 2.0f + emitters[i]->GetEndSize() * 3.0f + emitters[i]->GetVortexRadius() * 2.5f);
                    const Vector3 extents(plume_radius, plume_radius, plume_radius);
                    const math::BoundingBox plume_box(emitter_params[i].position - extents, emitter_params[i].position + extents);
                    eligible = view_camera->IsInViewFrustum(plume_box);
                }

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
                const float plume_radius = max(4.0f, emitters[index]->GetEmissionRadius() * 2.0f + emitters[index]->GetEndSize() * 3.0f + emitters[index]->GetVortexRadius() * 2.5f);
                const Vector3 extents(plume_radius, plume_radius, plume_radius);
                expand_volume_screen_rect(math::BoundingBox(emitter_params[index].position - extents, emitter_params[index].position + extents), view_camera);
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

            // the same emitters feed the fluid, as much smoke per second as their particles carry between them
            for (uint32_t index : candidates)
            {
                const Sb_EmitterParams& params = emitter_params[index];
                if (emit_counts[index] == 0 || delta_time <= 0.0f)
                {
                    continue;
                }

                // what one parcel holds averaged over its life, in the units the volume splat deposits, the
                // peak density of a splat times the volume of its kernel, a parcel that entrains air grows
                // and thins on the way and the powers below are that ramp integrated
                const float alpha = max(params.start_color.a, 0.0f) * max(params.volume_density, 0.0f);
                float parcel      = alpha * 0.69f * powf(max(params.start_size, 0.0f) * 0.5f, 3.0f);
                if (params.end_size > params.start_size && params.start_size > 0.0f)
                {
                    const float size_mean = params.start_size + (params.end_size - params.start_size) * 0.6f;
                    parcel = alpha * 0.69f * 0.75f * powf(params.start_size, 0.65f) * powf(size_mean, 2.35f) * 0.125f;
                }

                // smoke comes out of the wedge behind the contact patch, not from under the middle of the tire,
                // and a cell above the road, where there is air to put it in
                const Vector3 direction(params.emission_direction.x, 0.0f, params.emission_direction.z);
                ParticleFluidSource source;
                source.id           = emitters[index]->GetEntity()->GetObjectId();
                source.position     = params.position + direction * (params.vortex_radius * 0.45f) + Vector3(0.0f, particle_fluid_cell_size * 0.6f, 0.0f);
                source.velocity     = params.emission_direction * params.start_speed + params.emitter_velocity * params.velocity_inheritance;
                source.radius       = params.radius;
                source.density_rate = static_cast<float>(emit_counts[index]) / delta_time * parcel;
                // Skip spinning up the 3D Navier-Stokes fluid grid for faint exhaust wisps;
                // active tire smoke, burnouts, and dense plumes easily clear this threshold.
                if (source.density_rate < 0.08f)
                {
                    continue;
                }
                source.heat         = max(params.thermal_strength, 0.0f);
                source.shade        = clamp(params.start_color.r * 0.2126f + params.start_color.g * 0.7152f + params.start_color.b * 0.0722f, 0.0f, 1.0f);
                fluid_sources.push_back(source);
            }
        }

        // the depth buffer only ever knew about the front layer of what was on screen, so a particle
        // behind geometry, off screen or behind the camera passed straight through the world, trace
        // against the acceleration structure instead whenever one is available
        RHI_AccelerationStructure* tlas_collision = nullptr;
        if (RHI_Device::Caps().ray_tracing)
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
            // Persistent leaves need off-screen surface queries. Keep the existing
            // depth collision path for smoke rather than adding six rays per puff.
            const auto mode = emitters[i]->GetRenderMode();
            emitter_params[i].collision_traced = tlas_collision && (mode == ParticleRenderMode::Leaf || mode == ParticleRenderMode::LeafLitter) ? 1u : 0u;
        }

        buf_emitter->ResetOffset();
        buf_emitter->Update(emitter_params.data(), sizeof(Sb_EmitterParams) * emitter_count);

        RHI_Buffer* buf_wake = GetBuffer(Renderer_Buffer::ParticleWake);
        static_assert(sizeof(particle_wake::Source) == sizeof(Sb_ParticleWake));
        buf_wake->ResetOffset();
        buf_wake->Update(particle_wake::sources.data(), sizeof(Sb_ParticleWake) * particle_wake::capacity);

        uint32_t thread_group = 256;

        RHI_CommandList::BeginPass("particles");

        // the air moves first, so the particles ride this frame's flow and the march reads this frame's smoke
        const bool fluid_running = step_particle_fluid(fluid_sources, delta_time);
        const auto& fluid        = m_pass_state.particle_fluid;
        m_pcb_pass_cpu = {};
        m_pcb_pass_cpu.set(pass_particles_fluid::origin, fluid_running ? fluid.origin : Vector3::Zero);
        m_pcb_pass_cpu.set(pass_particles_fluid::cell_size, fluid_running ? fluid.cell_size : 0.0f);
        // smoke left standing in the fluid is still there to draw after the last particle is gone
        bool fluid_visible = fluid_running;
        if (fluid_visible)
        {
            if (Camera* view_camera = Renderer::GetViewCamera())
            {
                const Vector3 fluid_size(
                    static_cast<float>(renderer_particle_fluid_width)  * fluid.cell_size,
                    static_cast<float>(renderer_particle_fluid_height) * fluid.cell_size,
                    static_cast<float>(renderer_particle_fluid_depth)  * fluid.cell_size
                );
                const math::BoundingBox fluid_box(fluid.origin, fluid.origin + fluid_size);
                fluid_visible = view_camera->IsInViewFrustum(fluid_box);
                if (fluid_visible)
                {
                    expand_volume_screen_rect(fluid_box, view_camera);
                }
            }
            else
            {
                volume_rect_full = true;
            }
        }
        volume_present = volume_present || fluid_visible;

        // emit, one dispatch per emitter so each spawns from its own position and rate
        constexpr float billboard_cull_distance = 85.0f;
        RHI_CommandList::BeginPass("particle_emit");
        RHI_CommandList::SetShader(shader_emit, "particle_emit");
        Renderer::BindBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
        Renderer::BindBuffer(Renderer_BindingsUav::particle_counter, buf_counter);
        Renderer::BindBuffer(Renderer_BindingsUav::particle_emitter, buf_emitter);
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            if (emit_counts[i] == 0 || range_counts[i] == 0 || emitter_distance[i] > billboard_cull_distance)
            {
                continue;
            }

            m_pcb_pass_cpu.set(pass_particles::emitter_index, i);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::Dispatch((emit_counts[i] + thread_group - 1) / thread_group, 1, 1);
        }
        RHI_CommandList::EndPass();

        // simulate
        RHI_CommandList::BeginPass("particle_simulate");
        {
            RHI_CommandList::SetShader(shader_simulate, "particle_simulate");
            Renderer::BindBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
            Renderer::BindBuffer(Renderer_BindingsUav::particle_counter, buf_counter);
            Renderer::BindBuffer(Renderer_BindingsUav::particle_emitter, buf_emitter);
            Renderer::BindBuffer(Renderer_BindingsUav::particle_wake, buf_wake);
            Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_normal, GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
            if (tlas_collision)
            {
                Renderer::BindAccelerationStructure(Renderer_BindingsSrv::tlas, tlas_collision);
                Renderer::BindBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));
            }
            bind_particle_fluid();
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch((total_particles + thread_group - 1) / thread_group, 1, 1);
        }
        RHI_CommandList::EndPass();

        bool resolve_ran = false;
        RHI_Texture* tex_volume_write = nullptr;
        if (volume_shaders_ready && volume_present)
        {
            const uint32_t voxel_count = renderer_particle_volume_width * renderer_particle_volume_height * renderer_particle_volume_depth;

            // the resolve accumulates against the previous frame's grid, so the two swap roles each frame,
            // a secondary view has no history of its own and its reprojection belongs to the primary
            const bool use_history = m_pass_state.particle_volume_history.valid && !IsSecondaryViewActive();
            tex_volume_write = m_pass_state.particle_volume_history.SelectWrite(tex_volume, tex_volume_history);
            RHI_Texture* tex_volume_read = m_pass_state.particle_volume_history.SelectRead(tex_volume, tex_volume_history);

            bool clear_ran = false;
            RHI_CommandList::BeginPass("particle_volume_clear");
            {
                RHI_CommandList::SetShader(shader_volume_clear, "particle_volume_clear");
                Renderer::BindBuffer(Renderer_BindingsUav::particle_volume_density, buf_volume_density);
                Renderer::BindBuffer(Renderer_BindingsUav::particle_volume_color, buf_volume_color);
                RHI_CommandList::Dispatch((voxel_count + thread_group - 1) / thread_group, 1, 1);
                clear_ran = RHI_CommandList::IsPipelineReady();
            }
            RHI_CommandList::EndPass();

            bool splat_ran = false;
            RHI_CommandList::BeginPass("particle_volume_splat");
            {
                RHI_CommandList::SetShader(shader_volume_splat, "particle_volume_splat");
                Renderer::BindBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
                Renderer::BindBuffer(Renderer_BindingsUav::particle_emitter, buf_emitter);
                Renderer::BindBuffer(Renderer_BindingsUav::particle_volume_density, buf_volume_density);
                Renderer::BindBuffer(Renderer_BindingsUav::particle_volume_color, buf_volume_color);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Dispatch((total_particles + thread_group - 1) / thread_group, 1, 1);
                splat_ran = RHI_CommandList::IsPipelineReady();
            }
            RHI_CommandList::EndPass();

            if (clear_ran && splat_ran)
            {
                RHI_CommandList::BeginPass("particle_volume_resolve");
                {
                    RHI_CommandList::SetShader(shader_volume_resolve, "particle_volume_resolve");
                    Renderer::BindBuffer(Renderer_BindingsUav::particle_volume_density, buf_volume_density);
                    Renderer::BindBuffer(Renderer_BindingsUav::particle_volume_color, buf_volume_color);
                    Renderer::BindTexture(Renderer_BindingsUav::tex3d, tex_volume_write);
                    Renderer::BindTexture(Renderer_BindingsSrv::tex3d, tex_volume_read);
                    m_pcb_pass_cpu.set(pass_particles_volumetric::reset_history, !use_history);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch((renderer_particle_volume_width + 7) / 8, (renderer_particle_volume_height + 7) / 8, (renderer_particle_volume_depth + 3) / 4);
                    resolve_ran = RHI_CommandList::IsPipelineReady();
                }
                RHI_CommandList::EndPass();
            }
        }
        RHI_CommandList::EndPass();

        s_particle_frame.has_work         = true;
        s_particle_frame.volume_ready     = volume_shaders_ready;
        s_particle_frame.volume_resolved  = resolve_ran;
        s_particle_frame.volume_rect_full = volume_rect_full;
        s_particle_frame.emitter_count    = emitter_count;
        s_particle_frame.total_particles  = total_particles;
        s_particle_frame.volume_uv_min    = volume_uv_min;
        s_particle_frame.volume_uv_max    = volume_uv_max;
        s_particle_frame.tex_volume_write = tex_volume_write;
        s_particle_frame.tlas_collision   = tlas_collision;
        s_particle_frame.fluid_origin     = fluid_running ? fluid.origin : Vector3::Zero;
        s_particle_frame.fluid_cell_size  = fluid_running ? fluid.cell_size : 0.0f;
    }

    void Renderer::Pass_Particles_Render()
    {
        if (!s_particle_frame.simulated)
        {
            Pass_Particles_Simulate();
        }
        s_particle_frame.simulated = false;
        if (!s_particle_frame.has_work)
        {
            return;
        }

        RHI_Shader* shader_render_v         = GetShader(Renderer_Shader::particle_render_v);
        RHI_Shader* shader_render_p         = GetShader(Renderer_Shader::particle_render_p);
        RHI_Shader* shader_volume_composite = GetShader(Renderer_Shader::particle_volume_composite_c);
        RHI_Buffer* buf_a                   = GetBuffer(Renderer_Buffer::ParticleBufferA);
        RHI_Buffer* buf_emitter             = GetBuffer(Renderer_Buffer::ParticleEmitter);
        if (!shader_render_v || !shader_render_p || !buf_a || !buf_emitter)
        {
            return;
        }

        auto to_blend_mode = [](ParticleBlendMode mode)
        {
            switch (mode)
            {
                case ParticleBlendMode::Alpha:
                    return RHI_BlendMode::Alpha;
                case ParticleBlendMode::Premultiplied:
                    return RHI_BlendMode::Premultiplied;
                case ParticleBlendMode::Additive:
                default:
                    return RHI_BlendMode::Additive;
            }
        };

        const uint32_t emitter_count                     = s_particle_frame.emitter_count;
        const auto& emitters                             = s_particle_frame.emitters;
        const auto& range_counts                         = s_particle_frame.range_counts;
        const auto& emitter_params                       = s_particle_frame.emitter_params;
        const auto& emitter_distance                     = s_particle_frame.emitter_distance;
        const bool volume_shaders_ready                  = s_particle_frame.volume_ready;
        RHI_AccelerationStructure* tlas_collision        = s_particle_frame.tlas_collision;
        constexpr float billboard_cull_distance          = 85.0f;

        m_pcb_pass_cpu = {};
        m_pcb_pass_cpu.set(pass_particles_fluid::origin, s_particle_frame.fluid_origin);
        m_pcb_pass_cpu.set(pass_particles_fluid::cell_size, s_particle_frame.fluid_cell_size);

        // render, each particle becomes a camera facing quad with the emitter selected blend mode
        // one draw per emitter so each can bind its own smoke texture and only its own range is drawn
        RHI_Texture* tex_white  = GetStandardTexture(Renderer_StandardTexture::White);
        RHI_Texture* tex_render = GetRenderTarget(Renderer_RenderTarget::frame_render);
        Camera* billboard_cam   = Renderer::GetViewCamera();
        RHI_CommandList::BeginPass("particle_render");
        {
            bool state_bound                    = false;
            RHI_BlendMode last_blend            = RHI_BlendMode::Max;
            RHI_Texture* last_particle_texture  = nullptr;

            for (uint32_t i = 0; i < emitter_count; i++)
            {
                // Traffic cars retain their smoke emitters even when idle. Keep
                // their buffer slices stable, but don't draw thousands of dead quads.
                if (range_counts[i] == 0 || !emitters[i]->HasLiveParticles() || emitter_distance[i] > billboard_cull_distance)
                {
                    continue;
                }
                // demoted emitters draw as billboards, only the ones that kept a volumetric slot are skipped here
                if (volume_shaders_ready && emitter_params[i].render_mode == static_cast<uint32_t>(ParticleRenderMode::Volumetric))
                {
                    continue;
                }
                if (billboard_cam)
                {
                    const float margin = max(20.0f, emitters[i]->GetEmissionRadius() * 2.0f + emitters[i]->GetEndSize() * 4.0f + emitters[i]->GetStartSpeed() * emitters[i]->GetLifetime());
                    const Vector3 ext(margin, margin, margin);
                    if (!billboard_cam->IsInViewFrustum(math::BoundingBox(emitter_params[i].position - ext, emitter_params[i].position + ext)))
                    {
                        continue;
                    }
                }

                RHI_Texture* tex_particle   = emitters[i]->GetTexture();
                bool has_texture            = tex_particle != nullptr;
                RHI_Texture* bound_texture  = has_texture ? tex_particle : tex_white;
                RHI_BlendMode blend = to_blend_mode(emitters[i]->GetBlendMode());

                if (!state_bound)
                {
                    RHI_CommandList::SetShaders(shader_render_v, shader_render_p);
                    RHI_CommandList::SetColorTargets(tex_render,
                        GetRenderTarget(m_pass_state.cloud_history.valid && !IsSecondaryViewActive() ? Renderer_RenderTarget::cloud_velocity : Renderer_RenderTarget::gbuffer_velocity),
                        GetRenderTarget(Renderer_RenderTarget::dlss_reactivity));
                    m_pass_state.cleared_reactivity = false;
                    RHI_CommandList::SetResolutionScale(true);

                    RHI_CommandList::SetTexture("tex_fog_extinction", GetRenderTarget(Renderer_RenderTarget::fog_extinction));
                    RHI_CommandList::SetTexture("tex_fog_air_source", GetRenderTarget(m_pass_state.fog_source));
                    RHI_CommandList::SetTexture("tex_fog_water_source", GetRenderTarget(m_pass_state.fog_water_source));
                    RHI_CommandList::SetTexture("tex_fog_scattering", GetRenderTarget(Renderer_RenderTarget::fog_integrated));
                    RHI_CommandList::SetTexture("tex_fog_transmittance", GetRenderTarget(Renderer_RenderTarget::fog_transmittance));
                    Renderer::BindBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
                    Renderer::BindBuffer(Renderer_BindingsUav::particle_emitter, buf_emitter);
                    Renderer::BindBuffer(Renderer_BindingsUav::cluster_light_grid, GetBuffer(Renderer_Buffer::ClusterLightGrid));
                    Renderer::BindBuffer(Renderer_BindingsUav::cluster_light_indices, GetBuffer(Renderer_Buffer::ClusterLightIndices));
                    Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
                    Renderer::BindTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::shadow_atlas));

                    if (tlas_collision)
                    {
                        Renderer::BindAccelerationStructure(Renderer_BindingsSrv::tlas, tlas_collision);
                        Renderer::BindBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));
                    }
                    RHI_CommandList::SetCullMode(RHI_CullMode::None);
                    state_bound = true;
                }

                if (blend != last_blend)
                {
                    RHI_CommandList::SetBlend(blend);
                    last_blend = blend;
                }
                if (bound_texture != last_particle_texture)
                {
                    Renderer::BindTexture(Renderer_BindingsSrv::tex, bound_texture);
                    last_particle_texture = bound_texture;
                }

                m_pcb_pass_cpu.set(pass_particles::emitter_index, i);
                m_pcb_pass_cpu.set(pass_particles::use_texture, has_texture);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                RHI_CommandList::Draw(range_counts[i] * 6);
            }
        }
        RHI_CommandList::EndPass();

        if (volume_shaders_ready && s_particle_frame.volume_resolved && s_particle_frame.tex_volume_write && shader_volume_composite)
        {
            RHI_CommandList::BeginPass("particle_volume_composite");
            {
                RHI_CommandList::SetShader(shader_volume_composite, "particle_volume_composite");
                RHI_CommandList::SetTexture("tex_fog_extinction", GetRenderTarget(Renderer_RenderTarget::fog_extinction));
                RHI_CommandList::SetTexture("tex_fog_air_source", GetRenderTarget(m_pass_state.fog_source));
                RHI_CommandList::SetTexture("tex_fog_water_source", GetRenderTarget(m_pass_state.fog_water_source));
                RHI_CommandList::SetTexture("tex_fog_scattering", GetRenderTarget(Renderer_RenderTarget::fog_integrated));
                RHI_CommandList::SetTexture("tex_fog_transmittance", GetRenderTarget(Renderer_RenderTarget::fog_transmittance));
                Renderer::BindTexture(Renderer_BindingsUav::tex, tex_render);
                if (RHI_Texture* tex_reactivity = GetRenderTarget(Renderer_RenderTarget::dlss_reactivity))
                {
                    Renderer::BindTexture(Renderer_BindingsUav::tex2, tex_reactivity);
                    m_pass_state.cleared_reactivity = false;
                }
                Renderer::BindTexture(Renderer_BindingsSrv::tex3d, s_particle_frame.tex_volume_write);
                Renderer::BindTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::shadow_atlas));
                Renderer::BindTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
                Renderer::BindBuffer(Renderer_BindingsUav::cluster_light_grid, GetBuffer(Renderer_Buffer::ClusterLightGrid));
                Renderer::BindBuffer(Renderer_BindingsUav::cluster_light_indices, GetBuffer(Renderer_Buffer::ClusterLightIndices));
                bind_particle_fluid();
                const uint32_t width  = GetScaledDimension(tex_render->GetWidth(), GetResolutionScale());
                const uint32_t height = GetScaledDimension(tex_render->GetHeight(), GetResolutionScale());
                const uint32_t total_tiles_x = (width + 15u) / 16u;
                const uint32_t total_tiles_y = (height + 15u) / 16u;
                uint32_t min_tile_x = 0u;
                uint32_t min_tile_y = 0u;
                uint32_t tile_count_x = total_tiles_x;
                uint32_t tile_count_y = total_tiles_y;
                if (!s_particle_frame.volume_rect_full && s_particle_frame.volume_uv_max.x > s_particle_frame.volume_uv_min.x && s_particle_frame.volume_uv_max.y > s_particle_frame.volume_uv_min.y)
                {
                    const float u0 = std::clamp(s_particle_frame.volume_uv_min.x, 0.0f, 1.0f);
                    const float v0 = std::clamp(s_particle_frame.volume_uv_min.y, 0.0f, 1.0f);
                    const float u1 = std::clamp(s_particle_frame.volume_uv_max.x, 0.0f, 1.0f);
                    const float v1 = std::clamp(s_particle_frame.volume_uv_max.y, 0.0f, 1.0f);
                    min_tile_x = static_cast<uint32_t>(u0 * static_cast<float>(width)) / 16u;
                    min_tile_y = static_cast<uint32_t>(v0 * static_cast<float>(height)) / 16u;
                    const uint32_t max_tile_x = std::min(total_tiles_x, (static_cast<uint32_t>(std::ceil(u1 * static_cast<float>(width))) + 15u) / 16u + 1u);
                    const uint32_t max_tile_y = std::min(total_tiles_y, (static_cast<uint32_t>(std::ceil(v1 * static_cast<float>(height))) + 15u) / 16u + 1u);
                    tile_count_x = max_tile_x > min_tile_x ? max_tile_x - min_tile_x : 0u;
                    tile_count_y = max_tile_y > min_tile_y ? max_tile_y - min_tile_y : 0u;
                }
                if (tile_count_x > 0u && tile_count_y > 0u)
                {
                    m_pcb_pass_cpu.set(pass_particles_volumetric::tile_offset_x, min_tile_x);
                    m_pcb_pass_cpu.set(pass_particles_volumetric::tile_offset_y, min_tile_y);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::Dispatch(tile_count_x, tile_count_y);
                }
            }
            RHI_CommandList::EndPass();

            if (!IsSecondaryViewActive())
            {
                m_pass_state.particle_volume_history.Advance();
            }
        }
        else if (!IsSecondaryViewActive())
        {
            m_pass_state.particle_volume_history.Reset();
        }
    }

    void Renderer::Pass_Particles()
    {
        Pass_Particles_Simulate();
        Pass_Particles_Render();
    }
}
