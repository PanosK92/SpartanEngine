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
#include "../World/Components/ParticleSystem.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_VendorTechnology.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    template<typename F>
    void Renderer::Pass_Compute(RHI_CommandList* cmd_list, const char* name, Renderer_Shader shader_enum,
                                RHI_Texture* tex_in, RHI_Texture* tex_out, F setup)
    {
        cmd_list->BeginTimeblock(name);
        {
            RHI_PipelineState pso;
            pso.name             = name;
            pso.shaders[Compute] = GetShader(shader_enum);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);

            if constexpr (!std::is_null_pointer_v<F>)
            {
                setup();
            }

            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ScreenSpaceAmbientOcclusion(RHI_CommandList* cmd_list)
    {
        if (!cvar_ssao.GetValueAs<bool>())
        {
            return;
        }

        RHI_Texture* tex_ssao = GetRenderTarget(Renderer_RenderTarget::ssao);
        if (!tex_ssao)
        {
            return;
        }

        cmd_list->BeginTimeblock("screen_space_ambient_occlusion");
        {
            RHI_PipelineState pso;
            pso.name             = "screen_space_ambient_occlusion";
            pso.shaders[Compute] = GetShader(Renderer_Shader::ssao_c);
            cmd_list->SetPipelineState(pso);

            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_ssao);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->Dispatch(tex_ssao, GetResolutionScale());
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_PostProcess_Color(RHI_CommandList* cmd_list, RHI_Texture*& tex_in, RHI_Texture*& tex_out, uint32_t eye_layer)
    {
        RHI_Texture* rt_frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);

        auto run_effect = [&](const char* name, Renderer_Shader shader, auto setup)
        {
            Pass_Compute(cmd_list, name, shader, tex_in, tex_out, setup);
            swap(tex_in, tex_out);
        };

        if (cvar_depth_of_field.GetValueAs<bool>())
        {
            run_effect("depth_of_field", Renderer_Shader::depth_of_field_c, [&]()
            {
                SetCommonTextures(cmd_list, eye_layer);
                m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetAperture(), 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
            });
        }

        if (cvar_motion_blur.GetValueAs<bool>())
        {
            run_effect("motion_blur", Renderer_Shader::motion_blur_c, [&]()
            {
                SetCommonTextures(cmd_list, eye_layer);
                m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetShutterSpeed(), 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
            });
        }

        const bool auto_exposure_enabled = cvar_auto_exposure_adaptation_speed.GetValue() > 0.0f;
        if (auto_exposure_enabled)
        {
            RHI_Texture* tex_exposure = tex_in;
            if (!tex_exposure->HasPerMipViews())
            {
                tex_exposure = tex_out->HasPerMipViews() ? tex_out : rt_frame_output;
                if (tex_exposure != tex_in)
                {
                    cmd_list->Blit(tex_in, tex_exposure, false);
                }
            }
            Pass_Downscale(cmd_list, tex_exposure, Renderer_DownsampleFilter::Average);
            Pass_AutoExposure(cmd_list, tex_exposure);
        }

        if (cvar_bloom.GetValueAs<bool>())
        {
            Pass_Bloom(cmd_list, tex_in, tex_out);
            swap(tex_in, tex_out);
        }

        Pass_Tonemap(cmd_list, tex_in, tex_out);
        swap(tex_in, tex_out);

        if (auto_exposure_enabled)
        {
            cmd_list->Blit(GetRenderTarget(Renderer_RenderTarget::auto_exposure), GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous), false);
        }

        if (cvar_dithering.GetValueAs<bool>())
        {
            run_effect("dithering", Renderer_Shader::dithering_c, [&]()
            {
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetStandardTexture(Renderer_StandardTexture::Noise_blue));
            });
        }

        if (cvar_sharpness.GetValueAs<bool>())
        {
            run_effect("sharpening", Renderer_Shader::ffx_cas_c, [&]()
            {
                m_pcb_pass_cpu.set_f3_value(cvar_sharpness.GetValue(), 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
            });
        }

        if (cvar_film_grain.GetValueAs<bool>())
        {
            run_effect("film_grain", Renderer_Shader::film_grain_c, [&]()
            {
                m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetIso(), 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
            });
        }

        if (cvar_chromatic_aberration.GetValueAs<bool>())
        {
            run_effect("chromatic_aberration", Renderer_Shader::chromatic_aberration_c, [&]()
            {
                m_pcb_pass_cpu.set_f3_value(World::GetCamera()->GetAperture(), 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);
            });
        }

        if (cvar_vhs.GetValueAs<bool>())
        {
            run_effect("vhs", Renderer_Shader::vhs_c, nullptr);
        }

        if (tex_in != rt_frame_output)
        {
            cmd_list->Copy(tex_in, rt_frame_output, false);
        }
    }

    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* rt_frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_in          = rt_frame_output;
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::frame_output_2);

        cmd_list->BeginMarker("post_process");
        Pass_PostProcess_Color(cmd_list, tex_in, tex_out, eye_layer);

        // editor overlays
        Pass_Grid   (cmd_list, rt_frame_output);
        Pass_Lines  (cmd_list, rt_frame_output);
        Pass_Outline(cmd_list, rt_frame_output);
        Pass_Icons  (cmd_list, rt_frame_output);
        cmd_list->EndMarker();
    }

    void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        RHI_Shader* shader_luminance          = GetShader(Renderer_Shader::bloom_luminance_c);
        RHI_Shader* shader_downsample         = GetShader(Renderer_Shader::bloom_downsample_c);
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
    
        cmd_list->BeginTimeblock("bloom");
    
        // luminance
        cmd_list->BeginMarker("luminance");
        {
            RHI_PipelineState pso;
            pso.name             = "bloom_luminance";
            pso.shaders[Compute] = shader_luminance;
            cmd_list->SetPipelineState(pso);
    
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom, 0, 1);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->Dispatch(tex_bloom);
        }
        cmd_list->EndMarker();
    
        // downsample chain
        cmd_list->BeginMarker("downsample_chain");
        {
            RHI_PipelineState pso;
            pso.name             = "bloom_downsample";
            pso.shaders[Compute] = shader_downsample;
            cmd_list->SetPipelineState(pso);
    
            for (uint32_t i = 0; i < bloom_mip_count - 1; i++)
            {
                RHI_Texture* input_mip = tex_bloom;
                int input_mip_idx      = i;
                int output_mip_idx     = i + 1;
                
                uint32_t output_width  = tex_bloom->GetWidth() >> output_mip_idx;
                uint32_t output_height = tex_bloom->GetHeight() >> output_mip_idx;

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, input_mip, input_mip_idx, 1);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, input_mip, output_mip_idx, 1);

                uint32_t thread_group_count = 8;
                uint32_t dispatch_x = (output_width + thread_group_count - 1) / thread_group_count;
                uint32_t dispatch_y = (output_height + thread_group_count - 1) / thread_group_count;
                
                cmd_list->Dispatch(dispatch_x, dispatch_y);
                
                cmd_list->InsertBarrier(tex_bloom, RHI_BarrierType::EnsureWriteThenRead);
            }
        }
        cmd_list->EndMarker();
    
        // upsample & blend chain
        cmd_list->BeginMarker("upsample_chain");
        {
            RHI_PipelineState pso;
            pso.name             = "bloom_upsample_blend_mip";
            pso.shaders[Compute] = shader_upsample_blend_mip;
            cmd_list->SetPipelineState(pso);

            for (int i = bloom_mip_count - 1; i > 0; i--)
            {
                int small_mip_idx = i;
                int big_mip_idx   = i - 1;
                
                uint32_t big_width  = tex_bloom->GetWidth() >> big_mip_idx;
                uint32_t big_height = tex_bloom->GetHeight() >> big_mip_idx;

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_bloom, small_mip_idx, 1);
                cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_bloom, big_mip_idx, 1);

                uint32_t thread_group_count = 8;
                uint32_t dispatch_x = (big_width + thread_group_count - 1) / thread_group_count;
                uint32_t dispatch_y = (big_height + thread_group_count - 1) / thread_group_count;
                
                cmd_list->Dispatch(dispatch_x, dispatch_y);
                
                 cmd_list->InsertBarrier(tex_bloom, RHI_BarrierType::EnsureWriteThenRead);
            }
        }
        cmd_list->EndMarker();
    
        // composite
        cmd_list->BeginMarker("blend_with_frame");
        {
            RHI_PipelineState pso;
            pso.name             = "bloom_blend_frame";
            pso.shaders[Compute] = shader_blend_frame;
            cmd_list->SetPipelineState(pso);

            m_pcb_pass_cpu.set_f3_value(cvar_bloom.GetValue(), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_bloom, 0, 1);
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndMarker();
    
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Tonemap(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        RHI_Shader* shader_c = GetShader(Renderer_Shader::output_c);

        cmd_list->BeginTimeblock("tonemap");

        RHI_PipelineState pso;
        pso.name             = "tonemap";
        pso.shaders[Compute] = shader_c;
        cmd_list->SetPipelineState(pso);

        const bool auto_exposure_enabled = cvar_auto_exposure_adaptation_speed.GetValue() > 0.0f;
        m_pcb_pass_cpu.set_f3_value(cvar_tonemapping.GetValue(), auto_exposure_enabled ? 1.0f : 0.0f);
        cmd_list->PushConstants(m_pcb_pass_cpu);

        cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
        cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous));
        cmd_list->Dispatch(tex_out);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_AA_Upscale(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* tex_in          = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_out         = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_velocity    = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        RHI_Texture* tex_depth       = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        const float resolution_scale = Renderer::GetResolutionScale();

        cmd_list->BeginTimeblock("aa_upscale");
        {
            cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureReadThenWrite);
            cmd_list->FlushBarriers();

            bool is_stereo = eye_layer != rhi_all_mips;
            Renderer_AntiAliasing_Upsampling method = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();

            // taau/xess don't support array textures, fall back to fxaa or blit in stereo
            if (!is_stereo && method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
            {
                RHI_VendorTechnology::XeSS_Dispatch(
                    cmd_list,
                    tex_in,
                    tex_depth,
                    tex_velocity,
                    tex_out
                );
            }
            else if (!is_stereo && method == Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau)
            {
                RHI_Texture* tex_history = GetRenderTarget(Renderer_RenderTarget::taau_history);

                RHI_PipelineState pso;
                pso.name             = "taau";
                pso.shaders[Compute] = GetShader(Renderer_Shader::taau_c);
                cmd_list->SetPipelineState(pso);

                m_pcb_pass_cpu.set_f3_value(m_taau_reset_history ? 1.0f : 0.0f, 0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                cmd_list->SetTexture(Renderer_BindingsSrv::tex,  tex_history);
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_in);
                cmd_list->SetTexture(Renderer_BindingsUav::tex,  tex_out);
                cmd_list->Dispatch(tex_out);

                cmd_list->InsertBarrier(tex_out,     RHI_BarrierType::EnsureWriteThenRead);
                cmd_list->InsertBarrier(tex_history, RHI_BarrierType::EnsureReadThenWrite);
                cmd_list->FlushBarriers();
                cmd_list->Copy(tex_out, tex_history, false);

                m_taau_reset_history = false;
            }
            else if (method == Renderer_AntiAliasing_Upsampling::AA_Fxaa_Upscale_Linear)
            {
                Pass_Compute(cmd_list, "fxaa", Renderer_Shader::fxaa_c, tex_in, tex_out);
                Pass_Compute(cmd_list, "fxaa", Renderer_Shader::fxaa_c, tex_out, tex_in);
                cmd_list->Blit(tex_in, tex_out, false, resolution_scale);
            }
            else
            {
                cmd_list->Blit(tex_in, tex_out, false, resolution_scale);
            }

            cmd_list->InsertBarrier(tex_out, RHI_BarrierType::EnsureWriteThenRead);

            // generate mips for refraction roughness
            Pass_Downscale(cmd_list, tex_out, Renderer_DownsampleFilter::Average);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_AutoExposure(RHI_CommandList* cmd_list, RHI_Texture* tex_in)
    {
        RHI_Texture* tex_exposure          = GetRenderTarget(Renderer_RenderTarget::auto_exposure);
        RHI_Texture* tex_exposure_previous = GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);

        RHI_PipelineState pso;
        pso.name             = "auto_exposure";
        pso.shaders[Compute] = GetShader(Renderer_Shader::auto_exposure_c);
    
        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);

            m_pcb_pass_cpu.set_f3_value(cvar_auto_exposure_adaptation_speed.GetValue());
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex2, tex_exposure_previous);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_exposure);
            cmd_list->Dispatch(1, 1, 1);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Blit(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // compute blit: vulkan can't blit depth to float, amd uav requires float
        RHI_Shader* shader_c = GetShader(Renderer_Shader::blit_c);

        cmd_list->BeginTimeblock("blit");
        {
            RHI_PipelineState pso;
            pso.name             = "blit";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_out);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_BlitRestirFallback(RHI_CommandList* cmd_list, RHI_Texture* tex_raw, RHI_Texture* tex_denoised, RHI_Texture* tex_history, bool clear_history_to_black)
    {
        if (clear_history_to_black)
        {
            cmd_list->ClearTexture(tex_history, Color::standard_black);
        }
        cmd_list->InsertBarrier(tex_denoised, RHI_BarrierType::EnsureReadThenWrite);
        if (!clear_history_to_black)
        {
            cmd_list->InsertBarrier(tex_history, RHI_BarrierType::EnsureReadThenWrite);
        }
        Pass_Blit(cmd_list, tex_raw, tex_denoised);
        if (!clear_history_to_black)
        {
            Pass_Blit(cmd_list, tex_raw, tex_history);
        }
        cmd_list->InsertBarrier(tex_denoised, RHI_BarrierType::EnsureWriteThenRead);
        if (!clear_history_to_black)
        {
            cmd_list->InsertBarrier(tex_history, RHI_BarrierType::EnsureWriteThenRead);
        }
    }

    void Renderer::Pass_Downscale(RHI_CommandList* cmd_list, RHI_Texture* tex, const Renderer_DownsampleFilter filter)
    {
        // amd fidelityfx spd, single pass downsampler, dispatch math from spd integration guide
        const uint32_t mip_start             = 0;
        const uint32_t output_mip_count      = tex->GetMipCount() - (mip_start + 1);
        const uint32_t width                 = tex->GetWidth();
        const uint32_t height                = tex->GetHeight() >> mip_start;
        const uint32_t thread_group_count_x_ = (width  + 63) >> 6;
        const uint32_t thread_group_count_y_ = (height + 63) >> 6;

        SP_ASSERT(tex->HasPerMipViews());
        SP_ASSERT(width <= 4096 && height <= 4096 && output_mip_count <= 12);
        SP_ASSERT(mip_start < output_mip_count);

        Renderer_Shader shader = Renderer_Shader::ffx_spd_average_c;
        shader                 = filter == Renderer_DownsampleFilter::Min ? Renderer_Shader::ffx_spd_min_c: shader;
        shader                 = filter == Renderer_DownsampleFilter::Max ? Renderer_Shader::ffx_spd_max_c: shader;
        RHI_Shader* shader_c   = GetShader(shader);

        cmd_list->BeginMarker("downscale");
        {
            cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::SpdCounter));

            RHI_PipelineState pso;
            pso.name             = "downscale";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            m_pcb_pass_cpu.set_f3_value(static_cast<float>(output_mip_count), static_cast<float>(thread_group_count_x_ * thread_group_count_y_), 0.0f);
            m_pcb_pass_cpu.set_f3_value2(static_cast<float>(tex->GetWidth()), static_cast<float>(tex->GetHeight()), 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->SetBuffer(Renderer_BindingsUav::sb_spd,   GetBuffer(Renderer_Buffer::SpdCounter));
            cmd_list->SetTexture(Renderer_BindingsSrv::tex,     tex, mip_start, 1);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_spd, tex, mip_start + 1, output_mip_count);
            cmd_list->Dispatch(thread_group_count_x_, thread_group_count_y_);
            cmd_list->InsertBarrier(tex, RHI_BarrierType::EnsureWriteThenRead);
        }
        cmd_list->EndMarker();
    }

    void Renderer::Pass_Blur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool bilateral, const float radius, const uint32_t mip /*= rhi_all_mips*/)
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

        cmd_list->BeginMarker("blur");

        RHI_PipelineState pso;
        pso.name             = "blur";
        pso.shaders[Compute] = shader_c;
        cmd_list->SetPipelineState(pso);

        // horizontal
        {
            m_pcb_pass_cpu.set_f3_value(radius, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            SetCommonTextures(cmd_list);
            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in, mip, mip_range);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_blur);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y);
        }

        // vertical
        {
            m_pcb_pass_cpu.set_f3_value(radius, 1.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_blur);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, tex_in, mip, mip_range);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_Particles(RHI_CommandList* cmd_list)
    {
        // gather every active emitter in the world, all of them share one ring buffer
        vector<ParticleSystem*> emitters;
        for (Entity* entity : World::GetEntities())
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
        RHI_Shader* shader_render   = GetShader(Renderer_Shader::particle_render_c);
        if (!shader_emit || !shader_emit->IsCompiled() ||
            !shader_simulate || !shader_simulate->IsCompiled() ||
            !shader_render || !shader_render->IsCompiled())
        {
            return;
        }

        RHI_Buffer* buf_a       = GetBuffer(Renderer_Buffer::ParticleBufferA);
        RHI_Buffer* buf_counter = GetBuffer(Renderer_Buffer::ParticleCounter);
        RHI_Buffer* buf_emitter = GetBuffer(Renderer_Buffer::ParticleEmitter);
        if (!buf_a || !buf_counter || !buf_emitter)
        {
            return;
        }

        // clamp the emitter count to the emitter buffer capacity
        uint32_t emitter_capacity = static_cast<uint32_t>(buf_emitter->GetObjectSize() / sizeof(Sb_EmitterParams));
        uint32_t emitter_count    = std::min(static_cast<uint32_t>(emitters.size()), emitter_capacity);

        // the ring buffer is shared, each particle records its emitter so simulate applies the right look
        uint32_t buffer_capacity = static_cast<uint32_t>(buf_a->GetObjectSize() / sizeof(Sb_Particle));
        uint32_t total_particles = 0;
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            total_particles += emitters[i]->GetMaxParticles();
        }
        total_particles = std::min(total_particles, buffer_capacity);

        // one params entry per emitter, the ring size and frame data are shared so every entry carries the same copy
        vector<Sb_EmitterParams> emitter_params(emitter_count);
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            ParticleSystem* emitter = emitters[i];

            Sb_EmitterParams& params = emitter_params[i];
            params.position         = emitter->GetEntity()->GetPosition();
            params.emission_rate    = emitter->GetEmissionRate();
            params.lifetime         = emitter->GetLifetime();
            params.start_speed      = emitter->GetStartSpeed();
            params.start_size       = emitter->GetStartSize();
            params.end_size         = emitter->GetEndSize();
            params.start_color      = emitter->GetStartColor();
            params.end_color        = emitter->GetEndColor();
            params.gravity_modifier = emitter->GetGravityModifier();
            params.radius           = emitter->GetEmissionRadius();
            params.delta_time       = m_cb_frame_cpu.delta_time;
            params.max_particles    = total_particles;
            params.frame            = m_cb_frame_cpu.frame;
            params.emitter_count    = emitter_count;
        }

        buf_emitter->ResetOffset();
        buf_emitter->Update(cmd_list, emitter_params.data(), sizeof(Sb_EmitterParams) * emitter_count);

        uint32_t thread_group = 256;

        cmd_list->BeginTimeblock("particles");

        // emit, one dispatch per emitter so each spawns from its own position and rate
        for (uint32_t i = 0; i < emitter_count; i++)
        {
            uint32_t emit_count = static_cast<uint32_t>(emitters[i]->GetEmissionRate() * m_cb_frame_cpu.delta_time);
            if (emit_count == 0)
            {
                continue;
            }

            RHI_PipelineState pso;
            pso.name             = "particle_emit";
            pso.shaders[Compute] = shader_emit;

            cmd_list->SetPipelineState(pso);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_counter,  buf_counter);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_emitter,  buf_emitter);

            m_pcb_pass_cpu.set_f3_value(static_cast<float>(i), 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->Dispatch((emit_count + thread_group - 1) / thread_group, 1, 1);
        }

        // barrier: emit -> simulate
        cmd_list->InsertBarrier(buf_a);
        cmd_list->InsertBarrier(buf_counter);

        // simulate
        {
            RHI_PipelineState pso;
            pso.name             = "particle_simulate";
            pso.shaders[Compute] = shader_simulate;

            cmd_list->SetPipelineState(pso);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_counter,  buf_counter);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_emitter,  buf_emitter);
            cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth,  GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_normal, GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
            cmd_list->Dispatch((total_particles + thread_group - 1) / thread_group, 1, 1);
        }

        // barrier: simulate -> render
        cmd_list->InsertBarrier(buf_a);

        // render
        {
            RHI_PipelineState pso;
            pso.name             = "particle_render";
            pso.shaders[Compute] = shader_render;

            cmd_list->SetPipelineState(pso);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_buffer_a, buf_a);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_counter,  buf_counter);
            cmd_list->SetBuffer(Renderer_BindingsUav::particle_emitter,  buf_emitter);
            cmd_list->SetTexture(Renderer_BindingsUav::tex, GetRenderTarget(Renderer_RenderTarget::frame_render));
            cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            cmd_list->Dispatch((total_particles + thread_group - 1) / thread_group, 1, 1);
        }

        cmd_list->EndTimeblock();
    }
}
