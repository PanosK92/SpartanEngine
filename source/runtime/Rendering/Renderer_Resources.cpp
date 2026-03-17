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

//= INCLUDES =============================
#include "pch.h"
#include "Window.h"
#include "Renderer.h"
#include "Material.h"
#include "../Geometry/GeometryGeneration.h"
#include "../World/Components/Light.h"
#include "../Resource/ResourceCache.h"
#include "../Display/Display.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Device.h"
#include "../XR/Xr.h"
#ifdef _MSC_VER
#include "../RHI/RHI_VendorTechnology.h"
#endif
//========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // graphics states
        array<shared_ptr<RHI_RasterizerState>, static_cast<uint32_t>(Renderer_RasterizerState::Max)>     rasterizer_states;
        array<shared_ptr<RHI_DepthStencilState>, static_cast<uint32_t>(Renderer_DepthStencilState::Max)> depth_stencil_states;
        array<shared_ptr<RHI_BlendState>, 3>                                                             blend_states;

        // renderer resources
        array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)> render_targets;
        array<shared_ptr<RHI_Shader>,  static_cast<uint32_t>(Renderer_Shader::max)>       shaders;
        array<shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>      samplers;
        array<shared_ptr<RHI_Buffer>,  static_cast<uint32_t>(Renderer_Buffer::Max)>       buffers;

        // asset resources
        array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_StandardTexture::Max)> standard_textures;
        array<shared_ptr<Mesh>, static_cast<uint32_t>(MeshType::Max)>                        standard_meshes;
        shared_ptr<Font>                                                                     standard_font;
        shared_ptr<Material>                                                                 standard_material;
    }

    void Renderer::CreateBuffers()
    {
        uint32_t element_count = renderer_draw_data_buffer_count;
        #define buffer(x) buffers[static_cast<uint8_t>(x)]

        // initialization values
        uint32_t spd_counter_value = 0;
        array<Instance, renderer_max_instance_count> identity;
        identity.fill(Instance::GetIdentity());

        buffer(Renderer_Buffer::ConstantFrame)      = make_shared<RHI_Buffer>(RHI_Buffer_Type::Constant, sizeof(Cb_Frame),                           element_count,                          nullptr,            true, "frame");
        buffer(Renderer_Buffer::SpdCounter)         = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(uint32_t)),    1,                                      &spd_counter_value, true, "spd_counter");
        buffer(Renderer_Buffer::MaterialParameters) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Material)), rhi_max_array_size,                     nullptr,            true, "materials");
        buffer(Renderer_Buffer::LightParameters)    = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Light)),    rhi_max_array_size,                     nullptr,            true, "lights");
        buffer(Renderer_Buffer::DummyInstance)      = make_shared<RHI_Buffer>(RHI_Buffer_Type::Instance, sizeof(Instance),                           static_cast<uint32_t>(identity.size()), &identity,          true, "dummy_instance_buffer");
        buffer(Renderer_Buffer::GeometryInfo)       = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_GeometryInfo)), rhi_max_array_size,                     nullptr,            true, "geometry_info");

        // single draw data and aabb buffers large enough for all frames; each frame writes to its
        // own offset region so the bindless descriptors never change, eliminating the race where
        // vkUpdateDescriptorSets (host-side, instantly visible under UPDATE_AFTER_BIND) would
        // change the buffer pointer while in-flight gpu commands were still reading from it
        buffer(Renderer_Buffer::DrawData) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_DrawData)),
            renderer_max_draw_calls * renderer_draw_data_buffer_count, nullptr, true,
            "draw_data"
        );
        buffer(Renderer_Buffer::AABBs) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_Aabb)),
            rhi_max_array_size * renderer_draw_data_buffer_count, nullptr, true,
            "aabbs"
        );

        // per-frame rotated buffers
        uint32_t draw_count_init = 0;
        for (uint32_t i = 0; i < renderer_draw_data_buffer_count; i++)
        {
            FrameResource& fr = m_frame_resources[i];

            fr.indirect_draw_args = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs)),
                rhi_max_array_size, nullptr, true,
                (string("indirect_draw_args_") + to_string(i)).c_str()
            );

            fr.indirect_draw_data = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_DrawData)),
                rhi_max_array_size, nullptr, true,
                (string("indirect_draw_data_") + to_string(i)).c_str()
            );

            fr.indirect_draw_args_out = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs)),
                rhi_max_array_size, nullptr, true,
                (string("indirect_draw_args_out_") + to_string(i)).c_str()
            );

            fr.indirect_draw_data_out = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_DrawData)),
                rhi_max_array_size, nullptr, true,
                (string("indirect_draw_data_out_") + to_string(i)).c_str()
            );

            fr.indirect_draw_count = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),
                1, &draw_count_init, true,
                (string("indirect_draw_count_") + to_string(i)).c_str()
            );
        }

        // point the active buffer slots at frame 0
        const FrameResource& fr = m_frame_resources[0];
        buffer(Renderer_Buffer::IndirectDrawArgs)    = fr.indirect_draw_args;
        buffer(Renderer_Buffer::IndirectDrawData)    = fr.indirect_draw_data;
        buffer(Renderer_Buffer::IndirectDrawArgsOut) = fr.indirect_draw_args_out;
        buffer(Renderer_Buffer::IndirectDrawDataOut) = fr.indirect_draw_data_out;
        buffer(Renderer_Buffer::IndirectDrawCount)   = fr.indirect_draw_count;

        // particle buffers
        const uint32_t particle_max = 100000;
        uint32_t particle_counter_init[2] = { 0, 0 };
        buffer(Renderer_Buffer::ParticleBufferA) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_Particle)),       particle_max, nullptr,                true, "particle_buffer_a");
        buffer(Renderer_Buffer::ParticleCounter) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),          2,            particle_counter_init,  true, "particle_counter");
        buffer(Renderer_Buffer::ParticleEmitter) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_EmitterParams)),  1,            nullptr,                true, "particle_emitter");
    }

    void Renderer::CreateDepthStencilStates()
    {
        #define depth_stencil_state(x) depth_stencil_states[static_cast<uint8_t>(x)]

        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        depth_stencil_state(Renderer_DepthStencilState::Off)              = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::ReadEqual)        = make_shared<RHI_DepthStencilState>(true,  false, RHI_Comparison_Function::Equal);
        depth_stencil_state(Renderer_DepthStencilState::ReadGreaterEqual) = make_shared<RHI_DepthStencilState>(true,  false, RHI_Comparison_Function::GreaterEqual);
        depth_stencil_state(Renderer_DepthStencilState::ReadWrite)        = make_shared<RHI_DepthStencilState>(true,  true,  RHI_Comparison_Function::GreaterEqual);
    }

    void Renderer::CreateRasterizerStates()
    {
        // bias done in shader, hardware bias is uncontrollable across cascades
        float bias              = 0.0f;
        float bias_clamp        = 0.0f;
        float bias_slope_scaled = 0.0f;
        float line_width        = 3.0f;

        #define rasterizer_state(x) rasterizer_states[static_cast<uint8_t>(x)]
        //                                                                                               fill mode,    depth clip enabled,  bias,   bias clamp,       slope scaled bias, line width
        rasterizer_state(Renderer_RasterizerState::Solid)             = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     true,  0.0f,         0.0f,       0.0f,              line_width);
        rasterizer_state(Renderer_RasterizerState::Wireframe)         = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Wireframe, true,  0.0f,         0.0f,       0.0f,              line_width);
        rasterizer_state(Renderer_RasterizerState::Light_point_spot)  = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     true,  bias,         bias_clamp, bias_slope_scaled, line_width);
        rasterizer_state(Renderer_RasterizerState::Light_directional) = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     false, bias * 0.5f,  bias_clamp, bias_slope_scaled, line_width);
    }

    void Renderer::CreateBlendStates()
    {
        #define blend_state(x) blend_states[static_cast<uint8_t>(x)]

        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        blend_state(Renderer_BlendState::Off)      = make_shared<RHI_BlendState>(false);
        blend_state(Renderer_BlendState::Alpha)    = make_shared<RHI_BlendState>(true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        blend_state(Renderer_BlendState::Additive) = make_shared<RHI_BlendState>(true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers()
    {
        #define sampler(type, filter_min, filter_mag, filter_mip, address_mode, comparison_func, anisotropy, comparison_enabled, mip_bias) \
        samplers[static_cast<uint8_t>(type)] = make_shared<RHI_Sampler>(filter_min, filter_mag, filter_mip, address_mode, comparison_func, anisotropy, comparison_enabled, mip_bias)

        // non anisotropic
        {
            static bool samplers_created = false;
            if (!samplers_created)
            {
                sampler(Renderer_Sampler::Compare_depth,         RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Greater, 0.0f, true,  0.0f); // reverse-z
                sampler(Renderer_Sampler::Point_clamp_edge,      RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Point_clamp_border,    RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Point_wrap,            RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,        RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Bilinear_clamp_edge,   RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Bilinear_clamp_border, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Bilinear_wrap,         RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,        RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Trilinear_clamp,       RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);

                samplers_created = true;
            }
        }

        // anisotropic (negative mip bias when upscaling to keep textures sharp)
        {
            float mip_bias_new = 0.0f;
            if (GetResolutionOutput().x > GetResolutionRender().x)
            {
                mip_bias_new = log2(GetResolutionRender().x / GetResolutionOutput().x) - 1.0f;
            }

            static float mip_bias = numeric_limits<float>::max();
            if (mip_bias_new != mip_bias)
            {
                mip_bias         = mip_bias_new;
                float anisotropy = cvar_anisotropy.GetValue();
                sampler(Renderer_Sampler::Anisotropic_wrap, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_bias);
            }
        }

        m_bindless_samplers_dirty = true;
    }

    void Renderer::UpdateOptionalRenderTargets()
    {
        uint32_t width  = static_cast<uint32_t>(GetResolutionRender().x);
        uint32_t height = static_cast<uint32_t>(GetResolutionRender().y);
        uint32_t flags  = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit;

        #define render_target(x) render_targets[static_cast<uint8_t>(x)]
        
        // ssao
        bool need_ssao = cvar_ssao.GetValueAs<bool>();
        if (need_ssao && !render_target(Renderer_RenderTarget::ssao))
        {
            render_target(Renderer_RenderTarget::ssao) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, flags | RHI_Texture_ConcurrentSharing, "ssao");
        }
        else if (!need_ssao && render_target(Renderer_RenderTarget::ssao))
        {
            render_target(Renderer_RenderTarget::ssao) = nullptr;
        }
        
        // ray traced reflections gbuffer
        bool need_rt_reflections = cvar_ray_traced_reflections.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing();
        if (need_rt_reflections && !render_target(Renderer_RenderTarget::gbuffer_reflections_position))
        {
            render_target(Renderer_RenderTarget::gbuffer_reflections_position) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R32G32B32A32_Float, flags, "gbuffer_reflections_position");
            render_target(Renderer_RenderTarget::gbuffer_reflections_normal)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "gbuffer_reflections_normal");
            render_target(Renderer_RenderTarget::gbuffer_reflections_albedo)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_reflections_albedo");
        }
        else if (!need_rt_reflections && render_target(Renderer_RenderTarget::gbuffer_reflections_position))
        {
            render_target(Renderer_RenderTarget::gbuffer_reflections_position) = nullptr;
            render_target(Renderer_RenderTarget::gbuffer_reflections_normal)   = nullptr;
            render_target(Renderer_RenderTarget::gbuffer_reflections_albedo)   = nullptr;
        }
        
        // restir reservoirs
        bool need_restir = cvar_restir_pt.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing();
        if (need_restir && !render_target(Renderer_RenderTarget::restir_reservoir0))
        {
            uint32_t restir_flags = flags | RHI_Texture_ConcurrentSharing;

            static const char* reservoir_names[] =
            {
                "restir_reservoir0",         "restir_reservoir1",         "restir_reservoir2",         "restir_reservoir3",         "restir_reservoir4",
                "restir_reservoir_prev0",    "restir_reservoir_prev1",    "restir_reservoir_prev2",    "restir_reservoir_prev3",    "restir_reservoir_prev4",
                "restir_reservoir_spatial0", "restir_reservoir_spatial1", "restir_reservoir_spatial2", "restir_reservoir_spatial3", "restir_reservoir_spatial4",
            };

            for (uint32_t i = 0; i < 15; i++)
            {
                auto rt = static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0) + i);
                render_target(rt) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R32G32B32A32_Float, restir_flags, reservoir_names[i]);
            }
            
            // nrd denoiser
            render_target(Renderer_RenderTarget::nrd_viewz)                    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16_Float,          restir_flags, "nrd_viewz");
            render_target(Renderer_RenderTarget::nrd_normal_roughness)         = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R10G10B10A2_Unorm,  restir_flags, "nrd_normal_roughness");
            render_target(Renderer_RenderTarget::nrd_diff_radiance_hitdist)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "nrd_diff_radiance_hitdist");
            render_target(Renderer_RenderTarget::nrd_spec_radiance_hitdist)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "nrd_spec_radiance_hitdist");
            render_target(Renderer_RenderTarget::nrd_out_diff_radiance_hitdist)= make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "nrd_out_diff_radiance_hitdist");
            render_target(Renderer_RenderTarget::nrd_out_spec_radiance_hitdist)= make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "nrd_out_spec_radiance_hitdist");
        }
        else if (!need_restir && render_target(Renderer_RenderTarget::restir_reservoir0))
        {
            for (uint32_t i = 0; i < 15; i++)
            {
                auto rt = static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0) + i);
                render_target(rt) = nullptr;
            }
            
            render_target(Renderer_RenderTarget::nrd_viewz)                     = nullptr;
            render_target(Renderer_RenderTarget::nrd_normal_roughness)          = nullptr;
            render_target(Renderer_RenderTarget::nrd_diff_radiance_hitdist)     = nullptr;
            render_target(Renderer_RenderTarget::nrd_spec_radiance_hitdist)     = nullptr;
            render_target(Renderer_RenderTarget::nrd_out_diff_radiance_hitdist) = nullptr;
            render_target(Renderer_RenderTarget::nrd_out_spec_radiance_hitdist) = nullptr;
        }
        
        #undef render_target
    }

    void Renderer::CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic)
    {
        uint32_t width_render  = static_cast<uint32_t>(GetResolutionRender().x);
        uint32_t height_render = static_cast<uint32_t>(GetResolutionRender().y);
        uint32_t width_output  = static_cast<uint32_t>(GetResolutionOutput().x);
        uint32_t height_output = static_cast<uint32_t>(GetResolutionOutput().y);

        auto compute_mip_count = [](const uint32_t width, const uint32_t height, const uint32_t smallest_dimension)
        {
            uint32_t max_dimension = max(width, height);
            uint32_t mip_count     = 1;
            while (max_dimension >= smallest_dimension)
            {
                max_dimension /= 2;
                mip_count++;
            }
            return mip_count;
        };

        // avoid combining uav + rtv on frequently accessed targets (forces suboptimal layouts on amd)

        #define render_target(x) render_targets[static_cast<uint8_t>(x)]

        // vr stereo uses 2-layer array textures for multiview rendering
        bool xr_stereo          = Xr::IsSessionRunning() && Xr::GetStereoMode();
        RHI_Texture_Type rt_type = xr_stereo ? RHI_Texture_Type::Type2DArray : RHI_Texture_Type::Type2D;
        uint32_t rt_layers       = xr_stereo ? Xr::eye_count : 1;

        // resolution - render
        if (create_render)
        {
            // frame (kept as 2d even in stereo - reused per eye during compute passes)
            {
                render_target(Renderer_RenderTarget::frame_render)        = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_render");
                render_target(Renderer_RenderTarget::frame_render_opaque) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_render_opaque");
            }

            // g-buffer (concurrent sharing: read by async compute for ssao/sss)
            {
                uint32_t flags = RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing;

                render_target(Renderer_RenderTarget::gbuffer_color)    = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_color");
                render_target(Renderer_RenderTarget::gbuffer_normal)   = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R16G16B16A16_Float, flags, "gbuffer_normal");
                render_target(Renderer_RenderTarget::gbuffer_material) = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_material");
                render_target(Renderer_RenderTarget::gbuffer_velocity) = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R16G16_Float,       flags, "gbuffer_velocity");
                render_target(Renderer_RenderTarget::gbuffer_depth)    = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::D32_Float,          flags, "gbuffer_depth");
            }

            // light
            {
                uint32_t flags = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit;

                render_target(Renderer_RenderTarget::light_diffuse)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_diffuse");
                render_target(Renderer_RenderTarget::light_specular)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_specular");
                render_target(Renderer_RenderTarget::light_volumetric) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_volumetric");
            }

            // occlusion
            {
                // amd depth format restrictions: separate texture for uav + manual blit
                render_target(Renderer_RenderTarget::gbuffer_depth_occluders) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv, "depth_occluders");

                // full mip chain so the cull shader can pick a level where the aabb fits in ~1-2 texels
                uint32_t hiz_mip_count = static_cast<uint32_t>(floor(log2(static_cast<float>(max(width_render, height_render))))) + 1;
                render_target(Renderer_RenderTarget::gbuffer_depth_occluders_hiz) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, hiz_mip_count, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_PerMipViews, "depth_occluders_hiz");
            }

            // misc
            render_target(Renderer_RenderTarget::sss)                = make_shared<RHI_Texture>(RHI_Texture_Type::Type2DArray, width_render, height_render, 4, 1, RHI_Format::R16_Float,          RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "sss");
            render_target(Renderer_RenderTarget::reflections)        = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "reflections");
            render_target(Renderer_RenderTarget::ray_traced_shadows) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16_Float,          RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "ray_traced_shadows");
            render_target(Renderer_RenderTarget::restir_output)      = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "restir_output");
            
            // optional render targets (ssao, rt reflections, restir)
            UpdateOptionalRenderTargets();
            
            if (RHI_Device::IsSupportedVrs())
            {
                // vrs texture dimensions must match the gpu's reported texel size
                uint32_t texel_size_x = max(RHI_Device::PropertyGetMaxShadingRateTexelSizeX(), 1u);
                uint32_t texel_size_y = max(RHI_Device::PropertyGetMaxShadingRateTexelSizeY(), 1u);
                uint32_t vrs_width    = (width_render + texel_size_x - 1) / texel_size_x;
                uint32_t vrs_height   = (height_render + texel_size_y - 1) / texel_size_y;
                render_target(Renderer_RenderTarget::shading_rate) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, vrs_width, vrs_height, 1, 1, RHI_Format::R8_Uint, RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_Rtv | RHI_Texture_Vrs | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "shading_rate");
            }
            render_target(Renderer_RenderTarget::shadow_atlas) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 8192, 8192, 1, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit, "shadow_atlas");
        }

        // resolution - output
        if (create_output)
        {
            // frame (kept as 2d - reused per eye during compute passes)
            uint32_t mip_count = compute_mip_count(width_output, height_output, 16);
            render_target(Renderer_RenderTarget::frame_output)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit | RHI_Texture_PerMipViews | RHI_Texture_ConcurrentSharing, "frame_output");
            render_target(Renderer_RenderTarget::frame_output_2) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_output_2");

            // stereo output: 2-layer array for xr swapchain blit (only when vr is active)
            if (xr_stereo)
            {
                render_target(Renderer_RenderTarget::frame_output_stereo) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2DArray, width_output, height_output, Xr::eye_count, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_output_stereo");
            }
            render_target(Renderer_RenderTarget::debug_output)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "debug_output");

            // misc
            render_target(Renderer_RenderTarget::bloom)                       = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "bloom");
            render_target(Renderer_RenderTarget::outline)                     = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,         RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv,         "outline");
            render_target(Renderer_RenderTarget::gbuffer_depth_opaque_output) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,         RHI_Format::D32_Float,          RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit,   "depth_opaque_output");
        }

        // resolution - fixed (created once)
        if (!render_target(Renderer_RenderTarget::lut_brdf_specular))
        {
            // lookup tables
            render_target(Renderer_RenderTarget::lut_brdf_specular)           = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 512, 512,  1, 1, RHI_Format::R16G16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "lut_brdf_specular");
            render_target(Renderer_RenderTarget::lut_atmosphere_scatter)      = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 256, 256, 32, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "lut_atmosphere_scatter");
            render_target(Renderer_RenderTarget::lut_atmosphere_transmittance)= make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 256, 64,   1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "lut_atmosphere_transmittance");
            render_target(Renderer_RenderTarget::lut_atmosphere_multiscatter) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 32,  32,   1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "lut_atmosphere_multiscatter");

            // misc
            render_target(Renderer_RenderTarget::blur)      = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 4096, 4096, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "blur_scratch");
            const uint32_t lowest_dimension                 = 16; // lowest mip is 16x16, preserving directional detail for diffuse IBL (1x1 loses directionality)
            render_target(Renderer_RenderTarget::skysphere) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 4096, 2048, 1, compute_mip_count(4096, 2048, lowest_dimension), RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "skysphere");

            // auto-exposure
            render_target(Renderer_RenderTarget::auto_exposure)          = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1, 1, 1, 1, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "auto_exposure_1");
            render_target(Renderer_RenderTarget::auto_exposure_previous) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1, 1, 1, 1, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "auto_exposure_2");

            // volumetric clouds (r16g16b16a16 to avoid material texture detection)
            render_target(Renderer_RenderTarget::cloud_noise_shape)  = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 128, 128, 128, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "cloud_noise_shape");
            render_target(Renderer_RenderTarget::cloud_noise_detail) = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 32,  32,  32,  1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "cloud_noise_detail");
            render_target(Renderer_RenderTarget::cloud_shadow)       = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1024, 1024, 1, 1, RHI_Format::R16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "cloud_shadow");
            
        }
    }

    namespace
    {
        void compile_shader(
            Renderer_Shader id, RHI_Shader_Type type, const string& path,
            bool async             = true,
            RHI_Vertex_Type vtype  = RHI_Vertex_Type::Max,
            const char* define     = nullptr
        )
        {
            auto& slot = shaders[static_cast<uint8_t>(id)];
            slot = make_shared<RHI_Shader>();
            if (define)
            {
                slot->AddDefine(define);
            }
            slot->Compile(type, path, async, vtype);
        }
    }

    void Renderer::CreateShaders()
    {
        const string sd = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "/";

        // debug
        compile_shader(Renderer_Shader::line_v,    RHI_Shader_Type::Vertex,  sd + "line.hlsl",    true, RHI_Vertex_Type::PosCol);
        compile_shader(Renderer_Shader::line_p,    RHI_Shader_Type::Pixel,   sd + "line.hlsl");
        compile_shader(Renderer_Shader::grid_v,    RHI_Shader_Type::Vertex,  sd + "grid.hlsl",    true, RHI_Vertex_Type::PosUvNorTan);
        compile_shader(Renderer_Shader::grid_p,    RHI_Shader_Type::Pixel,   sd + "grid.hlsl");
        compile_shader(Renderer_Shader::outline_v, RHI_Shader_Type::Vertex,  sd + "outline.hlsl", true, RHI_Vertex_Type::PosUvNorTan);
        compile_shader(Renderer_Shader::outline_p, RHI_Shader_Type::Pixel,   sd + "outline.hlsl");
        compile_shader(Renderer_Shader::outline_c, RHI_Shader_Type::Compute, sd + "outline.hlsl");

        // depth
        compile_shader(Renderer_Shader::depth_prepass_v,           RHI_Shader_Type::Vertex, sd + "depth_prepass.hlsl", true, RHI_Vertex_Type::PosUvNorTan);
        compile_shader(Renderer_Shader::depth_prepass_alpha_test_p, RHI_Shader_Type::Pixel,  sd + "depth_prepass.hlsl");
        compile_shader(Renderer_Shader::depth_light_v,             RHI_Shader_Type::Vertex, sd + "depth_light.hlsl",  true, RHI_Vertex_Type::PosUvNorTan);
        compile_shader(Renderer_Shader::depth_light_alpha_color_p, RHI_Shader_Type::Pixel,  sd + "depth_light.hlsl");

        // g-buffer
        compile_shader(Renderer_Shader::gbuffer_v, RHI_Shader_Type::Vertex, sd + "g_buffer.hlsl", true, RHI_Vertex_Type::PosUvNorTan);
        compile_shader(Renderer_Shader::gbuffer_p, RHI_Shader_Type::Pixel,  sd + "g_buffer.hlsl");

        // tessellation
        compile_shader(Renderer_Shader::tessellation_h, RHI_Shader_Type::Hull,   sd + "common_tessellation.hlsl");
        compile_shader(Renderer_Shader::tessellation_d, RHI_Shader_Type::Domain, sd + "common_tessellation.hlsl");

        // light
        compile_shader(Renderer_Shader::light_integration_brdf_specular_lut_c,  RHI_Shader_Type::Compute, sd + "light_integration.hlsl", false, RHI_Vertex_Type::Max, "BRDF_SPECULAR_LUT");
        compile_shader(Renderer_Shader::light_integration_environment_filter_c, RHI_Shader_Type::Compute, sd + "light_integration.hlsl", true,  RHI_Vertex_Type::Max, "ENVIRONMENT_FILTER");
        compile_shader(Renderer_Shader::light_c,                                RHI_Shader_Type::Compute, sd + "light.hlsl");
        compile_shader(Renderer_Shader::light_composition_c,                    RHI_Shader_Type::Compute, sd + "light_composition.hlsl");
        compile_shader(Renderer_Shader::light_image_based_c,                    RHI_Shader_Type::Compute, sd + "light_image_based.hlsl");

        // blur
        compile_shader(Renderer_Shader::blur_gaussian_c,            RHI_Shader_Type::Compute, sd + "blur.hlsl");
        compile_shader(Renderer_Shader::blur_gaussian_bilaterial_c, RHI_Shader_Type::Compute, sd + "blur.hlsl", true, RHI_Vertex_Type::Max, "PASS_BLUR_GAUSSIAN_BILATERAL");

        // bloom
        compile_shader(Renderer_Shader::bloom_luminance_c,          RHI_Shader_Type::Compute, sd + "bloom.hlsl", true, RHI_Vertex_Type::Max, "LUMINANCE");
        compile_shader(Renderer_Shader::bloom_downsample_c,         RHI_Shader_Type::Compute, sd + "bloom.hlsl", true, RHI_Vertex_Type::Max, "DOWNSAMPLE");
        compile_shader(Renderer_Shader::bloom_upsample_blend_mip_c, RHI_Shader_Type::Compute, sd + "bloom.hlsl", true, RHI_Vertex_Type::Max, "UPSAMPLE_BLEND_MIP");
        compile_shader(Renderer_Shader::bloom_blend_frame_c,        RHI_Shader_Type::Compute, sd + "bloom.hlsl", true, RHI_Vertex_Type::Max, "BLEND_FRAME");

        // amd fidelityfx
        compile_shader(Renderer_Shader::ffx_cas_c,         RHI_Shader_Type::Compute, sd + "amd_fidelity_fx/cas.hlsl");
        compile_shader(Renderer_Shader::ffx_spd_average_c, RHI_Shader_Type::Compute, sd + "amd_fidelity_fx/spd.hlsl", false, RHI_Vertex_Type::Max, "AVERAGE");
        compile_shader(Renderer_Shader::ffx_spd_min_c,     RHI_Shader_Type::Compute, sd + "amd_fidelity_fx/spd.hlsl", false, RHI_Vertex_Type::Max, "MIN");
        compile_shader(Renderer_Shader::ffx_spd_max_c,     RHI_Shader_Type::Compute, sd + "amd_fidelity_fx/spd.hlsl", false, RHI_Vertex_Type::Max, "MAX");

        // sky
        compile_shader(Renderer_Shader::skysphere_c,                    RHI_Shader_Type::Compute, sd + "sky/skysphere.hlsl");
        compile_shader(Renderer_Shader::skysphere_lut_c,                RHI_Shader_Type::Compute, sd + "sky/skysphere.hlsl", true,  RHI_Vertex_Type::Max, "LUT");
        compile_shader(Renderer_Shader::skysphere_transmittance_lut_c,  RHI_Shader_Type::Compute, sd + "sky/skysphere.hlsl", false, RHI_Vertex_Type::Max, "TRANSMITTANCE_LUT");
        compile_shader(Renderer_Shader::skysphere_multiscatter_lut_c,   RHI_Shader_Type::Compute, sd + "sky/skysphere.hlsl", false, RHI_Vertex_Type::Max, "MULTISCATTER_LUT");

        // post-process
        compile_shader(Renderer_Shader::fxaa_c,                 RHI_Shader_Type::Compute, sd + "fxaa/fxaa.hlsl");
        compile_shader(Renderer_Shader::font_v,                 RHI_Shader_Type::Vertex,  sd + "font.hlsl", true, RHI_Vertex_Type::PosUv);
        compile_shader(Renderer_Shader::font_p,                 RHI_Shader_Type::Pixel,   sd + "font.hlsl");
        compile_shader(Renderer_Shader::film_grain_c,           RHI_Shader_Type::Compute, sd + "film_grain.hlsl");
        compile_shader(Renderer_Shader::chromatic_aberration_c, RHI_Shader_Type::Compute, sd + "chromatic_aberration.hlsl");
        compile_shader(Renderer_Shader::vhs_c,                  RHI_Shader_Type::Compute, sd + "vhs.hlsl");
        compile_shader(Renderer_Shader::output_c,               RHI_Shader_Type::Compute, sd + "output.hlsl");
        compile_shader(Renderer_Shader::motion_blur_c,          RHI_Shader_Type::Compute, sd + "motion_blur.hlsl");
        compile_shader(Renderer_Shader::ssao_c,                 RHI_Shader_Type::Compute, sd + "ssao.hlsl");
        compile_shader(Renderer_Shader::sss_c_bend,             RHI_Shader_Type::Compute, sd + "screen_space_shadows/bend_sss.hlsl");
        compile_shader(Renderer_Shader::depth_of_field_c,       RHI_Shader_Type::Compute, sd + "depth_of_field.hlsl");
        compile_shader(Renderer_Shader::variable_rate_shading_c, RHI_Shader_Type::Compute, sd + "variable_rate_shading.hlsl");
        compile_shader(Renderer_Shader::blit_c,                 RHI_Shader_Type::Compute, sd + "blit.hlsl");

        // indirect draw
        compile_shader(Renderer_Shader::indirect_cull_c,         RHI_Shader_Type::Compute, sd + "indirect_cull.hlsl");
        compile_shader(Renderer_Shader::gbuffer_indirect_v,      RHI_Shader_Type::Vertex,  sd + "g_buffer.hlsl",      true, RHI_Vertex_Type::Max, "INDIRECT_DRAW");
        compile_shader(Renderer_Shader::gbuffer_indirect_p,      RHI_Shader_Type::Pixel,   sd + "g_buffer.hlsl",      true, RHI_Vertex_Type::Max, "INDIRECT_DRAW");
        compile_shader(Renderer_Shader::depth_prepass_indirect_v, RHI_Shader_Type::Vertex,  sd + "depth_prepass.hlsl", true, RHI_Vertex_Type::Max, "INDIRECT_DRAW");

        // misc
        compile_shader(Renderer_Shader::icon_c,                                  RHI_Shader_Type::Compute, sd + "icon.hlsl");
        compile_shader(Renderer_Shader::dithering_c,                              RHI_Shader_Type::Compute, sd + "dithering.hlsl");
        compile_shader(Renderer_Shader::transparency_reflection_refraction_c,     RHI_Shader_Type::Compute, sd + "transparency_reflection_refraction.hlsl");
        compile_shader(Renderer_Shader::auto_exposure_c,                          RHI_Shader_Type::Compute, sd + "auto_exposure.hlsl");

        // ray-tracing
        if (RHI_Device::IsSupportedRayTracing())
        {
            compile_shader(Renderer_Shader::reflections_ray_generation_r, RHI_Shader_Type::RayGeneration, sd + "ray_traced_reflections.hlsl");
            compile_shader(Renderer_Shader::reflections_ray_miss_r,       RHI_Shader_Type::RayMiss,       sd + "ray_traced_reflections.hlsl");
            compile_shader(Renderer_Shader::reflections_ray_hit_r,        RHI_Shader_Type::RayHit,        sd + "ray_traced_reflections.hlsl");
            compile_shader(Renderer_Shader::light_reflections_c,          RHI_Shader_Type::Compute,       sd + "light_reflections.hlsl");
            compile_shader(Renderer_Shader::nrd_prepare_c,                RHI_Shader_Type::Compute,       sd + "nrd_prepare.hlsl");

            compile_shader(Renderer_Shader::shadows_ray_generation_r, RHI_Shader_Type::RayGeneration, sd + "ray_traced_shadows.hlsl");
            compile_shader(Renderer_Shader::shadows_ray_miss_r,       RHI_Shader_Type::RayMiss,       sd + "ray_traced_shadows.hlsl");
            compile_shader(Renderer_Shader::shadows_ray_hit_r,        RHI_Shader_Type::RayHit,        sd + "ray_traced_shadows.hlsl");

            compile_shader(Renderer_Shader::restir_pt_ray_generation_r, RHI_Shader_Type::RayGeneration, sd + "restir_pt.hlsl");
            compile_shader(Renderer_Shader::restir_pt_ray_miss_r,       RHI_Shader_Type::RayMiss,       sd + "restir_pt.hlsl", true, RHI_Vertex_Type::Max, "MAIN_MISS");
            compile_shader(Renderer_Shader::restir_pt_ray_hit_r,        RHI_Shader_Type::RayHit,        sd + "restir_pt.hlsl", true, RHI_Vertex_Type::Max, "MAIN_HIT");
            compile_shader(Renderer_Shader::restir_pt_temporal_c,       RHI_Shader_Type::Compute,       sd + "restir_pt_temporal.hlsl");
            compile_shader(Renderer_Shader::restir_pt_spatial_c,        RHI_Shader_Type::Compute,       sd + "restir_pt_spatial.hlsl");
        }

        // volumetric clouds
        compile_shader(Renderer_Shader::cloud_noise_shape_c,  RHI_Shader_Type::Compute, sd + "sky/cloud_noise.hlsl",  true, RHI_Vertex_Type::Max, "SHAPE_NOISE");
        compile_shader(Renderer_Shader::cloud_noise_detail_c, RHI_Shader_Type::Compute, sd + "sky/cloud_noise.hlsl",  true, RHI_Vertex_Type::Max, "DETAIL_NOISE");
        compile_shader(Renderer_Shader::cloud_shadow_c,       RHI_Shader_Type::Compute, sd + "sky/cloud_shadow.hlsl");

        // gpu-driven particles
        compile_shader(Renderer_Shader::particle_emit_c,     RHI_Shader_Type::Compute, sd + "particles.hlsl", true, RHI_Vertex_Type::Max, "EMIT");
        compile_shader(Renderer_Shader::particle_simulate_c, RHI_Shader_Type::Compute, sd + "particles.hlsl", true, RHI_Vertex_Type::Max, "SIMULATE");
        compile_shader(Renderer_Shader::particle_render_c,   RHI_Shader_Type::Compute, sd + "particles.hlsl", true, RHI_Vertex_Type::Max, "RENDER");

        // gpu texture compression (synchronous)
        compile_shader(Renderer_Shader::texture_compress_bc1_c, RHI_Shader_Type::Compute, sd + "texture_compress_bc1.hlsl", false);
        compile_shader(Renderer_Shader::texture_compress_bc3_c, RHI_Shader_Type::Compute, sd + "texture_compress_bc3.hlsl", false);
        compile_shader(Renderer_Shader::texture_compress_bc5_c, RHI_Shader_Type::Compute, sd + "texture_compress_bc5.hlsl", false);
    }

    void Renderer::CreateFonts()
    {
        const string dir_font = ResourceCache::GetResourceDirectory(ResourceDirectory::Fonts) + "/";

        uint32_t size = static_cast<uint32_t>(10 * Window::GetDpiScale());
        standard_font = make_shared<Font>(dir_font + "OpenSans/OpenSans-Medium.ttf", size, Color(0.9f, 0.9f, 0.9f, 1.0f));
    }

    void Renderer::CreateStandardMeshes()
    {
        using VertexVec = vector<RHI_Vertex_PosTexNorTan>;
        using IndexVec  = vector<uint32_t>;

        struct MeshDef
        {
            MeshType type;
            void (*generate)(VertexVec*, IndexVec*);
            const char* name;
        };

        // wrappers to bind default arguments for functions with extra parameters
        auto gen_sphere   = [](VertexVec* v, IndexVec* i) { geometry_generation::generate_sphere(v, i);   };
        auto gen_cylinder = [](VertexVec* v, IndexVec* i) { geometry_generation::generate_cylinder(v, i); };
        auto gen_cone     = [](VertexVec* v, IndexVec* i) { geometry_generation::generate_cone(v, i);     };

        const MeshDef defs[] =
        {
            { MeshType::Cube,     geometry_generation::generate_cube, "standard_cube"     },
            { MeshType::Quad,     geometry_generation::generate_quad, "standard_quad"     },
            { MeshType::Sphere,   +gen_sphere,                        "standard_sphere"   },
            { MeshType::Cylinder, +gen_cylinder,                      "standard_cylinder" },
            { MeshType::Cone,     +gen_cone,                          "standard_cone"     },
        };

        const string project_directory = ResourceCache::GetProjectDirectory();
        for (const MeshDef& def : defs)
        {
            shared_ptr<Mesh> mesh = make_shared<Mesh>();
            VertexVec vertices;
            IndexVec indices;

            def.generate(&vertices, &indices);
            mesh->SetResourceFilePath(project_directory + def.name + EXTENSION_MESH);
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
            mesh->AddGeometry(vertices, indices, false);
            mesh->SetType(def.type);
            mesh->CreateGpuBuffers();

            standard_meshes[static_cast<uint8_t>(def.type)] = mesh;
        }

        m_lines_vertex_buffer = make_shared<RHI_Buffer>();
    }

    void Renderer::CreateStandardTextures()
    {
        const string dir_texture   = ResourceCache::GetResourceDirectory(ResourceDirectory::Textures) + "/";
        const string dir_materials = "project/materials/";

        #define standard_texture(x) standard_textures[static_cast<uint32_t>(x)]

        // perlin noise
        standard_texture(Renderer_StandardTexture::Noise_perlin) = make_shared<RHI_Texture>(dir_texture + "noise_perlin.png");

        // blue noise texture (only one is actually used in shaders)
        standard_texture(Renderer_StandardTexture::Noise_blue) = make_shared<RHI_Texture>(dir_texture + "noise_blue_0.png");

        // gizmos
        {
            standard_texture(Renderer_StandardTexture::Gizmo_light_directional) = make_shared<RHI_Texture>(dir_texture + "sun.png");
            standard_texture(Renderer_StandardTexture::Gizmo_light_point)       = make_shared<RHI_Texture>(dir_texture + "light_bulb.png");
            standard_texture(Renderer_StandardTexture::Gizmo_light_spot)        = make_shared<RHI_Texture>(dir_texture + "flashlight.png");
            standard_texture(Renderer_StandardTexture::Gizmo_audio_source)      = make_shared<RHI_Texture>(dir_texture + "audio.png");
        }

        // misc
        {
            standard_texture(Renderer_StandardTexture::Checkerboard) = make_shared<RHI_Texture>(dir_texture + "no_texture.png");
        }

        // solid 1x1 textures
        {
            auto create_solid_texture = [](const char* name, std::byte r, std::byte g, std::byte b, std::byte a)
            {
                std::vector<RHI_Texture_Mip>   mips   = { RHI_Texture_Mip{std::vector<std::byte>{r, g, b, a}} };
                std::vector<RHI_Texture_Slice> slices  = { RHI_Texture_Slice{mips} };
                return make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1, 1, 1, 1, RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv | RHI_Texture_Uav, name, slices);
            };

            standard_texture(Renderer_StandardTexture::Black) = create_solid_texture("black_texture", std::byte{0},   std::byte{0},   std::byte{0},   std::byte{255});
            standard_texture(Renderer_StandardTexture::White) = create_solid_texture("white_texture", std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255});
        }
    }

    void Renderer::CreateStandardMaterials()
    {
        const string data_dir = string(ResourceCache::GetDataDirectory()) + "/";
        FileSystem::CreateDirectory_(data_dir);

        standard_material = make_shared<Material>();
        standard_material->SetResourceName("standard" + string(EXTENSION_MATERIAL));
        standard_material->SetProperty(MaterialProperty::TextureTilingX, 1.0f);
        standard_material->SetProperty(MaterialProperty::TextureTilingY, 1.0f);
        standard_material->SetProperty(MaterialProperty::ColorR,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorG,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorB,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorA,         1.0f);
        standard_material->SetProperty(MaterialProperty::WorldSpaceUv,   1.0f);
        standard_material->SetTexture(MaterialTextureType::Color,        Renderer::GetStandardTexture(Renderer_StandardTexture::Checkerboard));
    }

    void Renderer::DestroyResources()
    {
        render_targets.fill(nullptr);
        shaders.fill(nullptr);
        samplers.fill(nullptr);
        standard_textures.fill(nullptr);
        standard_meshes.fill(nullptr);
        buffers.fill(nullptr);

        m_frame_resources.fill(FrameResource{});

        standard_font     = nullptr;
        standard_material = nullptr;
    }

    array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)>& Renderer::GetRenderTargets()
    {
        return render_targets;
    }

    array<shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& Renderer::GetShaders()
    {
        return shaders;
    }

    array<shared_ptr<RHI_Buffer>, static_cast<uint32_t>(Renderer_Buffer::Max)>& Renderer::GetStructuredBuffers()
    {
        return buffers;
    }

    array<shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>& Renderer::GetSamplers()
    {
        return samplers;
    }

    RHI_RasterizerState* Renderer::GetRasterizerState(const Renderer_RasterizerState type)
    {
        return rasterizer_states[static_cast<uint8_t>(type)].get();
    }

    RHI_DepthStencilState* Renderer::GetDepthStencilState(const Renderer_DepthStencilState type)
    {
        return depth_stencil_states[static_cast<uint8_t>(type)].get();
    }

    RHI_BlendState* Renderer::GetBlendState(const Renderer_BlendState type)
    {
        return blend_states[static_cast<uint8_t>(type)].get();
    }

    RHI_Texture* Renderer::GetRenderTarget(const Renderer_RenderTarget type)
    {
        return render_targets[static_cast<uint8_t>(type)].get();
    }

    RHI_Shader* Renderer::GetShader(const Renderer_Shader type)
    {
        return shaders[static_cast<uint8_t>(type)].get();
    }

    RHI_Buffer* Renderer::GetBuffer(const Renderer_Buffer type)
    {
        return buffers[static_cast<uint8_t>(type)].get();
    }

    void Renderer::RotateFrameBuffers()
    {
        m_frame_resource_index = (m_frame_resource_index + 1) % renderer_draw_data_buffer_count;
        const FrameResource& fr = m_frame_resources[m_frame_resource_index];

        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawArgs)]    = fr.indirect_draw_args;
        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawData)]    = fr.indirect_draw_data;
        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawArgsOut)] = fr.indirect_draw_args_out;
        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawDataOut)] = fr.indirect_draw_data_out;
        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawCount)]   = fr.indirect_draw_count;
    }

    RHI_Texture* Renderer::GetStandardTexture(const Renderer_StandardTexture type)
    {
        return standard_textures[static_cast<uint8_t>(type)].get();
    }

    shared_ptr<Mesh>& Renderer::GetStandardMesh(const MeshType type)
    {
        return standard_meshes[static_cast<uint8_t>(type)];
    }

    shared_ptr<Font>& Renderer::GetFont()
    {
        return standard_font;
    }

    shared_ptr<Material>& Renderer::GetStandardMaterial()
    {
        return standard_material;
    }

    void Renderer::ClearMaterialTextureReferences()
    {
        // clear cached texture pointers that become dangling when resource cache shuts down
        if (standard_material)
        {
            standard_material->ClearPackedTextures();
        }
    }
}
