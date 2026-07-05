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
#include <array>
#include <cstdint>
//================

namespace spartan
{
    const uint32_t renderer_max_draw_calls         = 20000;
    const uint32_t renderer_max_instance_count     = 1024;
    const uint32_t renderer_editor_icon_size_px    = 48;
    // hard cap on the restir nee pool, the cpu walker stops appending once this many emissive
    // triangles have been recorded so worst case upload size is bounded at 80 * 16384 = 1.25 mb
    const uint32_t restir_emissive_tri_max         = 16384;
    const uint32_t renderer_draw_data_buffer_count = 4;       // matches command list pool size, avoids cpu-gpu memcpy races
    const uint32_t renderer_max_indirect_draws     = 131072;  // per-renderable lod draw data, cull shader clamps writes
    // per (renderable, instance) cull tasks, drives the phase a instance cull dispatch size and caps the phase a survivor list
    // this is one task per instance now (not per meshlet x instance), so the budget scales with instance count, not geometry density,
    // a consolidated world-spanning forest entity costs one task per tree here instead of meshlets x trees
    const uint32_t renderer_max_cull_tasks         = 8 * 1024 * 1024;
    // meshlet cull survivor list (phase b output), bounded by the visible meshlets of the phase a survivors
    // 4M is ample once phase a frustum + hi-z + distance culls out-of-view and out-of-range instances before meshlet expansion
    const uint32_t renderer_max_meshlet_instances  = 4 * 1024 * 1024;
    // triangle cull survivor list, the cull pass packs (meshlet_instance_idx, triangle_idx) into a uint per visible triangle
    // sized to absorb the burst of dense foliage + terrain meshlets without throttling, when this overflows the wave-atomic
    // race silently drops late triangles which manifests as distant terrain rendering only a few meshlets at a time
    const uint32_t renderer_max_visible_triangles  = 32 * 1024 * 1024;
    // the triangle cull partitions the one visible-triangle buffer into two equal halves, opaque survivors
    // grow from offset 0, alpha-tested survivors grow from this midpoint, the split lets the depth prepass
    // draw the opaque half with no pixel shader (double-speed z) and run the alpha-test ps only on the alpha half
    // both halves draw with first_vertex 0 and a base offset pushed in the constant, so the addressing is api agnostic
    const uint32_t renderer_visible_triangles_half = renderer_max_visible_triangles / 2;

    // gpu procedural grass
    // per-lod hard cap on the number of blades the populate shader is allowed to emit, the visible
    // density inside a ring is cap_per_lod / ring_area so the caps are tuned per lod independently:
    //  - lod 0 is the close ring, blades are big and individually readable, dense but not insane
    //  - lod 1 and lod 2 are the mid/far rings, the eye sees them at a shallow angle so they need
    //    higher cell-density to hide the lod transition, but the depth prepass pays vs+setup cost
    //    per blade and the close ring dominates the on-screen pixel count anyway, so the boost vs
    //    lod 0 stays modest, the segment counts in Game.cpp pair with these (6/3/1 segments)
    // the buffer is one contiguous block of GrassInstance entries (16 bytes each), each lod gets a
    // dedicated slot at a cumulative offset, see renderer_grass_lod_base() for the offset calc
    const uint32_t renderer_max_grass_lod_count                                              = 3;
    constexpr std::array<uint32_t, renderer_max_grass_lod_count> renderer_max_grass_per_lod  = { 384u * 1024u, 512u * 1024u, 512u * 1024u };
    const uint32_t renderer_max_grass_instances                                              = renderer_max_grass_per_lod[0] + renderer_max_grass_per_lod[1] + renderer_max_grass_per_lod[2];
    // ~1.4m instances * 16 bytes = ~22 mb, well within budget for a 500 m far ring
    // cumulative prefix sum of the per-lod caps, the populate shader writes into [base, base + cap)
    // and the raster reads with the same base via sv_instanceid + base in the push constant
    constexpr uint32_t renderer_grass_lod_base(uint32_t lod)
    {
        uint32_t base = 0u;
        for (uint32_t i = 0u; i < lod; i++)
        {
            base += renderer_max_grass_per_lod[i];
        }
        return base;
    }

    // fft ocean, square spectrum resolution and the max number of cascades packed as array slices
    const uint32_t renderer_ocean_resolution         = 512;
    const uint32_t renderer_ocean_max_cascades       = 4;
    const uint32_t renderer_ocean_heights_resolution = renderer_ocean_resolution / 4; // cpu readback for buoyancy, coarser than the render cascades

    // render target dimensions, fixed allocations sized for current quality budgets
    const uint32_t renderer_resolution_shadow_atlas = 8192; // total shadow atlas, packed by row of square slices
    const uint32_t renderer_resolution_blur_scratch = 4096; // ping pong target reused by every blur pass
    const uint32_t renderer_resolution_skysphere_w  = 4096; // skysphere panorama width
    const uint32_t renderer_resolution_skysphere_h  = 2048; // skysphere panorama height
    const uint32_t renderer_resolution_brdf_lut     = 512;  // pre integrated brdf lookup table
    const uint32_t renderer_resolution_restir_min   = 64;   // minimum restir output dimension regardless of scale
    const uint32_t renderer_particle_volume_width   = 160;
    const uint32_t renderer_particle_volume_height  = 90;
    const uint32_t renderer_particle_volume_depth   = 64;

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

        // fft ocean, displacement and normal/foam cascades sampled by water geometry
        ocean_displacement = 30,
        ocean_normal       = 31,
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
        // fft ocean compute targets (texture2d arrays, one slice per cascade)
        ocean_spectrum     = 9,  // persistent h0 spectrum
        ocean_fft_a        = 10, // working: height + x displacement
        ocean_fft_b        = 11, // working: z displacement
        ocean_displacement = 12, // assembled displacement
        ocean_normal       = 13, // assembled slope + foam
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
        // two-phase culling: phase a survivor list + its indirect dispatch args
        surviving_instances    = 37,
        instance_dispatch_args = 55,
        // clustered lighting, grid is uint2 (first_index, count), indices is a flat uint list
        cluster_light_grid     = 45,
        cluster_light_indices  = 46,
        // cluster stats and compacted volumetric light index list
        cluster_stats          = 47,
        volumetric_light_indices = 48,
        // restir path tracing nee pool, world space emissive triangles built each frame on the
        // cpu by Renderer::BuildEmissiveTriangleNeePool, declared rw to match the engine pattern
        // for per-pass structured buffers (cull_tasks, meshlet_bounds, etc.) even though the
        // shader treats it read-only
        emissive_triangles     = 49,
        // gpu procedural grass, transient ring buffer + per-lod atomic counter + indirect draw args
        // populate compute writes grass_instances and bumps grass_count, the args compute reads
        // grass_count and writes grass_indirect_args (one entry per lod), the raster passes
        // read grass_instances using sv_instanceid plus the per-draw lod_base in the push constant
        grass_instances        = 50,
        grass_count            = 51,
        grass_indirect_args    = 52,
        particle_volume_density = 53,
        particle_volume_color   = 54,
        // fft ocean vertical displacement per texel, host visible so the cpu can sample wave height for buoyancy
        ocean_heights          = 56,
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
        skysphere_transmittance_lut_c,
        skysphere_multiscatter_lut_c,
        skysphere_sky_view_lut_c,
        clouds_noise_c,
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
        auto_exposure_c,
        // reflections
        reflections_ray_generation_r,
        reflections_ray_miss_r,
        reflections_ray_hit_r,
        reflections_shade_c,
        reflections_denoise_temporal_c,
        reflections_denoise_spatial_c,
        reflections_apply_c,
        // ray traced shadows
        shadows_ray_generation_r,
        shadows_ray_miss_r,
        shadows_ray_hit_r,
        shadows_denoise_temporal_c,
        shadows_denoise_spatial_c,
        // restir path tracing gi
        restir_pt_ray_generation_r,
        restir_pt_ray_miss_r,
        restir_pt_ray_hit_r,
        restir_pt_temporal_c,
        restir_pt_spatial_c,
        restir_pt_duplication_c,
        restir_pt_denoise_temporal_c,
        restir_pt_denoise_spatial_c,
        // baked wind field
        wind_field_c,
        // fft ocean
        ocean_spectrum_init_c,
        ocean_spectrum_update_c,
        ocean_fft_horizontal_c,
        ocean_fft_vertical_c,
        ocean_assemble_c,
        // gpu-driven indirect rendering
        instance_cull_c,
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
        particle_render_v,
        particle_render_p,
        particle_volume_clear_c,
        particle_volume_splat_c,
        particle_volume_resolve_c,
        particle_volume_composite_c,
        // gpu procedural grass
        grass_populate_c,
        grass_indirect_args_c,
        grass_gbuffer_v,
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
        gbuffer_normal_previous,
        gbuffer_material,
        gbuffer_velocity,
        gbuffer_depth,
        gbuffer_depth_previous,
        gbuffer_depth_occluders,
        gbuffer_depth_occluders_hiz,
        gbuffer_depth_opaque_output,
        lut_brdf_specular,
        lut_atmosphere_transmittance,
        lut_atmosphere_multiscatter,
        lut_sky_view,
        cloud_noise,
        light_diffuse,
        light_specular,
        light_volumetric,
        particle_volume,
        frame_render,
        frame_render_opaque,
        frame_output,
        frame_output_2,
        screenshot_sdr,
        screenshot_sdr_2,
        taau_history,
        ssao,
        reflections,
        gbuffer_reflections_position,
        gbuffer_reflections_normal,
        gbuffer_reflections_albedo,
        reflections_history,
        reflections_moments,
        reflections_moments_history,
        reflections_ping,
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
        ray_traced_shadows_history,
        ray_traced_shadows_moments,
        ray_traced_shadows_moments_history,
        ray_traced_shadows_ping,
        // restir path tracing output
        restir_output,
        // sample duplication score map driving the adaptive temporal confidence cap, lin 2026
        restir_duplication,
        restir_denoised,
        restir_denoised_history,
        restir_denoised_ping,
        restir_denoised_moments,
        restir_denoised_moments_history,
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
        // baked wind field, written each frame, sampled by depth_prepass/g_buffer/depth_light
        wind_field,
        // fft ocean, texture2d arrays with one slice per cascade
        ocean_spectrum,      // persistent h0 spectrum
        ocean_fft_a,         // working: height + x displacement
        ocean_fft_b,         // working: z displacement
        ocean_displacement,  // assembled displacement, sampled by water vs
        ocean_normal,        // assembled slope + foam, sampled by water ps
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
        CullTasks,                 // per (renderable, instance) cull tasks consumed by the instance cull compute shader (phase a)
        SurvivingInstances,        // phase a survivor list, phase b dispatches one workgroup per entry
        InstanceDispatchArgs,      // single-slot indirect dispatch args buffer driving the meshlet cull pass (phase b)
        DrawData,                  // bindless per-draw data (transforms, material index, etc.)
        // clustered lighting
        ClusterLightGrid,          // one uint2 per cluster: (first_index, count) into ClusterLightIndices
        ClusterLightIndices,       // flat list of light indices, sliced by cluster in chunks of CLUSTER_MAX_LIGHTS
        ClusterStats,              // tiny stats buffer for the cluster assign pass (overflow counter)
        VolumetricLightIndices,    // compact list of volumetric light indices, built on cpu each frame
        OceanHeights,              // fft ocean vertical displacement per cascade texel, host visible for cpu buoyancy queries
        // restir path tracing emissive triangle nee pool, rebuilt each frame from renderables with non-zero emission
        EmissiveTriangles,
        // gpu-driven particles
        ParticleBufferA,
        ParticleCounter,
        ParticleEmitter,
        ParticleVolumeDensity,
        ParticleVolumeColor,
        // gpu procedural grass, allocated lazily by Renderer::EnableProceduralGrass
        // GrassInstances is the transient ring buffer of GrassInstance entries (full float xyz)
        // GrassCount holds one uint per lod, bumped atomically by the populate shader
        // GrassIndirectArgs holds one DrawIndexedIndirect entry per lod, written by the args build shader
        GrassInstances,
        GrassCount,
        GrassIndirectArgs,
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
        Gizmo_particle,
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
        Premultiplied,
        Additive,
        Max
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
