/*
Copyright(c) 2016-2023 Panos Karabelas

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

namespace Spartan
{
    #define debug_color Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)
    constexpr uint8_t resources_frame_lifetime = 5;
    constexpr uint8_t shader_count             = 54;

    enum class Renderer_Option : uint32_t
    {
        Debug_Aabb,
        Debug_PickingRay,
        Debug_Grid,
        Debug_ReflectionProbes,
        Debug_TransformHandle,
        Debug_SelectionOutline,
        Debug_Lights,
        Debug_PerformanceMetrics,
        Debug_Physics,
        Debug_Wireframe,
        Bloom,
        Fog,
        FogVolumetric,
        ScreenSpaceGlobalIllumination,
        ScreenSpaceShadows,
        ScreenSpaceReflections,
        MotionBlur,
        DepthOfField,
        FilmGrain,
        ChromaticAberration,
        Debanding,
        Anisotropy,
        ShadowResolution,
        Gamma,
        Exposure,
        PaperWhite,
        Antialiasing,
        Tonemapping,
        Upsampling,
        Sharpness,
        Hdr,
        Vsync,
        Max
    };

    enum class Renderer_ScreenspaceShadow : uint32_t
    {
        Disabled,
        Normal,
        Bend
    };

    enum class Renderer_Antialiasing : uint32_t
    {
        Disabled,
        Fxaa,
        Taa,
        TaaFxaa
    };

    enum class Renderer_Tonemapping : uint32_t
    {
        Amd,
        Aces,
        Reinhard,
        Uncharted2,
        Matrix,
        Disabled,
    };

    enum class Renderer_Upsampling : uint32_t
    {
        Linear,
        FSR2
    };

    enum class Renderer_BindingsCb
    {
        frame = 0
    };
    
    enum class Renderer_BindingsSrv
    {
        // g-buffer
        gbuffer_albedo            = 0,
        gbuffer_normal            = 1,
        gbuffer_material          = 2,
        gbuffer_velocity          = 3,
        gbuffer_velocity_previous = 4,
        gbuffer_depth             = 5,
    
        // lighting
        light_diffuse              = 6,
        light_diffuse_transparent  = 7,
        light_specular             = 8,
        light_specular_transparent = 9,
        light_volumetric           = 10,
    
        // light depth/color maps
        light_directional_depth = 11,
        light_directional_color = 12,
        light_point_depth       = 13,
        light_point_color       = 14,
        light_spot_depth        = 15,
        light_spot_color        = 16,
    
        // noise
        noise_normal = 17,
        noise_blue   = 18,
    
        // misc
        lutIbl           = 19,
        environment      = 20,
        ssgi             = 21,
        ssr              = 22,
        frame            = 23,
        tex              = 24,
        tex2             = 25,
        font_atlas       = 26,
        reflection_probe = 27,
        sss              = 28,

        // bindless
        materials = 29
    };

    enum class Renderer_BindingsUav
    {
        sb_materials      = 0,
        sb_lights         = 1,
        tex               = 2,
        tex2              = 3,
        tex3              = 4,
        tex_sss           = 5,
        sb_spd            = 6,
        tex_spd           = 7,
    };

    enum class Renderer_Shader : uint8_t
    {
        gbuffer_v,
        gbuffer_instanced_v,
        gbuffer_p,
        depth_prepass_v,
        depth_prepass_instanced_v,
        depth_prepass_alpha_test_p,
        depth_light_v,
        depth_light_instanced_v,
        depth_light_p,
        depth_light_alpha_test_p,
        fullscreen_triangle_v,
        quad_v,
        quad_p,
        fxaa_c,
        film_grain_c,
        motion_blur_c,
        dof_downsample_coc_c,
        dof_bokeh_c,
        dof_tent_c,
        dof_upscale_blend_c,
        chromatic_aberration_c,
        bloom_luminance_c,
        bloom_downsample_c,
        bloom_blend_frame_c,
        bloom_upsample_blend_mip_c,
        tonemapping_gamma_correction_c,
        debanding_c,
        debug_reflection_probe_v,
        debug_reflection_probe_p,
        brdf_specular_lut_c,
        light_c,
        light_composition_c,
        light_image_based_p,
        line_v,
        line_p,
        grid_p,
        outline_v,
        outline_p,
        outline_c,
        font_v,
        font_p,
        ssgi_c,
        ssr_c,
        sss_c_bend,
        skysphere_c,
        temporal_filter_c,
        blur_gaussian_c,
        blur_gaussian_bilaterial_c,
        reflection_probe_v,
        reflection_probe_p,
        ffx_cas_c,
        ffx_spd_c_average,
        ffx_spd_c_highest,
        ffx_spd_c_antiflicker
    };
    
    enum class Renderer_RenderTexture : uint8_t
    {
        gbuffer_color,
        gbuffer_normal,
        gbuffer_material,
        gbuffer_velocity,
        gbuffer_velocity_previous,
        gbuffer_depth,
        brdf_specular_lut,
        light_diffuse,
        light_diffuse_transparent,
        light_specular,
        light_specular_transparent,
        light_volumetric,
        frame_render,
        frame_render_post_process,
        frame_render_fsr2_opaque,
        frame_output,
        frame_output_2,
        dof_half,
        dof_half_2,
        ssgi,
        ssgi_filtered,
        ssr,
        sss,
        skysphere,
        bloom,
        blur,
        outline,
        hi_z,
        max
    };

    enum class Renderer_Entity
    {
        Geometry,
        GeometryInstanced,
        GeometryTransparent,
        GeometryTransparentInstanced,
        Light,
        Camera,
        ReflectionProbe,
        AudioSource
    };

    enum class Renderer_Sampler
    {
        Compare_depth,
        Point_clamp_edge,
        Point_clamp_border,
        Point_wrap,
        Bilinear_clamp,
        Bilinear_wrap,
        Trilinear_clamp,
        Anisotropic_wrap
    };

    enum class Renderer_StructuredBuffer
    {
        Spd,
        Materials,
        Lights
    };

    enum class Renderer_StandardTexture
    {
        Noise_normal,
        Noise_blue,
        White,
        Black,
        Transparent,
        Checkerboard,
        Gizmo_light_directional,
        Gizmo_light_point,
        Gizmo_light_spot,
        Gizmo_audio_source
    };

    enum class Renderer_MeshType
    {
        NotAssigned,
        Cube,
        Quad,
        Grid,
        Sphere,
        Cylinder,
        Cone,
        Custom
    };

    enum class Renderer_RasterizerState
    {
        Solid_cull_back,
        Wireframe_cull_none,
        Solid_cull_none,
        Light_point_spot,
        Light_directional
    };

    enum class Renderer_DepthStencilState
    {
        Off,
        Stencil_read,
        Depth_read_write_stencil_read,
        Depth_read,
        Depth_read_write_stencil_write
    };

    enum class Renderer_BlendState
    {
        Disabled,
        Alpha,
        Additive
    };

    enum class Renderer_DownsampleFilter
    {
        Average,
        Highest,
        Antiflicker
    };
}
