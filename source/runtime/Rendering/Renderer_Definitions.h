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
    const uint32_t renderer_resource_frame_lifetime = 100;
    const uint32_t renderer_max_draw_calls          = 20000;
    const uint32_t renderer_max_instance_count      = 1024;

    enum class Renderer_Option : uint32_t
    {
        Aabb,
        PickingRay,
        Grid,
        TransformHandle,
        SelectionOutline,
        Lights,
        AudioSources,
        PerformanceMetrics,
        Physics,
        Wireframe,
        Bloom,
        Fog,
        ScreenSpaceAmbientOcclusion,
        ScreenSpaceReflections,
        RayTracedReflections,
        MotionBlur,
        DepthOfField,
        FilmGrain,
        Vhs,
        ChromaticAberration,
        Anisotropy,
        Tonemapping,
        AntiAliasing_Upsampling,
        Sharpness,
        Dithering,
        Hdr,
        WhitePoint,
        Gamma,
        Vsync,
        VariableRateShading,
        ResolutionScale,
        DynamicResolution,
        OcclusionCulling,
        AutoExposureAdaptationSpeed,
        // volumetric clouds
        CloudCoverage, // 0=clear sky, 1=overcast
        CloudShadows,  // shadow intensity on ground
        Max
    };

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
        AA_Fxaa_Upcale_Linear,
        AA_Fsr_Upscale_Fsr,
        AA_Xess_Upscale_Xess
    };

    enum class Renderer_BindingsCb
    {
        frame
    };
    
    enum class Renderer_BindingsSrv
    {
        // g-buffer
        gbuffer_albedo   = 0,
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
        reservoir_prev0    = 22,
        reservoir_prev1    = 23,
        reservoir_prev2    = 24,
        reservoir_prev3    = 25,
        reservoir_prev4    = 26,
    };

    enum class Renderer_BindingsUav
    {
        tex           = 0,
        tex2          = 1,
        tex3          = 2,
        tex4          = 3,
        tex3d         = 4,
        tex_sss       = 5,
        visibility_unused = 6, // slot preserved to keep enum values stable
        sb_spd        = 7,
        tex_spd       = 8,
        geometry_info = 20, // ray tracing geometry info buffer
        // restir reservoir uav bindings
        reservoir0    = 21,
        reservoir1    = 22,
        reservoir2    = 23,
        reservoir3    = 24,
        reservoir4    = 25,
        // nrd output bindings
        nrd_viewz              = 26,
        nrd_normal_roughness   = 27,
        nrd_diff_radiance      = 28,
        nrd_spec_radiance      = 29,
        // integer format textures (vrs, etc)
        tex_uint               = 30,
        // gpu-driven indirect drawing
        indirect_draw_args     = 31,
        indirect_draw_data     = 32,
        indirect_draw_args_out = 33,
        indirect_draw_data_out = 34,
        indirect_draw_count    = 35,
    };

    enum class Renderer_Shader : uint8_t
    {
        tessellation_h,
        tessellation_d,
        gbuffer_v,
        gbuffer_p,
        depth_prepass_v,
        depth_prepass_alpha_test_p,
        depth_light_v,
        depth_light_alpha_color_p,
        fxaa_c,
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
        blur_gaussian_bilaterial_c,
        variable_rate_shading_c,
        ffx_cas_c,
        ffx_spd_average_c,
        ffx_spd_min_c,
        ffx_spd_max_c,
        blit_c,
        occlusion_c_unused, // slot preserved to keep enum values stable
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
        // volumetric clouds
        cloud_noise_shape_c,
        cloud_noise_detail_c,
        cloud_shadow_c,
        light_reflections_c,
        // nrd denoiser
        nrd_prepare_c,
        // gpu-driven indirect rendering
        indirect_cull_c,
        gbuffer_indirect_v,
        gbuffer_indirect_p,
        depth_prepass_indirect_v,
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
        // restir reservoir buffers (current frame)
        restir_reservoir0,
        restir_reservoir1,
        restir_reservoir2,
        restir_reservoir3,
        restir_reservoir4,
        // restir reservoir buffers (previous frame for temporal)
        restir_reservoir_prev0,
        restir_reservoir_prev1,
        restir_reservoir_prev2,
        restir_reservoir_prev3,
        restir_reservoir_prev4,
        // restir reservoir buffers (spatial ping-pong)
        restir_reservoir_spatial0,
        restir_reservoir_spatial1,
        restir_reservoir_spatial2,
        restir_reservoir_spatial3,
        restir_reservoir_spatial4,
        // volumetric clouds
        cloud_noise_shape,
        cloud_noise_detail,
        cloud_shadow,
        // nrd denoiser textures
        nrd_viewz,
        nrd_normal_roughness,
        nrd_diff_radiance_hitdist,
        nrd_spec_radiance_hitdist,
        nrd_out_diff_radiance_hitdist,
        nrd_out_spec_radiance_hitdist,
        // debug
        debug_output,
        max
    };

    enum class Renderer_Sampler
    {
        Compare_depth,
        Point_clamp_edge,
        Point_clamp_border,
        Point_wrap,
        Bilinear_clamp_edge,
        Bilienar_clamp_border,
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
        Visibility_unused,         // slot preserved to keep enum values stable
        VisibilityPrev_unused,     // slot preserved to keep enum values stable
        VisibilityReadback_unused, // slot preserved to keep enum values stable
        GeometryInfo,
        IndirectDrawArgs,
        IndirectDrawData,
        IndirectDrawDataOut,
        IndirectDrawArgsOut,
        IndirectDrawCount,
        DrawData,                  // bindless per-draw data (transforms, material index, etc.)
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

    class Renderable;
    struct Renderer_DrawCall
    {
        Renderable* renderable   = nullptr;
        uint32_t instance_index  = 0;
        uint32_t instance_count  = 0;
        uint32_t lod_index       = 0;
        uint32_t draw_data_index = 0; // index into the bindless draw data buffer
        float distance_squared   = 0.0f;
        bool is_occluder         = false;
        bool camera_visible      = false;
    };

}
