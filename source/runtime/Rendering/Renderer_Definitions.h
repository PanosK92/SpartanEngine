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

#pragma once

//= INCLUDES =====
#include <cstdint>
//================

namespace spartan
{
    const uint32_t renderer_max_draw_calls         = 20000;
    const uint32_t renderer_max_instance_count     = 1024;
    const uint32_t renderer_draw_data_buffer_count = 4;       // matches command list pool size, avoids cpu-gpu memcpy races
    const uint32_t renderer_max_indirect_draws     = 131072;  // per-renderable lod draw data, cull shader clamps writes
    const uint32_t renderer_max_cull_tasks         = 524288;  // per (renderable, meshlet) cull tasks, drives meshlet cull dispatch size
    const uint32_t renderer_max_meshlet_instances  = 1048576; // meshlet cull survivor list, hw instancing fans out so can exceed renderer_max_cull_tasks
    const uint32_t renderer_max_visible_triangles  = 8388608; // triangle cull survivor list, worst case practical cap

    // render target dimensions, fixed allocations sized for current quality budgets
    const uint32_t renderer_resolution_shadow_atlas = 8192; // total shadow atlas, packed by row of square slices
    const uint32_t renderer_resolution_blur_scratch = 4096; // ping pong target reused by every blur pass
    const uint32_t renderer_resolution_skysphere_w  = 4096; // skysphere panorama width
    const uint32_t renderer_resolution_skysphere_h  = 2048; // skysphere panorama height
    const uint32_t renderer_resolution_brdf_lut     = 512;  // pre integrated brdf lookup table
    const uint32_t renderer_resolution_restir_min   = 64;   // minimum restir output dimension regardless of scale

    enum class Renderer_Tonemapping : uint32_t
    {
        Aces,
        AgX,
        Reinhard,
        AcesNautilus,
        GranTurismo7,
        Max,
    };

    enum class Renderer_AntiAliasing_Upsampling : uint32_t
    {
        AA_Off_Upscale_Linear,
        AA_Fxaa_Upscale_Linear,
        AA_Taau_Upscale_Taau,
        AA_Xess_Upscale_Xess
    };

    enum class Renderer_BindingsCb
    {
        frame
    };
    
    enum class Renderer_BindingsSrv
    {
        // g-buffer
        gbuffer_color    = 0,
        gbuffer_normal   = 1,
        gbuffer_material = 2,
        gbuffer_velocity = 3,
        gbuffer_depth    = 4,

        // ray-tracing
        tlas = 5,

        // other
        ssao = 6,
    
        // misc
        tex   = 7,
        tex2  = 8,
        tex3  = 9,
        tex4  = 10,
        tex5  = 11,
        tex6  = 12,
        tex3d = 13,

        // noise
        tex_perlin = 14,

        // bindless
        bindless_material_textures   = 15,
        bindless_material_parameters = 16,
        bindless_light_parameters    = 17,
        bindless_aabbs               = 18,
        bindless_draw_data           = 19,
        
        // volumetric clouds 3D noise
        tex3d_cloud_shape  = 20,
        tex3d_cloud_detail = 21,
        // restir reservoir srv bindings (for temporal/spatial read)
        // kept contiguous so a single loop can bind all six slots starting from reservoir_prev0
        reservoir_prev0    = 22,
        reservoir_prev1    = 23,
        reservoir_prev2    = 24,
        reservoir_prev3    = 25,
        reservoir_prev4    = 26,
        reservoir_prev5    = 27,

        // baked wind field, sampled by all wind-driven geometry
        tex_wind_field     = 29,
    };

    enum class Renderer_BindingsUav
    {
        tex           = 0,
        tex2          = 1,
        tex3          = 2,
        tex4          = 3,
        tex3d         = 4,
        tex_sss       = 5,
        sb_spd        = 7,
        tex_spd       = 8,
        geometry_info = 20, // ray tracing geometry info buffer
        // restir reservoir uav bindings
        reservoir0    = 21,
        reservoir1    = 22,
        reservoir2    = 23,
        reservoir3    = 24,
        reservoir4    = 25,
        reservoir5    = 26,
        // integer format textures (vrs, etc)
        tex_uint               = 30,
        // gpu-driven indirect drawing
        // indirect_draw_args is a single-slot args buffer for the final non-indexed indirect draw, vertex_count is bumped by triangle cull
        // meshlet_instances holds the meshlet-cull survivors, the triangle cull dispatches one workgroup per entry
        // visible_triangles holds packed (meshlet_instance, triangle_in_meshlet) tuples emitted by triangle cull
        // triangle_dispatch_args is the indirect dispatch args buffer for the triangle cull pass
        indirect_draw_args     = 31,
        indirect_draw_data     = 32,
        meshlet_instances      = 33,
        visible_triangles      = 34,
        triangle_dispatch_args = 35,
        // gpu-driven particles
        particle_buffer_a      = 36,
        particle_counter       = 38,
        particle_emitter       = 39,
        // gpu texture compression
        compress_input         = 40,
        compress_output        = 41,
        compress_output_bc1    = 42,
        // per-meshlet bounds for gpu-driven culling
        meshlet_bounds         = 43,
        // per-instance cull tasks for gpu-driven culling
        cull_tasks             = 44,
        // clustered lighting, grid is uint2 (first_index, count), indices is a flat uint list
        cluster_light_grid     = 45,
        cluster_light_indices  = 46,
        // cluster stats and compacted volumetric light index list
        cluster_stats          = 47,
        volumetric_light_indices = 48,
    };

    enum class Renderer_Shader : uint8_t
    {
        tessellation_h,
        tessellation_d,
        gbuffer_v,
        gbuffer_p,
        depth_prepass_v,
        depth_prepass_indirect_alpha_test_p,
        depth_light_v,
        depth_light_alpha_color_p,
        fxaa_c,
        taau_c,
        film_grain_c,
        motion_blur_c,
        depth_of_field_c,
        chromatic_aberration_c,
        vhs_c,
        bloom_luminance_c,
        bloom_blend_frame_c,
        bloom_upsample_blend_mip_c,
        bloom_downsample_c,
        output_c,
        light_integration_brdf_specular_lut_c,
        light_integration_environment_filter_c,
        light_c,
        light_cluster_assign_c,
        light_cluster_visualize_c,
        light_composition_c,
        light_image_based_c,
        line_v,
        line_p,
        grid_v,
        grid_p,
        outline_v,
        outline_p,
        outline_c,
        font_v,
        font_p,
        ssao_c,
        sss_c_bend,
        skysphere_c,
        skysphere_lut_c,
        skysphere_transmittance_lut_c,
        skysphere_multiscatter_lut_c,
        blur_gaussian_c,
        blur_gaussian_bilateral_c,
        variable_rate_shading_c,
        ffx_cas_c,
        ffx_spd_average_c,
        ffx_spd_min_c,
        ffx_spd_max_c,
        blit_c,
        icon_c,
        dithering_c,
        transparency_reflection_refraction_c,
        auto_exposure_c,
        reflections_ray_generation_r,
        reflections_ray_miss_r,
        reflections_ray_hit_r,
        // ray traced shadows
        shadows_ray_generation_r,
        shadows_ray_miss_r,
        shadows_ray_hit_r,
        // restir path tracing gi
        restir_pt_ray_generation_r,
        restir_pt_ray_miss_r,
        restir_pt_ray_hit_r,
        restir_pt_temporal_c,
        restir_pt_spatial_c,
        restir_pt_denoise_temporal_c,
        restir_pt_denoise_spatial_c,
        restir_pt_debug_c,
        // volumetric clouds
        cloud_noise_shape_c,
        cloud_noise_detail_c,
        cloud_shadow_c,
        // baked wind field
        wind_field_c,
        light_reflections_c,
        // gpu-driven indirect rendering
        indirect_cull_c,
        indirect_cull_triangle_c,
        gbuffer_indirect_v,
        gbuffer_indirect_p,
        depth_prepass_indirect_v,
        meshlet_visualize_v,
        meshlet_visualize_p,
        // gpu-driven particles
        particle_emit_c,
        particle_simulate_c,
        particle_render_c,
        // gpu texture compression
        texture_compress_bc1_c,
        texture_compress_bc3_c,
        texture_compress_bc5_c,
        max
    };
    
    enum class Renderer_RenderTarget : uint8_t
    {
        gbuffer_color,
        gbuffer_normal,
        gbuffer_material,
        gbuffer_velocity,
        gbuffer_depth,
        gbuffer_depth_occluders,
        gbuffer_depth_occluders_hiz,
        gbuffer_depth_opaque_output,
        lut_brdf_specular,
        lut_atmosphere_scatter,
        lut_atmosphere_transmittance,
        lut_atmosphere_multiscatter,
        light_diffuse,
        light_specular,
        light_volumetric,
        frame_render,
        frame_render_opaque,
        frame_output,
        frame_output_2,
        taau_history,
        ssao,
        reflections,
        gbuffer_reflections_position,
        gbuffer_reflections_normal,
        gbuffer_reflections_albedo,
        sss,
        skysphere,
        bloom,
        blur,
        outline,
        shading_rate,
        shadow_atlas,
        auto_exposure,
        auto_exposure_previous,
        // ray traced shadows
        ray_traced_shadows,
        // restir path tracing output
        restir_output,
        restir_denoised,
        restir_denoised_history,
        restir_denoised_ping,
        // restir reservoir buffers (current frame)
        restir_reservoir0,
        restir_reservoir1,
        restir_reservoir2,
        restir_reservoir3,
        restir_reservoir4,
        restir_reservoir5,
        // restir reservoir buffers (previous frame for temporal)
        restir_reservoir_prev0,
        restir_reservoir_prev1,
        restir_reservoir_prev2,
        restir_reservoir_prev3,
        restir_reservoir_prev4,
        restir_reservoir_prev5,
        // restir reservoir buffers (spatial ping-pong)
        restir_reservoir_spatial0,
        restir_reservoir_spatial1,
        restir_reservoir_spatial2,
        restir_reservoir_spatial3,
        restir_reservoir_spatial4,
        restir_reservoir_spatial5,
        // volumetric clouds
        cloud_noise_shape,
        cloud_noise_detail,
        cloud_shadow,
        // baked wind field, written each frame, sampled by depth_prepass/g_buffer/depth_light
        wind_field,
        // debug
        debug_output,
        // vr stereo
        frame_output_stereo,
        max
    };

    enum class Renderer_Sampler
    {
        Compare_depth,
        Point_clamp_edge,
        Point_clamp_border,
        Point_wrap,
        Bilinear_clamp_edge,
        Bilinear_clamp_border,
        Bilinear_wrap,
        Trilinear_clamp,
        Anisotropic_wrap,
        Max
    };

    enum class Renderer_Buffer
    {
        ConstantFrame,
        SpdCounter,
        MaterialParameters,
        LightParameters,
        DummyInstance,
        AABBs,
        GeometryInfo,
        IndirectDrawArgs,          // single-slot args buffer for the final non-indexed indirect draw
        IndirectDrawData,          // per-renderable lod draw data
        MeshletInstances,          // meshlet-cull survivor list, the triangle cull pass dispatches one workgroup per entry
        VisibleTriangles,          // triangle-cull survivor list, one packed (meshlet_instance, triangle_in_meshlet) per entry
        TriangleDispatchArgs,      // single-slot indirect dispatch args buffer driving the triangle cull pass
        CullTasks,                 // per (renderable, meshlet) cull tasks consumed by the meshlet cull compute shader
        DrawData,                  // bindless per-draw data (transforms, material index, etc.)
        // clustered lighting
        ClusterLightGrid,          // one uint2 per cluster: (first_index, count) into ClusterLightIndices
        ClusterLightIndices,       // flat list of light indices, sliced by cluster in chunks of CLUSTER_MAX_LIGHTS
        ClusterStats,              // tiny stats buffer for the cluster assign pass (overflow counter)
        VolumetricLightIndices,    // compact list of volumetric light indices, built on cpu each frame
        // gpu-driven particles
        ParticleBufferA,
        ParticleCounter,
        ParticleEmitter,
        Max
    };

    enum class Renderer_StandardTexture
    {
        Noise_perlin,
        Noise_blue, // single blue noise texture (was 8, only 1 used)
        Checkerboard,
        Gizmo_light_directional,
        Gizmo_light_point,
        Gizmo_light_spot,
        Gizmo_audio_source,
        Black,
        White,
        Max
    };

    enum class Renderer_RasterizerState
    {
        Solid,
        Wireframe,
        Light_point_spot,
        Light_directional,
        Max
    };

    enum class Renderer_DepthStencilState
    {
        Off,
        ReadEqual,
        ReadGreaterEqual,
        ReadWrite,
        Max
    };

    enum class Renderer_BlendState
    {
        Off,
        Alpha,
        Additive
    };

    enum class Renderer_DownsampleFilter
    {
        Min,
        Max,
        Average
    };

    class Render;
    struct Renderer_DrawCall
    {
        Render* renderable   = nullptr;
        uint32_t instance_index  = 0;
        uint32_t instance_count  = 0;
        uint32_t lod_index       = 0;
        uint32_t draw_data_index = 0; // index into the bindless draw data buffer
        float distance_squared   = 0.0f;
        bool is_occluder         = false;
        bool camera_visible      = false;
    };

}
