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
#include "../world/components/Light.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_VendorTechnology.h"
#include "../core/Window.h"
#include "../xr/Xr.h"
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
    // restir gi composition gain, pairs with get_restir_w_clamp in restir_reservoir.hlsl
    // the reservoir estimate is already the diffuse indirect radiance, so an unbiased composition
    // is exactly one, anything above that was compensating for the gi clamp cutting real sky
    // bounce energy and it scales every leak by the same factor
    static const float restir_composition_intensity = 1.0f;

    void Renderer::Pass_Reflections_Apply(uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* tex_frame             = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_reflections       = GetRenderTarget(Renderer_RenderTarget::reflections);
        RHI_Texture* tex_refraction_source = GetRenderTarget(Renderer_RenderTarget::frame_render_opaque);

        Renderer::BeginPass("reflections_apply", eye_layer);
        {
            bool use_ray_traced =
                cvar_ray_traced_reflections.GetValueAs<bool>() &&
                !IsSecondaryViewActive();

            if (!m_pass_state.cleared_reflections && !use_ray_traced)
            {
                RHI_CommandList::ClearTexture(tex_reflections, Color::standard_transparent);
                m_pass_state.cleared_reflections = true;
            }

            RHI_CommandList::BeginMarker("apply");
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::reflections_apply_c));
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_reflections);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_refraction_source);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output));
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_frame, rhi_all_mips, 0, true);
                RHI_CommandList::Dispatch(tex_frame);
            }
            RHI_CommandList::EndMarker();
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Reflections_Trace(uint32_t eye_layer /*= rhi_all_mips*/)
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
        // rt reflections owns primary specular below roughness 0.9, rougher pixels skip the trace
        // a secondary view is excluded, it is not in the tlas and its denoiser history belongs to the primary
        const bool rt_reflections_active =
            cvar_ray_traced_reflections.GetValueAs<bool>() &&
            !IsSecondaryViewActive();
        if (!rt_reflections_active || !tex_reflections_position)
        {
            if (!m_pass_state.cleared_rt_reflections)
            {
                RHI_CommandList::ClearTexture(tex_reflections, Color::standard_black);
                m_pass_state.cleared_rt_reflections = true;
            }
            return;
        }
        m_pass_state.cleared_rt_reflections = false;

        // same gate as rt shadows, a blas written this list is not safe to closest-hit
        if (m_pass_state.skip_rt_trace)
        {
            return;
        }

        RHI_CommandList::BeginTimeblock("reflections_trace");
        {
            RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
            if (!tlas || !tlas->GetRhiResource())
            {
                RHI_CommandList::ClearTexture(tex_reflections, Color(1.0f, 1.0f, 0.0f, 1.0f));
                RHI_CommandList::EndTimeblock();
                return;
            }

            Renderer::SetPass("reflections_trace", eye_layer, false);
            RHI_CommandList::SetShaders(
                GetShader(Renderer_Shader::reflections_ray_generation_r),
                GetShader(Renderer_Shader::reflections_ray_miss_r),
                GetShader(Renderer_Shader::reflections_ray_hit_r)
            );
            RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
            
            RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), tex_skysphere);
            
            // reset and bind the per-hit geometry info ring as uav
            GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::geometry_info), GetBuffer(Renderer_Buffer::GeometryInfo));

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex),  tex_reflections_position, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_reflections_normal,   rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), tex_reflections_albedo,   rhi_all_mips, 0, true);

            uint32_t width  = tex_reflections_position->GetWidth();
            uint32_t height = tex_reflections_position->GetHeight();
            RHI_CommandList::TraceRays(width, height);
        }
        RHI_CommandList::EndTimeblock();
    }
    
    void Renderer::Pass_Reflections_Shade(uint32_t eye_layer /*= rhi_all_mips*/)
    {
        // restir pt is diffuse only at the primary, so the two never double count specular
        if (
            !cvar_ray_traced_reflections.GetValueAs<bool>() ||
            IsSecondaryViewActive() ||
            m_pass_state.skip_rt_trace
        )
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

        RHI_CommandList::BeginTimeblock("reflections_shade");
        {
            Renderer::SetPass("reflections_shade", eye_layer);
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::reflections_shade_c));
            
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_reflections_position);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_reflections_normal);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), tex_reflections_albedo);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), tex_skysphere);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_reflections, rhi_all_mips, 0, true);

            // bind tlas for inline ray traced shadows at the hit, every light type uses this
            // path so reflections darken correctly inside enclosed or shadowed geometry
            if (RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure())
            {
                if (tlas->GetRhiResource())
                {
                    RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
                }
            }
            
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(m_count_active_lights), static_cast<float>(tex_skysphere->GetMipCount()));
            RHI_CommandList::Dispatch(tex_reflections);
        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_Reflections_Denoise(uint32_t eye_layer /*= rhi_all_mips*/)
    {
        if (
            Window::IsMinimized() ||
            !cvar_ray_traced_reflections.GetValueAs<bool>() ||
            IsSecondaryViewActive()
        )
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

        RHI_CommandList::BeginTimeblock("reflections_denoise");
        {
            RHI_CommandList::BeginMarker("nrd_pack");
            {
                Renderer::SetPass("nrd_pack_reflections", eye_layer);
                RHI_CommandList::SetShader(shader_pack);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_reflections);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_mv, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_normal, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), tex_view_z, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex4), tex_in, rhi_all_mips, 0, true);
                RHI_CommandList::Dispatch(tex_reflections);
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("nrd_dispatch");
            {
                if (!RHI_VendorTechnology::NRD_Dispatch(Nrd_Preset::Reflections, tex_mv, tex_normal, tex_view_z, tex_in, tex_out))
                {
                    RHI_CommandList::EndMarker();
                    RHI_CommandList::EndTimeblock();
                    return;
                }
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("nrd_unpack");
            {
                RHI_CommandList::SetShader(shader_unpack, "nrd_unpack_reflections");
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_out);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_reflections, rhi_all_mips, 0, true);
                RHI_CommandList::Dispatch(tex_reflections);
            }
            RHI_CommandList::EndMarker();
        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_RayTracedShadows()
    {
        const uint32_t min_rt_dimension = 64;
        if (Window::IsMinimized())
        {
            return;
        }

        // a secondary view is not in the tlas so it has nothing to trace against, it falls back
        // to the shadow atlas, returning before the clear keeps the primary's mask and denoiser
        // history intact
        if (IsSecondaryViewActive())
        {
            return;
        }

        RHI_Texture* tex_shadows       = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows);
        RHI_Texture* tex_shadows_local = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows_local);

        if (tex_shadows && (tex_shadows->GetWidth() < min_rt_dimension || tex_shadows->GetHeight() < min_rt_dimension))
        {
            return;
        }
        if (!cvar_ray_traced_shadows.GetValueAs<bool>())
        {
            if (!m_pass_state.cleared_rt_shadows)
            {
                RHI_CommandList::ClearTexture(tex_shadows, Color::standard_white);
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
        if (!tlas || m_pass_state.skip_rt_trace)
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
        RHI_CommandList::BeginTimeblock("ray_traced_shadows");
        {
            RHI_CommandList::BeginMarker("trace");
            {
                Renderer::SetPass("ray_traced_shadows", rhi_all_mips);
                RHI_CommandList::SetShaders(shader_rgen, shader_miss, shader_hit);
                RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_shadows, rhi_all_mips, 0, true);
                if (tex_shadows_local)
                {
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::rt_shadows_local), tex_shadows_local, rhi_all_mips, 0, true);
                }

                // x tells the raygen whether transparents exist, opaque scenes take a single accept first hit ray
                m_pcb_pass_cpu.set_f3_value(m_transparents_present ? 1.0f : 0.0f);

                uint32_t width  = tex_shadows->GetWidth();
                uint32_t height = tex_shadows->GetHeight();
                RHI_CommandList::TraceRays(width, height);
            }
            RHI_CommandList::EndMarker();

            Pass_Denoise_RayTracedShadows();
        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_Denoise_RayTracedShadows()
    {
        if (Window::IsMinimized())
        {
            return;
        }

        if (
            !cvar_ray_traced_shadows.GetValueAs<bool>() ||
            !RHI_Device::IsSupportedRayTracing() ||
            IsSecondaryViewActive()
        )
        {
            return;
        }

        if (!GetTopLevelAccelerationStructure())
        {
            return;
        }

        RHI_Texture* tex_shadows       = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows);
        RHI_Texture* tex_shadows_local = GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows_local);
        RHI_Texture* tex_mv            = GetRenderTarget(Renderer_RenderTarget::nrd_screen_mv);
        RHI_Texture* tex_normal        = GetRenderTarget(Renderer_RenderTarget::nrd_screen_normal_roughness);
        RHI_Texture* tex_view_z        = GetRenderTarget(Renderer_RenderTarget::nrd_screen_viewz);
        RHI_Texture* tex_in            = GetRenderTarget(Renderer_RenderTarget::nrd_in_penumbra);
        RHI_Texture* tex_out           = GetRenderTarget(Renderer_RenderTarget::nrd_out_shadow);
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

        auto denoise_sigma = [&](float tan_radius, uint32_t slice, bool is_local, const Vector3& direction, uint32_t denoiser_index)
        {
            Renderer::SetPass(is_local ? "nrd_pack_shadows_local" : "nrd_pack_shadows", rhi_all_mips);
            RHI_CommandList::SetShader(shader_pack);
            m_pcb_pass_cpu.set_f3_value(tan_radius, static_cast<float>(slice), is_local ? 1.0f : 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_shadows);
            if (tex_shadows_local)
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::rt_shadows_local), tex_shadows_local);
            }
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_mv, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_normal, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), tex_view_z, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex4), tex_in, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(tex_shadows);

            if (!RHI_VendorTechnology::NRD_Dispatch(Nrd_Preset::Shadows, tex_mv, tex_normal, tex_view_z, tex_in, tex_out, &direction, denoiser_index))
            {
                return false;
            }

            Renderer::SetPass(is_local ? "nrd_unpack_shadows_local" : "nrd_unpack_shadows", rhi_all_mips);
            RHI_CommandList::SetShader(shader_unpack);
            m_pcb_pass_cpu.set_f3_value(0.0f, static_cast<float>(slice), is_local ? 1.0f : 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_out);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_shadows, rhi_all_mips, 0, true);
            if (tex_shadows_local)
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::rt_shadows_local), tex_shadows_local, rhi_all_mips, 0, true);
            }
            RHI_CommandList::Dispatch(tex_shadows);
            return true;
        };

        // sigma must use the same solar angular radius as the shadow rays
        Vector3 light_direction = Vector3::Down;
        const float tan_light_angular_radius = tanf(0.00465f);
        if (Light* sun = World::GetDirectionalLight())
        {
            if (sun->GetFlag(LightFlags::Shadows) && sun->GetIntensityRadiometric() > 0.0f)
            {
                light_direction = -sun->GetEntity()->GetForward();
            }
        }

        RHI_CommandList::BeginMarker("nrd_sun");
        {
            denoise_sigma(tan_light_angular_radius, 0, false, light_direction, 0);
        }
        RHI_CommandList::EndMarker();

        // nrd needs lightDirection only for the sun, local lights pass zero
        const Vector3 local_direction = Vector3::Zero;
        for (uint32_t i = 1; i < m_count_active_lights; i++)
        {
            const uint32_t slot = (m_bindless_lights[i].flags >> 8) & 7u;
            if (slot == 0)
            {
                continue;
            }

            // area lights already average a rectangle of rays, sigma would treat that
            // as a binary occluder and rebuild a spherical penumbra on top
            if ((m_bindless_lights[i].flags & (1u << 6)) != 0)
            {
                continue;
            }

            RHI_CommandList::BeginMarker("nrd_local");
            {
                denoise_sigma(0.0f, slot - 1, true, local_direction, slot);
            }
            RHI_CommandList::EndMarker();
        }
    }

    // self inverting pairing table, lin 2026 3.1, repeated 2x2 shuffles yield deltas of standard deviation sigma, wrapped so it tiles
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

    void Renderer::Pass_ReSTIR_TraceInitial(RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* tex_skysphere, RHI_Texture* const* reservoirs, uint32_t width, uint32_t height)
    {
        // amd tdrs when tracerays dispatches with uninitialized push constants, all raygen passes must push constants
        Renderer::BeginPass("restir_pt_initial", rhi_all_mips);
        {
            RHI_CommandList::SetShaders(
                GetShader(Renderer_Shader::restir_pt_ray_generation_r),
                GetShader(Renderer_Shader::restir_pt_ray_miss_r),
                GetShader(Renderer_Shader::restir_pt_ray_hit_r)
            );
            RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), tex_skysphere);

            // reset and bind the per-hit geometry info ring as uav
            GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::geometry_info), GetBuffer(Renderer_Buffer::GeometryInfo));

            // emissive triangle nee pool, prefix sum and per-triangle data populated by
            // BuildEmissiveTriangleNeePool, count comes through buffer_frame.restir_pt_emissive_tri_count
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::emissive_triangles), GetBuffer(Renderer_Buffer::EmissiveTriangles));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);

            for (uint32_t i = 0; i < restir_reservoir_textures; i++)
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0) + i, reservoirs[i], rhi_all_mips, 0, true);

            RHI_CommandList::TraceRays(width, height);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_ReSTIR_Temporal(RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_prev, uint32_t dispatch_x, uint32_t dispatch_y)
    {
        RHI_Shader* shader_temporal = GetShader(Renderer_Shader::restir_pt_temporal_c);
        if (!shader_temporal || !shader_temporal->IsCompiled())
        {
            return;
        }

        Renderer::BeginPass("restir_pt_temporal", rhi_all_mips);
        {
            RHI_CommandList::SetShader(shader_temporal);

            // the temporal pass re-traces the chosen reservoir's visibility ray to kill samples that no longer reach their reconnection vertex
            if (tlas)
            {
                RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
            }

            // the shared restir header declares the bindless geometry and emissive triangle
            // resources, bind them so the pass has a complete descriptor set
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::geometry_info), GetBuffer(Renderer_Buffer::GeometryInfo));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::emissive_triangles), GetBuffer(Renderer_Buffer::EmissiveTriangles));

            for (uint32_t i = 0; i < restir_reservoir_textures; i++)
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs_prev[i]);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0)      + i, reservoirs[i], rhi_all_mips, 0, true);
            }

            // the validity gate needs the prior surface depth at prev_uv, the current depth there ghosts moving objects
            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), depth_prev);
            }

            // previous frame normals for the same gate
            if (RHI_Texture* normal_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal_previous))
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex5), normal_prev);
            }

            if (RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere))
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), tex_skysphere);
            }

            // duplication score of the previous frame's reservoirs, drives the adaptive m cap, lin 2026 5
            if (RHI_Texture* tex_duplication = GetRenderTarget(Renderer_RenderTarget::restir_duplication))
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_duplication);
            }

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(dispatch_x, dispatch_y, 1);
        }
        RHI_CommandList::EndPass();
    }

    bool Renderer::Pass_ReSTIR_SpatialPair(RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_spatial, uint32_t dispatch_x, uint32_t dispatch_y)
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

        // paired spatial reuse, lin 2026 3, the pre-pass shifts each pixel to its partners once so the resample reads both directions
        Renderer::BeginPass("restir_pt_spatial_shift", rhi_all_mips);
        {
            RHI_CommandList::SetShader(shader_shift);
            RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);

            // see the matching comment in Pass_ReSTIR_Temporal
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::geometry_info), GetBuffer(Renderer_Buffer::GeometryInfo));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::emissive_triangles), GetBuffer(Renderer_Buffer::EmissiveTriangles));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::restir_pairing), GetBuffer(Renderer_Buffer::RestirPairing));

            if (RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere))
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), tex_skysphere);
            }

            for (uint32_t i = 0; i < restir_reservoir_textures; i++)
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs[i]);
            }

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex),  shift[0], rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), shift[1], rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), shift[2], rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(dispatch_x, dispatch_y, 1);
        }
        RHI_CommandList::EndPass();

        Renderer::BeginPass("restir_pt_spatial", rhi_all_mips);
        {
            RHI_CommandList::SetShader(shader_spatial);

            // tlas for the periodic sample validation ray, the pairing buffer resolves partners
            RHI_CommandList::SetAccelerationStructure(static_cast<uint32_t>(Renderer_BindingsSrv::tlas), tlas);
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::restir_pairing), GetBuffer(Renderer_Buffer::RestirPairing));

            // pre-pass shift results, one per pairing table
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), shift[0]);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), shift[1]);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), shift[2]);

            for (uint32_t i = 0; i < restir_reservoir_textures; i++)
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::reservoir_prev0) + i, reservoirs[i]);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::reservoir0)      + i, reservoirs_spatial[i], rhi_all_mips, 0, true);
            }

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(dispatch_x, dispatch_y, 1);
        }
        RHI_CommandList::EndPass();

        // the resample output lands in reservoirs_spatial, swapping pointers is free compared to blitting
        auto& render_targets = GetRenderTargets();
        for (uint32_t i = 0; i < restir_reservoir_textures; i++)
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
        for (uint32_t i = 0; i < restir_reservoir_textures; i++)
        {
            uint32_t idx_cur  = static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0)      + i;
            uint32_t idx_prev = static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir_prev0) + i;
            swap(render_targets[idx_cur], render_targets[idx_prev]);
        }
    }

    // cpu only pointer swap, call at frame end once every consumer of the current depth and normal targets has finished recording
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

    void Renderer::Pass_ReSTIR_PathTracing()
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

        // a secondary view is not in the tlas, tracing from it would gather the primary scene's
        // radiance, so the preview renders with direct lighting only, the reservoirs that hold
        // the primary's temporal state are left untouched
        if (IsSecondaryViewActive())
        {
            RHI_CommandList::ClearTexture(tex_gi, Color::standard_black);
            return;
        }

        if (!cvar_restir_pt.GetValueAs<bool>() || !RHI_Device::IsSupportedRayTracing() || !reservoir0)
        {
            if (!m_pass_state.cleared_restir)
            {
                RHI_CommandList::ClearTexture(tex_gi, Color::standard_black);
                m_pass_state.cleared_restir = true;
            }
            return;
        }
        m_pass_state.cleared_restir = false;

        RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure();
        if (!tlas || m_pass_state.skip_rt_trace)
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

        RHI_Texture* reservoirs[restir_reservoir_textures];
        RHI_Texture* reservoirs_prev[restir_reservoir_textures];
        RHI_Texture* reservoirs_spatial[restir_reservoir_textures];
        for (uint32_t i = 0; i < restir_reservoir_textures; i++)
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

        // one-shot clear after (re)allocation, new textures are not guaranteed zeroed and depth_previous must start at far so disocclusion fails closed
        if (!m_pass_state.restir_reservoirs_initialized)
        {
            for (uint32_t i = 0; i < restir_reservoir_textures; i++)
            {
                RHI_CommandList::ClearTexture(reservoirs[i],         Color::standard_transparent);
                RHI_CommandList::ClearTexture(reservoirs_prev[i],    Color::standard_transparent);
                RHI_CommandList::ClearTexture(reservoirs_spatial[i], Color::standard_transparent);
            }

            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                RHI_CommandList::ClearTexture(depth_prev, Color::standard_white, 1.0f);
            }

            // zero normals make the disocclusion gate fail closed until real history exists
            if (RHI_Texture* normal_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal_previous))
            {
                RHI_CommandList::ClearTexture(normal_prev, Color::standard_black);
            }

            // zero duplication keeps the temporal m cap at its default until real history exists
            if (RHI_Texture* tex_duplication = GetRenderTarget(Renderer_RenderTarget::restir_duplication))
            {
                RHI_CommandList::ClearTexture(tex_duplication, Color::standard_black);
            }

            // sigma is the paper's 16 px scaled by the restir resolution factor, the tables regenerate with the reservoirs
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
                pairing_buffer->Update(pairing.data(), static_cast<uint32_t>(pairing.size() * sizeof(uint32_t)));
            }

            m_pass_state.restir_reservoirs_initialized = true;
        }

        Pass_ReSTIR_TraceInitial(tlas, tex_gi, tex_skysphere, reservoirs, width, height);
        Pass_ReSTIR_Temporal(tlas, tex_gi, reservoirs, reservoirs_prev, dispatch_x, dispatch_y);
        const bool ran_spatial = Pass_ReSTIR_SpatialPair(tlas, tex_gi, reservoirs, reservoirs_spatial, dispatch_x, dispatch_y);

        // counts shifted copies of the same candidate per pixel, next frame's temporal pass lowers the confidence cap where they cluster
        if (RHI_Texture* tex_duplication = GetRenderTarget(Renderer_RenderTarget::restir_duplication))
        {
            RHI_Shader* shader_duplication = GetShader(Renderer_Shader::restir_pt_duplication_c);
            if (shader_duplication && shader_duplication->IsCompiled())
            {
                RHI_CommandList::BeginPass("restir_pt_duplication");
                {
                    RHI_CommandList::SetShader(shader_duplication);
                    RHI_Texture* reservoir_seed = ran_spatial ? reservoirs_spatial[3] : reservoirs[3];
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), reservoir_seed);
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_duplication, rhi_all_mips, 0, true);
                    RHI_CommandList::Dispatch(dispatch_x, dispatch_y, 1);
                }
                RHI_CommandList::EndPass();
            }
        }

        Pass_ReSTIR_SwapReservoirs();
    }

    void Renderer::Pass_ReSTIR_Denoising()
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
            if (tex_gi_raw && tex_gi_denoised)
            {
                Pass_BlitRestirFallback(tex_gi_raw, tex_gi_denoised);
            }
            return;
        }

        const uint32_t min_rt_dimension = 64;
        if (tex_gi_raw->GetWidth() < min_rt_dimension || tex_gi_raw->GetHeight() < min_rt_dimension)
        {
            Pass_BlitRestirFallback(tex_gi_raw, tex_gi_denoised);
            return;
        }

        RHI_Shader* shader_pack   = GetShader(Renderer_Shader::restir_pt_nrd_pack_c);
        RHI_Shader* shader_unpack = GetShader(Renderer_Shader::restir_pt_nrd_unpack_c);
        if (!shader_pack || !shader_unpack || !shader_pack->IsCompiled() || !shader_unpack->IsCompiled())
        {
            Pass_BlitRestirFallback(tex_gi_raw, tex_gi_denoised);
            return;
        }

        RHI_CommandList::BeginTimeblock("restir_pt_denoise");
        {
            RHI_CommandList::BeginMarker("nrd_pack");
            {
                Renderer::SetPass("restir_pt_nrd_pack", rhi_all_mips);
                RHI_CommandList::SetShader(shader_pack);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_gi_raw);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_mv, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), tex_normal, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3), tex_view_z, rhi_all_mips, 0, true);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex4), tex_in, rhi_all_mips, 0, true);
                RHI_CommandList::Dispatch(tex_gi_raw);
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("nrd_dispatch");
            {
                if (!RHI_VendorTechnology::NRD_Dispatch(Nrd_Preset::Gi, tex_mv, tex_normal, tex_view_z, tex_in, tex_out))
                {
                    Pass_BlitRestirFallback(tex_gi_raw, tex_gi_denoised);
                    RHI_CommandList::EndMarker();
                    RHI_CommandList::EndTimeblock();
                    return;
                }
            }
            RHI_CommandList::EndMarker();

            RHI_CommandList::BeginMarker("nrd_unpack");
            {
                RHI_CommandList::SetShader(shader_unpack, "restir_pt_nrd_unpack");
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_out);
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_gi_denoised, rhi_all_mips, 0, true);
                RHI_CommandList::Dispatch(tex_gi_denoised);
            }
            RHI_CommandList::EndMarker();
        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_ScreenSpaceShadows()
    {
        RHI_Texture* tex_sss = GetRenderTarget(Renderer_RenderTarget::sss);

        // bend sss is screen space from one depth view, applying left eye contacts to the right eye
        // darkens half the frame, clear to lit and skip until a stereo aware path exists
        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            if (tex_sss)
            {
                RHI_CommandList::ClearTexture(tex_sss, Color(1.0f, 1.0f, 1.0f, 1.0f));
            }
            return;
        }

        // this used to early out when ray traced shadows were on, on the grounds that the trace already
        // captures exact contact occlusion. it only captures what is in the acceleration structure, and
        // gpu scatter owns no entities, so grass and micro detail are missing from it entirely. the screen
        // space trace sees whatever reached the depth buffer, so both run now and the lighting pass takes
        // the darker of the two, which leaves geometry the trace can see unchanged

        // nothing writes the target when no light qualifies, and the lighting pass still samples it for
        // any light whose flags claim contact shadows, so the last frame that did write would keep being
        // applied. clear to lit and leave
        bool any_light = false;
        for (Entity* entity : World::GetEntities())
        {
            Light* light = entity->GetComponent<Light>();
            any_light    = any_light ||
                           (light                                           &&
                            light->GetLightType() == LightType::Directional &&
                            light->GetFlag(LightFlags::Shadows)             &&
                            light->GetFlag(LightFlags::ShadowsScreenSpace)  &&
                            light->GetIntensityRadiometric() != 0.0f);
        }
        if (!any_light)
        {
            if (tex_sss)
            {
                RHI_CommandList::ClearTexture(tex_sss, Color(1.0f, 1.0f, 1.0f, 1.0f));
            }
            return;
        }

        RHI_CommandList::BeginPass("screen_space_shadows");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::sss_c_bend));

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex_sss), tex_sss, rhi_all_mips, 0, true);
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
                        RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                        RHI_CommandList::Dispatch(dispatch.WaveCount[0], dispatch.WaveCount[1], dispatch.WaveCount[2]);
                    }
                }
            }
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_LightClusterAssign()
    {
        RHI_CommandList::BeginTimeblock("light_cluster_assign");
        {
            // clear the overflow counter every frame, the shader bumps it atomically once per overflowing cluster
            // and the light pass / cpu telemetry reads it back as the count for this frame
            {
                const uint32_t zero = 0;
                RHI_CommandList::UpdateBuffer(GetBuffer(Renderer_Buffer::ClusterStats), 0, sizeof(uint32_t), &zero, false);
            }

            // when only the directional sun is active there are no clustered lights, the light shader guards
            // with total_lights > 1 so the grid contents are unread, skip the dispatch entirely
            if (m_count_active_lights <= 1)
            {
                RHI_CommandList::EndTimeblock();
                return;
            }

            RHI_CommandList::SetShader(GetShader(Renderer_Shader::light_cluster_assign_c), "light_cluster_assign");

            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_grid), GetBuffer(Renderer_Buffer::ClusterLightGrid));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_indices), GetBuffer(Renderer_Buffer::ClusterLightIndices));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_stats), GetBuffer(Renderer_Buffer::ClusterStats));

            // one dispatch even in vr, the grid lives in left eye space which contains the right eye to well under a tile, so force eye 0
            const uint32_t saved_eye = m_pcb_pass_cpu.eye_index;
            m_pcb_pass_cpu.eye_index = 0;
            RHI_CommandList::Dispatch(CLUSTER_COUNT_X, CLUSTER_COUNT_Y, CLUSTER_COUNT_Z);
            m_pcb_pass_cpu.eye_index = saved_eye;

        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_LightClusterVisualize()
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

        RHI_CommandList::BeginPass("light_cluster_visualize");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::light_cluster_visualize_c));

            // depth feeds the per pixel cluster id, debug_output is the heatmap target
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_debug, rhi_all_mips, 0, true);

            // the visualize shader reads the grid populated by light_cluster_assign in compute batch a
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_grid), GetBuffer(Renderer_Buffer::ClusterLightGrid));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_indices), GetBuffer(Renderer_Buffer::ClusterLightIndices));

            // f3.x: visualization mode, f3.y: saturation cap for the count ramp (lights per cluster that maps to full red)
            // the cap is a cvar so users can tune contrast per scene, defaults to 4 which matches typical street-lamp density
            const float cap = max(cvar_cluster_visualize_cap.GetValue(), 1.0f);
            m_pcb_pass_cpu.set_f3_value(static_cast<float>(mode), cap, 0.0f);
            RHI_CommandList::Dispatch(tex_debug, 1.0f);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_LightFlares(uint32_t eye_layer /*= rhi_all_mips*/)
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

        Renderer::BeginPass("light_flares", eye_layer);
        {
            RHI_CommandList::SetShaders(shader_v, shader_p);
            RHI_CommandList::SetBlendState(GetBlendState(Renderer_BlendState::Additive));
            RHI_CommandList::SetColorTarget(tex_out);

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
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Draw(6);
            }
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Fog(uint32_t eye, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Shader* shader_inject    = GetShader(Renderer_Shader::fog_inject_c);
        RHI_Shader* shader_integrate = GetShader(Renderer_Shader::fog_integrate_c);
        RHI_Texture* tex_scatter     = GetRenderTarget(Renderer_RenderTarget::fog_scatter);
        RHI_Texture* tex_history     = GetRenderTarget(Renderer_RenderTarget::fog_scatter_history);
        RHI_Texture* tex_integrated  = GetRenderTarget(Renderer_RenderTarget::fog_integrated);
        if (!tex_scatter || !tex_history || !tex_integrated)
        {
            return;
        }

        if (!shader_inject || !shader_inject->IsCompiled() ||
            !shader_integrate || !shader_integrate->IsCompiled())
        {
            Renderer::BeginPass("fog_clear", eye_layer);
            RHI_CommandList::ClearTexture(tex_integrated, Color(0.0f, 0.0f, 0.0f, 1.0f));
            RHI_CommandList::EndPass();
            return;
        }

        const bool use_history =
            m_pass_state.fog_history.valid &&
            !IsSecondaryViewActive() &&
            eye == 0;

        RHI_Texture* tex_write = m_pass_state.fog_history.SelectWrite(tex_scatter, tex_history);
        RHI_Texture* tex_read  = m_pass_state.fog_history.SelectRead(tex_scatter, tex_history);

        const uint32_t groups_x = (renderer_fog_volume_width + 7) / 8;
        const uint32_t groups_y = (renderer_fog_volume_height + 7) / 8;
        const uint32_t groups_z = (renderer_fog_volume_depth + 3) / 4;

        Renderer::BeginPass("fog_inject", eye_layer);
        {
            RHI_CommandList::SetShader(shader_inject);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3d), tex_write, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3d), tex_read);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), GetRenderTarget(Renderer_RenderTarget::skysphere));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::shadow_atlas));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex5), GetRenderTarget(Renderer_RenderTarget::cloud_shadow));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::volumetric_light_indices), GetBuffer(Renderer_Buffer::VolumetricLightIndices));

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

            if (RHI_Texture* tex_ocean_norm = GetRenderTarget(Renderer_RenderTarget::ocean_normal))
            {
                RHI_CommandList::SetTexture("tex_ocean_normal", tex_ocean_norm);
            }

            RHI_Texture* tex_ocean_disp = GetRenderTarget(
                m_pass_state.ocean_history.SelectWrite(
                    Renderer_RenderTarget::ocean_displacement,
                    Renderer_RenderTarget::ocean_displacement_previous
                )
            );
            if (tex_ocean_disp)
            {
                RHI_CommandList::SetTexture("tex_ocean_displacement", tex_ocean_disp);
            }

            m_pcb_pass_cpu.set_f3_value(use_history ? 0.0f : 1.0f, cvar_fog.GetValue());
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch(groups_x, groups_y, groups_z);
        }
        RHI_CommandList::EndPass();

        Renderer::BeginPass("fog_integrate", eye_layer);
        {
            RHI_CommandList::SetShader(shader_integrate);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3d), tex_write);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex3d), tex_integrated, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(groups_x, groups_y, 1);
        }
        RHI_CommandList::EndPass();

        if (!IsSecondaryViewActive() && eye == 0)
        {
            m_pass_state.fog_history.Advance();
        }
    }

    void Renderer::Pass_Light(const bool is_transparent_pass, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Texture* light_diffuse  = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* light_specular = GetRenderTarget(Renderer_RenderTarget::light_specular);

        const char* pass_name = is_transparent_pass ? "light_transparent" : "light";
        Renderer::BeginPass(pass_name, eye_layer);
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::light_c));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex_sss), GetRenderTarget(Renderer_RenderTarget::sss), rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), GetRenderTarget(Renderer_RenderTarget::skysphere));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::shadow_atlas));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::rt_shadows_local), GetRenderTarget(Renderer_RenderTarget::ray_traced_shadows_local));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex5), GetRenderTarget(Renderer_RenderTarget::cloud_shadow));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), light_diffuse, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex2), light_specular, rhi_all_mips, 0, true);

            // clustered lighting grid, written by light_cluster_assign in compute batch a
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_grid), GetBuffer(Renderer_Buffer::ClusterLightGrid));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cluster_light_indices), GetBuffer(Renderer_Buffer::ClusterLightIndices));

            // bind tlas for inline ray traced shadows when ray tracing is supported and the world has geometry
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

            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            RHI_CommandList::Dispatch(light_diffuse, Renderer::GetResolutionScale());
        }
        RHI_CommandList::EndPass();
    }
    
    void Renderer::Pass_Light_Composition(const bool is_transparent_pass, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Shader* shader_c           = GetShader(Renderer_Shader::light_composition_c);
        RHI_Texture* tex_out           = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_skysphere     = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_light_diffuse = GetRenderTarget(Renderer_RenderTarget::light_diffuse);
        RHI_Texture* tex_light_specular = GetRenderTarget(Renderer_RenderTarget::light_specular);
        RHI_Texture* tex_fog           = GetRenderTarget(Renderer_RenderTarget::fog_integrated);

        Renderer::BeginPass(is_transparent_pass ? "light_composition_transparent" : "light_composition", eye_layer);
        {
            RHI_CommandList::SetShader(shader_c);
            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), tex_skysphere);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), tex_light_diffuse);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), tex_light_specular);
            if (is_transparent_pass)
            {
                RHI_Texture* tex_opaque = GetRenderTarget(Renderer_RenderTarget::frame_render_opaque);
                if (tex_opaque)
                {
                    RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_opaque);
                }
            }
            if (tex_fog)
            {
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3d), tex_fog);
            }
            RHI_CommandList::Dispatch(tex_out, Renderer::GetResolutionScale());
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Light_Ibl(uint32_t eye_layer /*= rhi_all_mips*/)
    {
        RHI_Shader* shader   = GetShader(Renderer_Shader::light_image_based_c);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::frame_render);

        Renderer::BeginPass("light_image_based", eye_layer);
        {
            RHI_CommandList::SetShader(shader);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex_sss), GetRenderTarget(Renderer_RenderTarget::sss), rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::lut_brdf_specular));
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), GetRenderTarget(Renderer_RenderTarget::skysphere));
            // l2 sh projected from the skysphere, used for directional diffuse ibl
            RHI_Texture* tex_sky_sh = GetRenderTarget(Renderer_RenderTarget::sky_sh);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex5), tex_sky_sh ? tex_sky_sh : GetStandardTexture(Renderer_StandardTexture::Black));

            RHI_Texture* tex_gi = GetRenderTarget(
                Renderer_RenderTarget::restir_denoised
            );
            const bool restir_enabled =
                cvar_restir_pt.GetValueAs<bool>() &&
                tex_gi &&
                !IsSecondaryViewActive();
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex4), tex_gi ? tex_gi : GetStandardTexture(
                    Renderer_StandardTexture::Black
                ));

            m_pcb_pass_cpu.set_f3_value(
                static_cast<float>(
                    GetRenderTarget(
                        Renderer_RenderTarget::skysphere
                    )->GetMipCount()
                ),
                restir_enabled ? 1.0f : 0.0f,
                restir_composition_intensity
            );
            RHI_CommandList::Dispatch(tex_out, Renderer::GetResolutionScale());
        }
        RHI_CommandList::EndPass();
    }
}
