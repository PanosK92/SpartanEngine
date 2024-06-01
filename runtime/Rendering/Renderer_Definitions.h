/*
Copyright(c) 2016-2024 Panos Karabelas

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
    // we are using double buffering so 5 is enough
    constexpr uint8_t resources_frame_lifetime = 5;

    enum class Renderer_Option : uint32_t
    {
        Aabb,
        PickingRay,
        Grid,
        TransformHandle,
        SelectionOutline,
        Lights,
        PerformanceMetrics,
        Physics,
        Wireframe,
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
        Anisotropy,
        ShadowResolution,
        Exposure,
        WhitePoint,
        Antialiasing,
        Tonemapping,
        Upsampling,
        Sharpness,
        Hdr,
        Vsync,
        VariableRateShading,
        ResolutionScale,
        DynamicResolution,
        OcclusionCulling,
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
        Aces,
        NautilusACES,
        Reinhard,
        Uncharted2,
        Matrix,
        Max,
    };

    enum class Renderer_Upsampling : uint32_t
    {
        Linear,
        Fsr2
    };

    enum class Renderer_BindingsCb
    {
        frame
    };
    
    enum class Renderer_BindingsSrv
    {
        // g-buffer
        gbuffer_albedo         = 0,
        gbuffer_normal         = 1,
        gbuffer_material       = 2,
        gbuffer_velocity       = 3,
        gbuffer_depth          = 4,
        gbuffer_depth_backface = 5,
        gbuffer_depth_opaque   = 6,

        // lighting
        light_diffuse              = 7,
        light_diffuse_transparent  = 8,
        light_specular             = 9,
        light_specular_transparent = 10,
        light_volumetric           = 11,
    
        // light depth/color maps
        light_depth = 12,
        light_color = 13,
    
        // noise
        noise_normal = 14,
        noise_blue   = 15,
    
        // misc
        lutIbl           = 16,
        environment      = 17,
        ssgi             = 18,
        ssr              = 19,
        frame            = 20,
        tex              = 21,
        tex2             = 22,
        font_atlas       = 23,
        reflection_probe = 24,
        sss              = 25,

        // bindless
        materials = 26
    };

    enum class Renderer_BindingsUav
    {
        sb_materials      = 0,
        sb_lights         = 1,
        tex               = 2,
        tex2              = 3,
        tex3              = 4,
        tex_uint          = 5,
        tex_sss           = 6,
        sb_spd            = 7,
        tex_spd           = 8,
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
        quad_v,
        quad_p,
        fxaa_c,
        film_grain_c,
        motion_blur_c,
        depth_of_field_c,
        chromatic_aberration_c,
        bloom_luminance_c,
        bloom_downsample_c,
        bloom_blend_frame_c,
        bloom_upsample_blend_mip_c,
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
        ssgi_c,
        ssr_c,
        sss_c_bend,
        skysphere_c,
        blur_gaussian_c,
        blur_gaussian_bilaterial_c,
        blur_gaussian_bilaterial_radius_from_texture_c,
        antiflicker_c,
        variable_rate_shading_c,
        ffx_cas_c,
        ffx_spd_average_c,
        ffx_spd_highest_c,
        ffx_spd_antiflicker_c,
        max
    };
    
    enum class Renderer_RenderTarget : uint8_t
    {
        gbuffer_color,
        gbuffer_normal,
        gbuffer_material,
        gbuffer_velocity,
        gbuffer_depth,
        gbuffer_depth_opaque,
        gbuffer_depth_backface,
        gbuffer_depth_output,
        brdf_specular_lut,
        light_diffuse,
        light_diffuse_transparent,
        light_specular,
        light_specular_transparent,
        light_volumetric,
        frame_render,
        frame_render_2,
        frame_render_opaque,
        frame_output,
        frame_output_2,
        ssgi,
        ssr,
        ssr_roughness,
        sss,
        skysphere,
        bloom,
        blur,
        antiflicker,
        outline,
        shading_rate,
        max
    };

    enum class Renderer_Entity
    {
        Mesh,
        Light,
        Camera,
        AudioSource
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
        Checkerboard,
        Gizmo_light_directional,
        Gizmo_light_point,
        Gizmo_light_spot,
        Gizmo_audio_source,
        Foam
    };

    enum class Renderer_RasterizerState
    {
        Solid,
        Wireframe,
        Light_point_spot,
        Light_directional
    };

    enum class Renderer_DepthStencilState
    {
        Off,
        Read,
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
        Average,
        Highest,
        Antiflicker
    };
}
