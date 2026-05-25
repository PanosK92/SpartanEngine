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
        template<typename E, typename A>
        auto& at(A& arr, E e) { return arr[static_cast<size_t>(e)]; }

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

        // six reservoirs each across current, previous and spatial slots, abi mirrors restir_reservoir_prev0 and restir_reservoir_spatial0 stride
        // the 6th slot per reservoir holds the source primary g-buffer for the chosen sample,
        // used by the temporal and spatial passes to evaluate the source brdf and jacobian
        // without sampling the current frame g-buffer at a reprojected pixel
        const uint32_t restir_reservoirs_per_slot  = 6;
        const uint32_t restir_reservoir_slot_count = restir_reservoirs_per_slot * 3;

        // visit every restir reservoir slot offset by index, fn signature is void(uint32_t i, Renderer_RenderTarget rt)
        template<typename F>
        void for_restir_reservoir_slot(F fn)
        {
            for (uint32_t i = 0; i < restir_reservoir_slot_count; i++)
                fn(i, static_cast<Renderer_RenderTarget>(static_cast<uint32_t>(Renderer_RenderTarget::restir_reservoir0) + i));
        }
    }

    void Renderer::CreateBuffers()
    {
        uint32_t element_count = renderer_draw_data_buffer_count;

        // initialization values
        uint32_t spd_counter_value = 0;
        array<Instance, renderer_max_instance_count> identity;
        identity.fill(Instance::GetIdentity());

        at(buffers, Renderer_Buffer::ConstantFrame)      = make_shared<RHI_Buffer>(RHI_Buffer_Type::Constant, sizeof(Cb_Frame),                           element_count,                          nullptr,            true, "frame");
        at(buffers, Renderer_Buffer::SpdCounter)         = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(uint32_t)),    1,                                      &spd_counter_value, true, "spd_counter");
        at(buffers, Renderer_Buffer::MaterialParameters) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Material)), rhi_max_array_size,                     nullptr,            true, "materials");
        at(buffers, Renderer_Buffer::LightParameters)    = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Light)),    rhi_max_array_size,                     nullptr,            true, "lights");
        at(buffers, Renderer_Buffer::DummyInstance)      = make_shared<RHI_Buffer>(RHI_Buffer_Type::Instance, sizeof(Instance),                           static_cast<uint32_t>(identity.size()), &identity,          true, "dummy_instance_buffer");
        at(buffers, Renderer_Buffer::GeometryInfo)       = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_GeometryInfo)), rhi_max_array_size,                     nullptr,            true, "geometry_info");

        // single draw data and aabb buffers large enough for all frames; each frame writes to its
        // own offset region so the bindless descriptors never change, eliminating the race where
        // vkUpdateDescriptorSets (host-side, instantly visible under UPDATE_AFTER_BIND) would
        // change the buffer pointer while in-flight gpu commands were still reading from it
        at(buffers, Renderer_Buffer::DrawData) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_DrawData)),
            renderer_max_draw_calls * renderer_draw_data_buffer_count, nullptr, true,
            "draw_data"
        );
        at(buffers, Renderer_Buffer::AABBs) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_Aabb)),
            rhi_max_array_size * renderer_draw_data_buffer_count, nullptr, true,
            "aabbs"
        );

        // per-frame rotated buffers
        for (uint32_t i = 0; i < renderer_draw_data_buffer_count; i++)
        {
            FrameResource& fr = m_frame_resources[i];

            // single-slot args buffer for the final non-indexed indirect draw, vertex_count is bumped atomically by the triangle cull pass
            fr.indirect_draw_args = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs)),
                1, nullptr, true,
                (string("indirect_draw_args_") + to_string(i)).c_str()
            );

            fr.indirect_draw_data = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_DrawData)),
                renderer_max_indirect_draws, nullptr, true,
                (string("indirect_draw_data_") + to_string(i)).c_str()
            );

            // meshlet-cull survivors, gpu-only (compute writes, vs reads), keep it off the host-visible heap
            fr.meshlet_instances = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_MeshletInstance)),
                renderer_max_meshlet_instances, nullptr, false,
                (string("meshlet_instances_") + to_string(i)).c_str()
            );

            // triangle-cull survivors, gpu-only and the largest of the cull buffers, must not land in bar memory
            fr.visible_triangles = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),
                renderer_max_visible_triangles, nullptr, false,
                (string("visible_triangles_") + to_string(i)).c_str()
            );

            // single-slot indirect dispatch args for the triangle cull pass, group_count_x is bumped atomically by the meshlet cull
            fr.triangle_dispatch_args = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_IndirectDispatchArgs)),
                1, nullptr, true,
                (string("triangle_dispatch_args_") + to_string(i)).c_str()
            );

            fr.cull_tasks = make_shared<RHI_Buffer>(
                RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_CullTask)),
                renderer_max_cull_tasks, nullptr, true,
                (string("cull_tasks_") + to_string(i)).c_str()
            );
        }

        // point the active buffer slots at frame 0
        const FrameResource& fr = m_frame_resources[0];
        at(buffers, Renderer_Buffer::IndirectDrawArgs)     = fr.indirect_draw_args;
        at(buffers, Renderer_Buffer::IndirectDrawData)     = fr.indirect_draw_data;
        at(buffers, Renderer_Buffer::MeshletInstances)     = fr.meshlet_instances;
        at(buffers, Renderer_Buffer::VisibleTriangles)     = fr.visible_triangles;
        at(buffers, Renderer_Buffer::TriangleDispatchArgs) = fr.triangle_dispatch_args;
        at(buffers, Renderer_Buffer::CullTasks)            = fr.cull_tasks;

        // clustered lighting buffers, written by the cluster assign pass and read by the light pass
        // grid stores (first_index, count) per cluster, indices is fixed-slot with first_index = cluster_id * CLUSTER_MAX_LIGHTS
        // single grid is shared across both eyes in vr stereo, built in the left eye's view-projection space which
        // contains the right eye's view to within the inter pupillary distance, far less than one cluster tile width
        at(buffers, Renderer_Buffer::ClusterLightGrid) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t) * 2),
            CLUSTER_COUNT_TOTAL, nullptr, false, "cluster_light_grid"
        );
        at(buffers, Renderer_Buffer::ClusterLightIndices) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),
            CLUSTER_COUNT_TOTAL * CLUSTER_MAX_LIGHTS, nullptr, false, "cluster_light_indices"
        );

        // tiny stats buffer for the cluster assign pass, currently holds a single overflow counter
        // bumped atomically when a cluster exceeds CLUSTER_MAX_LIGHTS, cleared each frame on the cpu
        // host visible so the cpu can read the previous frame's value for editor telemetry without a fence stall
        at(buffers, Renderer_Buffer::ClusterStats) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),
            1, nullptr, true, "cluster_stats"
        );

        // restir path tracing emissive triangle nee pool, rebuilt each frame on the cpu inside
        // UpdateAccelerationStructures by walking every renderable whose material has non-zero
        // emission, capped at restir_emissive_tri_max to bound the cpu walk cost on dense scenes
        // (handful of glowing surfaces is the common case, a million tri ferns are not)
        at(buffers, Renderer_Buffer::EmissiveTriangles) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_EmissiveTriangle)),
            restir_emissive_tri_max, nullptr, true, "emissive_triangles"
        );

        // volumetric light index list, compact list of light slot indices with the volumetric flag set
        // built each frame on the cpu in UpdateLights, scanned per pixel by the volumetric fog loop
        at(buffers, Renderer_Buffer::VolumetricLightIndices) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),
            rhi_max_array_size, nullptr, true, "volumetric_light_indices"
        );

        // particle buffers
        const uint32_t particle_max = 100000;
        uint32_t particle_counter_init[2] = { 0, 0 };
        at(buffers, Renderer_Buffer::ParticleBufferA) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_Particle)),       particle_max, nullptr,                true, "particle_buffer_a");
        at(buffers, Renderer_Buffer::ParticleCounter) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),          2,            particle_counter_init,  true, "particle_counter");
        at(buffers, Renderer_Buffer::ParticleEmitter) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_EmitterParams)),  1,            nullptr,                true, "particle_emitter");

        // gpu procedural grass, sized once and reused for the lifetime of the renderer
        // GrassInstances is the transient per-frame ring buffer that the populate shader fills
        // GrassCount is the per-lod atomic counter, cleared each frame by the populate setup
        // GrassIndirectArgs is the per-lod DrawIndexedIndirect args buffer, populated each frame by the args build shader
        // sizes are constants so the descriptors stay stable across worlds, EnableProceduralGrass just bakes per-lod offsets
        // grass uses a dedicated GrassInstance layout, 16 bytes with full float xyz, the shared PackedInstance
        // format keeps positions as half-floats and quantizes world positions to a ~1m lattice past a few hundred
        // meters from the origin, snapping every blade onto a visible grid regardless of how random the populate
        // compute scatters them. the raster vs reads this buffer via the same uav descriptor the populate compute
        // wrote, so the per-instance vertex stream slot is bound to the global geometry instance buffer instead.
        // device local since the cpu never touches it, the compute populate writes the entire content each frame
        at(buffers, Renderer_Buffer::GrassInstances) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Instance, static_cast<uint32_t>(sizeof(Sb_GrassInstance)),
            renderer_max_grass_instances, nullptr, false, "grass_instances"
        );
        at(buffers, Renderer_Buffer::GrassCount) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(uint32_t)),
            renderer_max_grass_lod_count, nullptr, true, "grass_count"
        );
        at(buffers, Renderer_Buffer::GrassIndirectArgs) = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Storage, static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs)),
            renderer_max_grass_lod_count, nullptr, true, "grass_indirect_args"
        );
    }

    void Renderer::CreateDepthStencilStates()
    {
        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        at(depth_stencil_states, Renderer_DepthStencilState::Off)              = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never);
        at(depth_stencil_states, Renderer_DepthStencilState::ReadEqual)        = make_shared<RHI_DepthStencilState>(true,  false, RHI_Comparison_Function::Equal);
        at(depth_stencil_states, Renderer_DepthStencilState::ReadGreaterEqual) = make_shared<RHI_DepthStencilState>(true,  false, RHI_Comparison_Function::GreaterEqual);
        at(depth_stencil_states, Renderer_DepthStencilState::ReadWrite)        = make_shared<RHI_DepthStencilState>(true,  true,  RHI_Comparison_Function::GreaterEqual);
    }

    void Renderer::CreateRasterizerStates()
    {
        // bias is done in the shader, hw bias is uncontrollable across cascades
        const float line_width = 3.0f;

        //                                                                                                  fill mode,             depth clip, bias, bias clamp, slope scaled bias, line width
        at(rasterizer_states, Renderer_RasterizerState::Solid)             = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     true,  0.0f, 0.0f, 0.0f, line_width);
        at(rasterizer_states, Renderer_RasterizerState::Wireframe)         = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Wireframe, true,  0.0f, 0.0f, 0.0f, line_width);
        at(rasterizer_states, Renderer_RasterizerState::Light_point_spot)  = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     true,  0.0f, 0.0f, 0.0f, line_width);
        at(rasterizer_states, Renderer_RasterizerState::Light_directional) = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     false, 0.0f, 0.0f, 0.0f, line_width);
    }

    void Renderer::CreateBlendStates()
    {
        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        at(blend_states, Renderer_BlendState::Off)      = make_shared<RHI_BlendState>(false);
        at(blend_states, Renderer_BlendState::Alpha)    = make_shared<RHI_BlendState>(true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        at(blend_states, Renderer_BlendState::Additive) = make_shared<RHI_BlendState>(true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers()
    {
        auto create_sampler = [](Renderer_Sampler type, RHI_Filter filter_min, RHI_Filter filter_mag, RHI_Filter filter_mip,
            RHI_Sampler_Address_Mode address_mode, RHI_Comparison_Function comparison_func, float anisotropy, bool comparison_enabled, float mip_bias)
        {
            at(samplers, type) = make_shared<RHI_Sampler>(filter_min, filter_mag, filter_mip, address_mode, comparison_func, anisotropy, comparison_enabled, mip_bias);
        };

        // non anisotropic
        {
            static bool samplers_created = false;
            if (!samplers_created)
            {
                create_sampler(Renderer_Sampler::Compare_depth,         RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Greater, 0.0f, true,  0.0f); // reverse-z
                create_sampler(Renderer_Sampler::Point_clamp_edge,      RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                create_sampler(Renderer_Sampler::Point_clamp_border,    RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                create_sampler(Renderer_Sampler::Point_wrap,            RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,        RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                create_sampler(Renderer_Sampler::Bilinear_clamp_edge,   RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                create_sampler(Renderer_Sampler::Bilinear_clamp_border, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                create_sampler(Renderer_Sampler::Bilinear_wrap,         RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,        RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                create_sampler(Renderer_Sampler::Trilinear_clamp,       RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);

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
                create_sampler(Renderer_Sampler::Anisotropic_wrap, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_bias);
            }
        }

        m_bindless_samplers_dirty = true;
    }

    void Renderer::UpdateOptionalRenderTargets()
    {
        uint32_t width  = static_cast<uint32_t>(GetResolutionRender().x);
        uint32_t height = static_cast<uint32_t>(GetResolutionRender().y);
        uint32_t flags  = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing;

        // ssao
        bool need_ssao = cvar_ssao.GetValueAs<bool>();
        if (need_ssao && !at(render_targets, Renderer_RenderTarget::ssao))
        {
            at(render_targets, Renderer_RenderTarget::ssao) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "ssao");
        }
        else if (!need_ssao && at(render_targets, Renderer_RenderTarget::ssao))
        {
            at(render_targets, Renderer_RenderTarget::ssao) = nullptr;
        }
        
        // ray traced reflections gbuffer, concurrent sharing so rt reflections can run on the compute queue
        bool need_rt_reflections = cvar_ray_traced_reflections.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing();
        if (need_rt_reflections && !at(render_targets, Renderer_RenderTarget::gbuffer_reflections_position))
        {
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_position) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R32G32B32A32_Float, flags, "gbuffer_reflections_position");
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_normal)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "gbuffer_reflections_normal");
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_albedo)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_reflections_albedo");
        }
        else if (!need_rt_reflections && at(render_targets, Renderer_RenderTarget::gbuffer_reflections_position))
        {
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_position) = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_normal)   = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_albedo)   = nullptr;
        }
        
        // restir, allocate or free both the reservoirs and the output ring together so the feature is fully on or fully off
        bool need_restir          = cvar_restir_pt.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing();
        float restir_scale        = cvar_restir_pt_scale.GetValue();
        static float last_restir_scale = -1.0f;
        bool restir_scale_changed = need_restir && at(render_targets, Renderer_RenderTarget::restir_reservoir0) && (last_restir_scale != restir_scale);

        auto release_restir_resources = [&]()
        {
            for_restir_reservoir_slot([&](uint32_t, Renderer_RenderTarget rt) { at(render_targets, rt) = nullptr; });
            at(render_targets, Renderer_RenderTarget::restir_output)                   = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised)                 = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_history)         = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_ping)            = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_moments)         = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_moments_history) = nullptr;
        };

        auto allocate_restir_resources = [&]()
        {
            uint32_t restir_width  = max(static_cast<uint32_t>(width * restir_scale), renderer_resolution_restir_min);
            uint32_t restir_height = max(static_cast<uint32_t>(height * restir_scale), renderer_resolution_restir_min);
            uint32_t restir_flags  = flags | RHI_Texture_ConcurrentSharing;

            static const char* reservoir_names[] =
            {
                "restir_reservoir0",         "restir_reservoir1",         "restir_reservoir2",         "restir_reservoir3",         "restir_reservoir4",         "restir_reservoir5",
                "restir_reservoir_prev0",    "restir_reservoir_prev1",    "restir_reservoir_prev2",    "restir_reservoir_prev3",    "restir_reservoir_prev4",    "restir_reservoir_prev5",
                "restir_reservoir_spatial0", "restir_reservoir_spatial1", "restir_reservoir_spatial2", "restir_reservoir_spatial3", "restir_reservoir_spatial4", "restir_reservoir_spatial5",
            };
            for_restir_reservoir_slot([&](uint32_t i, Renderer_RenderTarget rt)
            {
                at(render_targets, rt) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R32G32B32A32_Float, restir_flags, reservoir_names[i]);
            });
            at(render_targets, Renderer_RenderTarget::restir_output)                   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "restir_output");
            at(render_targets, Renderer_RenderTarget::restir_denoised)                 = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "restir_denoised");
            at(render_targets, Renderer_RenderTarget::restir_denoised_history)         = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "restir_denoised_history");
            at(render_targets, Renderer_RenderTarget::restir_denoised_ping)            = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "restir_denoised_ping");
            // svgf moments texture, packs (luma_M1, luma_M2, sample_count, unused) per pixel,
            // sampled by the spatial pass to drive luma_phi by sqrt(variance) instead of a
            // hand-tuned global scale, history copy ping pongs each frame for temporal accumulation
            at(render_targets, Renderer_RenderTarget::restir_denoised_moments)         = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "restir_denoised_moments");
            at(render_targets, Renderer_RenderTarget::restir_denoised_moments_history) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, restir_width, restir_height, 1, 1, RHI_Format::R16G16B16A16_Float, restir_flags, "restir_denoised_moments_history");

            last_restir_scale = restir_scale;
        };

        if (restir_scale_changed)
        {
            release_restir_resources();
            allocate_restir_resources();
            m_pass_state.restir_reservoirs_initialized = false;
        }
        else if (need_restir && !at(render_targets, Renderer_RenderTarget::restir_reservoir0))
        {
            allocate_restir_resources();
            m_pass_state.restir_reservoirs_initialized = false;
        }
        else if (!need_restir && at(render_targets, Renderer_RenderTarget::restir_reservoir0))
        {
            release_restir_resources();
            last_restir_scale = -1.0f;
            m_pass_state.restir_reservoirs_initialized = false;
        }
    }

    void Renderer::CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic)
    {
        // release old render targets before allocating new ones so that the gpu resources
        // (and their layout tracking entries) are fully retired first; without this, the
        // old shared_ptr destructor runs after the new texture is constructed, and if the
        // driver happens to reuse a vkimage handle the destructor's RemoveLayout call
        // erases the freshly-inserted entry for the new image
        if (create_render)
        {
            at(render_targets, Renderer_RenderTarget::frame_render)                = nullptr;
            at(render_targets, Renderer_RenderTarget::frame_render_opaque)         = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_color)               = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_normal)              = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_material)            = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_velocity)            = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_depth)               = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_previous)      = nullptr;
            at(render_targets, Renderer_RenderTarget::light_diffuse)               = nullptr;
            at(render_targets, Renderer_RenderTarget::light_specular)              = nullptr;
            at(render_targets, Renderer_RenderTarget::light_volumetric)            = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_occluders)     = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_occluders_hiz) = nullptr;
            at(render_targets, Renderer_RenderTarget::sss)                         = nullptr;
            at(render_targets, Renderer_RenderTarget::ssao)                        = nullptr;
            at(render_targets, Renderer_RenderTarget::reflections)                 = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_position)= nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_normal)  = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_reflections_albedo)  = nullptr;
            at(render_targets, Renderer_RenderTarget::ray_traced_shadows)             = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_output)                   = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised)                 = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_history)         = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_ping)            = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_moments)         = nullptr;
            at(render_targets, Renderer_RenderTarget::restir_denoised_moments_history) = nullptr;
            for_restir_reservoir_slot([](uint32_t, Renderer_RenderTarget rt) { at(render_targets, rt) = nullptr; });
            at(render_targets, Renderer_RenderTarget::shading_rate)                 = nullptr;
            at(render_targets, Renderer_RenderTarget::shadow_atlas)                 = nullptr;
            at(render_targets, Renderer_RenderTarget::debug_output)                 = nullptr;
        }
        if (create_output)
        {
            at(render_targets, Renderer_RenderTarget::frame_output)                = nullptr;
            at(render_targets, Renderer_RenderTarget::frame_output_2)              = nullptr;
            at(render_targets, Renderer_RenderTarget::taau_history)                = nullptr;
            at(render_targets, Renderer_RenderTarget::frame_output_stereo)         = nullptr;
            at(render_targets, Renderer_RenderTarget::bloom)                       = nullptr;
            at(render_targets, Renderer_RenderTarget::outline)                     = nullptr;
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_opaque_output) = nullptr;
        }

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

        // vr stereo uses 2-layer array textures for multiview rendering
        bool xr_stereo           = Xr::IsSessionRunning() && Xr::GetStereoMode();
        RHI_Texture_Type rt_type = xr_stereo ? RHI_Texture_Type::Type2DArray : RHI_Texture_Type::Type2D;
        uint32_t rt_layers       = xr_stereo ? Xr::eye_count : 1;

        // grouped builders, each owns one cohesive slice of allocations
        auto create_gbuffer = [&]()
        {
            at(render_targets, Renderer_RenderTarget::frame_render)        = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_render");
            at(render_targets, Renderer_RenderTarget::frame_render_opaque) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_render_opaque");

            // debug output sits at render resolution so debug raster passes can share gbuffer_depth for read-equal tests
            at(render_targets, Renderer_RenderTarget::debug_output) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "debug_output");

            // gbuffer, concurrent sharing so async compute (ssao, sss) can read while graphics writes
            uint32_t flags = RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing;
            at(render_targets, Renderer_RenderTarget::gbuffer_color)    = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_color");
            at(render_targets, Renderer_RenderTarget::gbuffer_normal)   = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R16G16B16A16_Float, flags, "gbuffer_normal");
            at(render_targets, Renderer_RenderTarget::gbuffer_material) = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_material");
            at(render_targets, Renderer_RenderTarget::gbuffer_velocity) = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::R16G16_Float,       flags, "gbuffer_velocity");
            at(render_targets, Renderer_RenderTarget::gbuffer_depth)    = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::D32_Float,          flags, "gbuffer_depth");
            // previous frame depth, used by restir's temporal validity gate so disocclusion is
            // tested against the actual prior depth at prev_uv instead of the current frame's
            // depth at prev_uv (the latter mistreats moving objects as disocclusion and is the
            // dominant cause of motion ghosting on the gi term)
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_previous) = make_shared<RHI_Texture>(rt_type, width_render, height_render, rt_layers, 1, RHI_Format::D32_Float, flags, "gbuffer_depth_previous");

            // hi-z occluders, amd depth format restrictions force a separate texture for uav and a manual blit
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_occluders) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv, "depth_occluders");

            // full mip chain so the cull shader can pick a level where the aabb fits in ~1-2 texels
            uint32_t hiz_mip_count = static_cast<uint32_t>(floor(log2(static_cast<float>(max(width_render, height_render))))) + 1;
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_occluders_hiz) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, hiz_mip_count, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_PerMipViews, "depth_occluders_hiz");
        };

        auto create_lighting_buffers = [&]()
        {
            uint32_t flags = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit;
            at(render_targets, Renderer_RenderTarget::light_diffuse)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_diffuse");
            at(render_targets, Renderer_RenderTarget::light_specular)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_specular");
            at(render_targets, Renderer_RenderTarget::light_volumetric) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_volumetric");

            at(render_targets, Renderer_RenderTarget::sss)                = make_shared<RHI_Texture>(RHI_Texture_Type::Type2DArray, width_render, height_render, 4, 1, RHI_Format::R16_Float,          RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "sss");
            at(render_targets, Renderer_RenderTarget::reflections)        = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "reflections");
            at(render_targets, Renderer_RenderTarget::ray_traced_shadows) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16_Float,          RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "ray_traced_shadows");
        };

        // gated by cvar_restir_pt so disabling restir frees the output ring as well as the reservoir slots managed by UpdateOptionalRenderTargets
        auto create_shadow_atlas_and_misc = [&]()
        {
            if (RHI_Device::IsSupportedVrs())
            {
                // vrs texture dimensions must match the gpu's reported texel size
                uint32_t texel_size_x = max(RHI_Device::PropertyGetMaxShadingRateTexelSizeX(), 1u);
                uint32_t texel_size_y = max(RHI_Device::PropertyGetMaxShadingRateTexelSizeY(), 1u);
                uint32_t vrs_width    = (width_render + texel_size_x - 1) / texel_size_x;
                uint32_t vrs_height   = (height_render + texel_size_y - 1) / texel_size_y;
                at(render_targets, Renderer_RenderTarget::shading_rate) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, vrs_width, vrs_height, 1, 1, RHI_Format::R8_Uint, RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_Rtv | RHI_Texture_Vrs | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "shading_rate");
            }
            at(render_targets, Renderer_RenderTarget::shadow_atlas) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, renderer_resolution_shadow_atlas, renderer_resolution_shadow_atlas, 1, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit, "shadow_atlas");
        };

        auto create_output_targets = [&]()
        {
            uint32_t mip_count = compute_mip_count(width_output, height_output, 16);
            at(render_targets, Renderer_RenderTarget::frame_output)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit | RHI_Texture_PerMipViews | RHI_Texture_ConcurrentSharing, "frame_output");
            at(render_targets, Renderer_RenderTarget::frame_output_2) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,         RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_output_2");
            at(render_targets, Renderer_RenderTarget::taau_history)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,         RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "taau_history");

            // stereo output, 2-layer array for xr swapchain blit (only when vr is active)
            if (xr_stereo)
            {
                at(render_targets, Renderer_RenderTarget::frame_output_stereo) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2DArray, width_output, height_output, Xr::eye_count, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_output_stereo");
            }

            at(render_targets, Renderer_RenderTarget::bloom)                       = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "bloom");
            at(render_targets, Renderer_RenderTarget::outline)                     = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,         RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv,         "outline");
            at(render_targets, Renderer_RenderTarget::gbuffer_depth_opaque_output) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,         RHI_Format::D32_Float,          RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit,   "depth_opaque_output");
        };

        auto create_atmosphere_luts = [&]()
        {
            at(render_targets, Renderer_RenderTarget::lut_brdf_specular)            = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, renderer_resolution_brdf_lut, renderer_resolution_brdf_lut,  1, 1, RHI_Format::R16G16_Float,       RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "lut_brdf_specular");
            at(render_targets, Renderer_RenderTarget::lut_atmosphere_scatter)       = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 256, 256, 32, 1,                                                  RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "lut_atmosphere_scatter");
            at(render_targets, Renderer_RenderTarget::lut_atmosphere_transmittance) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 256, 64,    1, 1,                                                  RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "lut_atmosphere_transmittance");
            at(render_targets, Renderer_RenderTarget::lut_atmosphere_multiscatter)  = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 32,  32,    1, 1,                                                  RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "lut_atmosphere_multiscatter");
            at(render_targets, Renderer_RenderTarget::cloud_noise)                  = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 128, 128,  128, 1,                                                RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "cloud_noise");

            at(render_targets, Renderer_RenderTarget::blur)      = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, renderer_resolution_blur_scratch, renderer_resolution_blur_scratch, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "blur_scratch");
            const uint32_t lowest_dimension                 = 16; // lowest mip is 16x16, preserving directional detail for diffuse IBL (1x1 loses directionality)
            at(render_targets, Renderer_RenderTarget::skysphere) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, renderer_resolution_skysphere_w, renderer_resolution_skysphere_h, 1, compute_mip_count(renderer_resolution_skysphere_w, renderer_resolution_skysphere_h, lowest_dimension), RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit | RHI_Texture_ConcurrentSharing, "skysphere");

            at(render_targets, Renderer_RenderTarget::auto_exposure)          = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1, 1, 1, 1, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "auto_exposure_1");
            at(render_targets, Renderer_RenderTarget::auto_exposure_previous) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1, 1, 1, 1, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "auto_exposure_2");
        };

        auto create_wind = [&]()
        {
            // wind field, baked once per frame, sampled by all wind-driven geometry
            at(render_targets, Renderer_RenderTarget::wind_field) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 256, 256, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ConcurrentSharing, "wind_field");
        };

        if (create_render)
        {
            create_gbuffer();
            create_lighting_buffers();
            UpdateOptionalRenderTargets(); // ssao, rt reflections, restir (reservoirs and output ring)
            create_shadow_atlas_and_misc();
        }

        if (create_output)
        {
            create_output_targets();
        }

        // fixed dimension targets, allocated once and reused across resolution changes
        if (!at(render_targets, Renderer_RenderTarget::lut_brdf_specular))
        {
            create_atmosphere_luts();
            create_wind();
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

        // shader compile table, set rt_only for shaders that should only compile when ray tracing is supported
        struct ShaderEntry
        {
            Renderer_Shader id;
            RHI_Shader_Type stage;
            const char*     file;
            RHI_Vertex_Type vtype   = RHI_Vertex_Type::Max;
            const char*     define  = nullptr;
            bool            async   = true;
            bool            rt_only = false;
        };

        const bool rt = RHI_Device::IsSupportedRayTracing();

        const ShaderEntry table[] =
        {
            // debug
            { Renderer_Shader::line_v,                                RHI_Shader_Type::Vertex,  "line.hlsl",                                  RHI_Vertex_Type::PosCol       },
            { Renderer_Shader::line_p,                                RHI_Shader_Type::Pixel,   "line.hlsl"                                                                  },
            { Renderer_Shader::grid_v,                                RHI_Shader_Type::Vertex,  "grid.hlsl",                                  RHI_Vertex_Type::PosUvNorTan  },
            { Renderer_Shader::grid_p,                                RHI_Shader_Type::Pixel,   "grid.hlsl"                                                                  },
            { Renderer_Shader::outline_v,                             RHI_Shader_Type::Vertex,  "outline.hlsl",                               RHI_Vertex_Type::PosUvNorTan  },
            { Renderer_Shader::outline_p,                             RHI_Shader_Type::Pixel,   "outline.hlsl"                                                               },
            { Renderer_Shader::outline_c,                             RHI_Shader_Type::Compute, "outline.hlsl"                                                               },

            // depth
            { Renderer_Shader::depth_prepass_v,                       RHI_Shader_Type::Vertex,  "depth_prepass.hlsl",                         RHI_Vertex_Type::PosUvNorTan  },
            { Renderer_Shader::depth_light_v,                         RHI_Shader_Type::Vertex,  "depth_light.hlsl",                           RHI_Vertex_Type::PosUvNorTan  },
            { Renderer_Shader::depth_light_alpha_color_p,             RHI_Shader_Type::Pixel,   "depth_light.hlsl"                                                           },

            // g-buffer
            { Renderer_Shader::gbuffer_v,                             RHI_Shader_Type::Vertex,  "g_buffer.hlsl",                              RHI_Vertex_Type::PosUvNorTan  },
            { Renderer_Shader::gbuffer_p,                             RHI_Shader_Type::Pixel,   "g_buffer.hlsl"                                                              },

            // tessellation
            { Renderer_Shader::tessellation_h,                        RHI_Shader_Type::Hull,    "common_tessellation.hlsl"                                                   },
            { Renderer_Shader::tessellation_d,                        RHI_Shader_Type::Domain,  "common_tessellation.hlsl"                                                   },

            // light
            { Renderer_Shader::light_integration_brdf_specular_lut_c, RHI_Shader_Type::Compute, "light_integration.hlsl",                     RHI_Vertex_Type::Max, "BRDF_SPECULAR_LUT", false },
            { Renderer_Shader::light_integration_environment_filter_c,RHI_Shader_Type::Compute, "light_integration.hlsl",                     RHI_Vertex_Type::Max, "ENVIRONMENT_FILTER"      },
            { Renderer_Shader::light_c,                               RHI_Shader_Type::Compute, "light.hlsl",                                 RHI_Vertex_Type::Max, rt ? "RAY_TRACING_ENABLED" : nullptr },
            { Renderer_Shader::light_cluster_assign_c,                RHI_Shader_Type::Compute, "light_cluster_assign.hlsl"                                                  },
            { Renderer_Shader::light_cluster_visualize_c,             RHI_Shader_Type::Compute, "light_cluster_visualize.hlsl"                                               },
            { Renderer_Shader::light_composition_c,                   RHI_Shader_Type::Compute, "light_composition.hlsl"                                                     },
            { Renderer_Shader::light_image_based_c,                   RHI_Shader_Type::Compute, "light_image_based.hlsl"                                                     },

            // blur
            { Renderer_Shader::blur_gaussian_c,                       RHI_Shader_Type::Compute, "blur.hlsl"                                                                  },
            { Renderer_Shader::blur_gaussian_bilateral_c,             RHI_Shader_Type::Compute, "blur.hlsl",                                  RHI_Vertex_Type::Max, "PASS_BLUR_GAUSSIAN_BILATERAL" },

            // bloom
            { Renderer_Shader::bloom_luminance_c,                     RHI_Shader_Type::Compute, "bloom.hlsl",                                 RHI_Vertex_Type::Max, "LUMINANCE"           },
            { Renderer_Shader::bloom_downsample_c,                    RHI_Shader_Type::Compute, "bloom.hlsl",                                 RHI_Vertex_Type::Max, "DOWNSAMPLE"          },
            { Renderer_Shader::bloom_upsample_blend_mip_c,            RHI_Shader_Type::Compute, "bloom.hlsl",                                 RHI_Vertex_Type::Max, "UPSAMPLE_BLEND_MIP"  },
            { Renderer_Shader::bloom_blend_frame_c,                   RHI_Shader_Type::Compute, "bloom.hlsl",                                 RHI_Vertex_Type::Max, "BLEND_FRAME"         },

            // amd fidelityfx
            { Renderer_Shader::ffx_cas_c,                             RHI_Shader_Type::Compute, "amd_fidelity_fx/cas.hlsl"                                                   },
            { Renderer_Shader::ffx_spd_average_c,                     RHI_Shader_Type::Compute, "amd_fidelity_fx/spd.hlsl",                   RHI_Vertex_Type::Max, "AVERAGE", false },
            { Renderer_Shader::ffx_spd_min_c,                         RHI_Shader_Type::Compute, "amd_fidelity_fx/spd.hlsl",                   RHI_Vertex_Type::Max, "MIN",     false },
            { Renderer_Shader::ffx_spd_max_c,                         RHI_Shader_Type::Compute, "amd_fidelity_fx/spd.hlsl",                   RHI_Vertex_Type::Max, "MAX",     false },

            // sky
            { Renderer_Shader::skysphere_c,                           RHI_Shader_Type::Compute, "sky/skysphere.hlsl"                                                         },
            { Renderer_Shader::skysphere_lut_c,                       RHI_Shader_Type::Compute, "sky/skysphere.hlsl",                         RHI_Vertex_Type::Max, "LUT"               },
            { Renderer_Shader::skysphere_transmittance_lut_c,         RHI_Shader_Type::Compute, "sky/skysphere.hlsl",                         RHI_Vertex_Type::Max, "TRANSMITTANCE_LUT", false },
            { Renderer_Shader::skysphere_multiscatter_lut_c,          RHI_Shader_Type::Compute, "sky/skysphere.hlsl",                         RHI_Vertex_Type::Max, "MULTISCATTER_LUT",  false },
            { Renderer_Shader::clouds_noise_c,                        RHI_Shader_Type::Compute, "sky/clouds.hlsl",                            RHI_Vertex_Type::Max, "CLOUD_NOISE",       false },

            // post-process
            { Renderer_Shader::fxaa_c,                                RHI_Shader_Type::Compute, "fxaa/fxaa.hlsl"                                                             },
            { Renderer_Shader::taau_c,                                RHI_Shader_Type::Compute, "taau.hlsl"                                                                  },
            { Renderer_Shader::font_v,                                RHI_Shader_Type::Vertex,  "font.hlsl",                                  RHI_Vertex_Type::PosUv         },
            { Renderer_Shader::font_p,                                RHI_Shader_Type::Pixel,   "font.hlsl"                                                                  },
            { Renderer_Shader::film_grain_c,                          RHI_Shader_Type::Compute, "film_grain.hlsl"                                                            },
            { Renderer_Shader::chromatic_aberration_c,                RHI_Shader_Type::Compute, "chromatic_aberration.hlsl"                                                  },
            { Renderer_Shader::vhs_c,                                 RHI_Shader_Type::Compute, "vhs.hlsl"                                                                   },
            { Renderer_Shader::output_c,                              RHI_Shader_Type::Compute, "output.hlsl"                                                                },
            { Renderer_Shader::motion_blur_c,                         RHI_Shader_Type::Compute, "motion_blur.hlsl"                                                           },
            { Renderer_Shader::ssao_c,                                RHI_Shader_Type::Compute, "ssao.hlsl"                                                                  },
            { Renderer_Shader::sss_c_bend,                            RHI_Shader_Type::Compute, "screen_space_shadows/bend_sss.hlsl"                                         },
            { Renderer_Shader::depth_of_field_c,                      RHI_Shader_Type::Compute, "depth_of_field.hlsl"                                                        },
            { Renderer_Shader::variable_rate_shading_c,               RHI_Shader_Type::Compute, "variable_rate_shading.hlsl"                                                 },
            { Renderer_Shader::blit_c,                                RHI_Shader_Type::Compute, "blit.hlsl"                                                                  },

            // indirect draw
            { Renderer_Shader::indirect_cull_c,                       RHI_Shader_Type::Compute, "indirect_cull.hlsl"                                                         },
            { Renderer_Shader::indirect_cull_triangle_c,              RHI_Shader_Type::Compute, "indirect_cull_triangle.hlsl"                                                },
            { Renderer_Shader::gbuffer_indirect_v,                    RHI_Shader_Type::Vertex,  "g_buffer.hlsl",                              RHI_Vertex_Type::Max, "INDIRECT_DRAW"        },
            { Renderer_Shader::gbuffer_indirect_p,                    RHI_Shader_Type::Pixel,   "g_buffer.hlsl",                              RHI_Vertex_Type::Max, "INDIRECT_DRAW"        },
            { Renderer_Shader::depth_prepass_indirect_v,              RHI_Shader_Type::Vertex,  "depth_prepass.hlsl",                         RHI_Vertex_Type::Max, "INDIRECT_DRAW"        },
            { Renderer_Shader::depth_prepass_indirect_alpha_test_p,   RHI_Shader_Type::Pixel,   "depth_prepass.hlsl",                         RHI_Vertex_Type::Max, "ALPHA_TEST_INDIRECT"  },
            { Renderer_Shader::meshlet_visualize_v,                   RHI_Shader_Type::Vertex,  "meshlet_visualize.hlsl"                                                     },
            { Renderer_Shader::meshlet_visualize_p,                   RHI_Shader_Type::Pixel,   "meshlet_visualize.hlsl"                                                     },

            // misc
            { Renderer_Shader::icon_c,                                RHI_Shader_Type::Compute, "icon.hlsl"                                                                  },
            { Renderer_Shader::dithering_c,                           RHI_Shader_Type::Compute, "dithering.hlsl"                                                             },
            { Renderer_Shader::transparency_reflection_refraction_c,  RHI_Shader_Type::Compute, "transparency_reflection_refraction.hlsl"                                    },
            { Renderer_Shader::auto_exposure_c,                       RHI_Shader_Type::Compute, "auto_exposure.hlsl"                                                         },

            // ray tracing, only compiled when supported
            { Renderer_Shader::reflections_ray_generation_r,          RHI_Shader_Type::RayGeneration, "ray_traced_reflections.hlsl",          RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::reflections_ray_miss_r,                RHI_Shader_Type::RayMiss,       "ray_traced_reflections.hlsl",          RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::reflections_ray_hit_r,                 RHI_Shader_Type::RayHit,        "ray_traced_reflections.hlsl",          RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::light_reflections_c,                   RHI_Shader_Type::Compute,       "light_reflections.hlsl",               RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::shadows_ray_generation_r,              RHI_Shader_Type::RayGeneration, "ray_traced_shadows.hlsl",              RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::shadows_ray_miss_r,                    RHI_Shader_Type::RayMiss,       "ray_traced_shadows.hlsl",              RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::shadows_ray_hit_r,                     RHI_Shader_Type::RayHit,        "ray_traced_shadows.hlsl",              RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::restir_pt_ray_generation_r,            RHI_Shader_Type::RayGeneration, "restir_pt.hlsl",                       RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::restir_pt_ray_miss_r,                  RHI_Shader_Type::RayMiss,       "restir_pt.hlsl",                       RHI_Vertex_Type::Max, "MAIN_MISS",                   true,  true },
            { Renderer_Shader::restir_pt_ray_hit_r,                   RHI_Shader_Type::RayHit,        "restir_pt.hlsl",                       RHI_Vertex_Type::Max, "MAIN_HIT",                    true,  true },
            { Renderer_Shader::restir_pt_temporal_c,                  RHI_Shader_Type::Compute,       "restir_pt_temporal.hlsl",              RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::restir_pt_spatial_c,                   RHI_Shader_Type::Compute,       "restir_pt_spatial.hlsl",               RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::restir_pt_denoise_temporal_c,          RHI_Shader_Type::Compute,       "restir_pt_denoise_temporal.hlsl",      RHI_Vertex_Type::Max, nullptr,                       true,  true },
            { Renderer_Shader::restir_pt_denoise_spatial_c,           RHI_Shader_Type::Compute,       "restir_pt_denoise_spatial.hlsl",       RHI_Vertex_Type::Max, nullptr,                       true,  true },

            // wind field
            { Renderer_Shader::wind_field_c,                          RHI_Shader_Type::Compute, "wind_field.hlsl"                                                                                    },

            // gpu-driven particles
            { Renderer_Shader::particle_emit_c,                       RHI_Shader_Type::Compute, "particles.hlsl",                             RHI_Vertex_Type::Max, "EMIT"                           },
            { Renderer_Shader::particle_simulate_c,                   RHI_Shader_Type::Compute, "particles.hlsl",                             RHI_Vertex_Type::Max, "SIMULATE"                       },
            { Renderer_Shader::particle_render_c,                     RHI_Shader_Type::Compute, "particles.hlsl",                             RHI_Vertex_Type::Max, "RENDER"                         },

            // gpu procedural grass
            { Renderer_Shader::grass_populate_c,                      RHI_Shader_Type::Compute, "grass_populate.hlsl"                                                                              },
            { Renderer_Shader::grass_indirect_args_c,                 RHI_Shader_Type::Compute, "grass_indirect_args.hlsl"                                                                         },
            { Renderer_Shader::grass_gbuffer_v,                       RHI_Shader_Type::Vertex,  "g_buffer.hlsl",                              RHI_Vertex_Type::PosUvNorTan, "GRASS_INSTANCED"        },
            { Renderer_Shader::grass_depth_prepass_v,                 RHI_Shader_Type::Vertex,  "depth_prepass.hlsl",                         RHI_Vertex_Type::PosUvNorTan, "GRASS_INSTANCED"        },

            // gpu texture compression, synchronous so encode-on-load can wait
            { Renderer_Shader::texture_compress_bc1_c,                RHI_Shader_Type::Compute, "texture_compress_bc1.hlsl",                  RHI_Vertex_Type::Max, nullptr,                         false },
            { Renderer_Shader::texture_compress_bc3_c,                RHI_Shader_Type::Compute, "texture_compress_bc3.hlsl",                  RHI_Vertex_Type::Max, nullptr,                         false },
            { Renderer_Shader::texture_compress_bc5_c,                RHI_Shader_Type::Compute, "texture_compress_bc5.hlsl",                  RHI_Vertex_Type::Max, nullptr,                         false },
        };

        for (const ShaderEntry& e : table)
        {
            if (e.rt_only && !rt)
            {
                continue;
            }
            compile_shader(e.id, e.stage, sd + e.file, e.async, e.vtype, e.define);
        }
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
        const string dir_texture = ResourceCache::GetResourceDirectory(ResourceDirectory::Textures) + "/";

        at(standard_textures, Renderer_StandardTexture::Noise_perlin) = make_shared<RHI_Texture>(dir_texture + "noise_perlin.png");
        at(standard_textures, Renderer_StandardTexture::Noise_blue)   = make_shared<RHI_Texture>(dir_texture + "noise_blue_0.png");

        // gizmos
        {
            at(standard_textures, Renderer_StandardTexture::Gizmo_light_directional) = make_shared<RHI_Texture>(dir_texture + "sun.png");
            at(standard_textures, Renderer_StandardTexture::Gizmo_light_point)       = make_shared<RHI_Texture>(dir_texture + "light_bulb.png");
            at(standard_textures, Renderer_StandardTexture::Gizmo_light_spot)        = make_shared<RHI_Texture>(dir_texture + "flashlight.png");
            at(standard_textures, Renderer_StandardTexture::Gizmo_audio_source)      = make_shared<RHI_Texture>(dir_texture + "audio.png");
        }

        // misc
        {
            at(standard_textures, Renderer_StandardTexture::Checkerboard) = make_shared<RHI_Texture>(dir_texture + "no_texture.png");
        }

        // solid 1x1 textures
        {
            auto create_solid_texture = [](const char* name, std::byte r, std::byte g, std::byte b, std::byte a)
            {
                std::vector<RHI_Texture_Mip>   mips   = { RHI_Texture_Mip{std::vector<std::byte>{r, g, b, a}} };
                std::vector<RHI_Texture_Slice> slices  = { RHI_Texture_Slice{mips} };
                return make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 1, 1, 1, 1, RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv | RHI_Texture_Uav, name, slices);
            };

            at(standard_textures, Renderer_StandardTexture::Black) = create_solid_texture("black_texture", std::byte{0},   std::byte{0},   std::byte{0},   std::byte{255});
            at(standard_textures, Renderer_StandardTexture::White) = create_solid_texture("white_texture", std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255});
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

    array<RHI_Texture*, rhi_max_array_size>& Renderer::GetBindlessMaterialTextures()
    {
        return m_bindless_textures;
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

        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawArgs)]     = fr.indirect_draw_args;
        buffers[static_cast<uint8_t>(Renderer_Buffer::IndirectDrawData)]     = fr.indirect_draw_data;
        buffers[static_cast<uint8_t>(Renderer_Buffer::MeshletInstances)]     = fr.meshlet_instances;
        buffers[static_cast<uint8_t>(Renderer_Buffer::VisibleTriangles)]     = fr.visible_triangles;
        buffers[static_cast<uint8_t>(Renderer_Buffer::TriangleDispatchArgs)] = fr.triangle_dispatch_args;
        buffers[static_cast<uint8_t>(Renderer_Buffer::CullTasks)]            = fr.cull_tasks;
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
