/*
Copyright(c) 2016-2022 Panos Karabelas

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
    #define DEBUG_COLOR Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)

    enum class RendererOption : uint32_t
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
        VolumetricFog,
        Ssao,
        Ssao_Gi,
        ScreenSpaceShadows,
        ScreenSpaceReflections,
        MotionBlur,
        DepthOfField,
        FilmGrain,
        ChromaticAberration,
        Debanding,
        ReverseZ,
        DepthPrepass,
        Anisotropy,
        ShadowResolution,
        Gamma,
        Fog,
        Antialiasing,
        Tonemapping,
        Upsampling,
        Sharpness,
    };

    enum class AntialiasingMode : uint32_t
    {
        Disabled,
        Fxaa,
        Taa,
        TaaFxaa
    };

    enum class TonemappingMode : uint32_t
    {
        Amd,
        Aces,
        Reinhard,
        Uncharted2,
        Matrix,
        Disabled,
    };

    enum class UpsamplingMode : uint32_t
    {
        Linear,
        FSR2
    };

    enum class RendererBindingsCb
    {
        frame    = 0,
        uber     = 1,
        light    = 2,
        material = 3,
        imgui    = 4
    };
    
    enum class RendererBindingsSrv
    {
        // Material
        material_albedo    = 0,
        material_roughness = 1,
        material_metallic  = 2,
        material_normal    = 3,
        material_height    = 4,
        material_occlusion = 5,
        material_emission  = 6,
        material_mask      = 7,
    
        // G-buffer
        gbuffer_albedo            = 8,
        gbuffer_normal            = 9,
        gbuffer_material          = 10,
        gbuffer_velocity          = 11,
        gbuffer_velocity_previous = 12,
        gbuffer_depth             = 13,
    
        // Lighting
        light_diffuse              = 14,
        light_diffuse_transparent  = 15,
        light_specular             = 16,
        light_specular_transparent = 17,
        light_volumetric           = 19,
    
        // Light depth/color maps
        light_directional_depth = 19,
        light_directional_color = 20,
        light_point_depth       = 21,
        light_point_color       = 22,
        light_spot_depth        = 23,
        light_spot_color        = 24,
    
        // Noise
        noise_normal = 25,
        noise_blue   = 26,
    
        // Misc
        lutIbl           = 27,
        environment      = 28,
        ssao             = 29,
        ssao_gi          = 30,
        ssr              = 31,
        frame            = 32,
        tex              = 33,
        tex2             = 34,
        font_atlas       = 35,
        reflection_probe = 36
    };

    enum class RendererBindingsUav
    {
        tex            = 0,
        tex2           = 1,
        tex3           = 2,
        atomic_counter = 3,
        tex_array      = 4
    };

    enum class RendererShader : uint8_t
    {
        gbuffer_v,
        gbuffer_p,
        depth_prepass_v,
        depth_prepass_p,
        depth_light_V,
        depth_light_p,
        fullscreen_triangle_v,
        quad_v,
        copy_point_c,
        copy_bilinear_c,
        copy_point_p,
        copy_bilinear_p,
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
        lines_v,
        lines_p,
        font_v,
        font_p,
        ssao_c,
        ssr_c,
        entity_v,
        entity_transform_p,
        blur_gaussian_c,
        blur_gaussian_bilaterial_c,
        entity_outline_p,
        reflection_probe_v,
        reflection_probe_p,
        ffx_cas_c,
        ffx_spd_c
    };
    
    enum class RendererTexture : uint8_t
    {
        undefined,
        gbuffer_albedo,
        gbuffer_normal,
        gbuffer_material,
        gbuffer_velocity,
        gbuffer_depth,
        brdf_specular_lut,
        light_diffuse,
        light_diffuse_transparent,
        light_specular,
        light_specular_transparent,
        light_volumetric,
        frame_render,
        frame_render_2,
        frame_output,
        frame_output_2,
        dof_half,
        dof_half_2,
        ssao,
        ssao_gi,
        ssr,
        bloom,
        blur,
        fsr2_mask_reactive,
        fsr2_mask_transparency
    };
    
    enum class RendererEntityType
    {
        GeometryOpaque,
        GeometryTransparent,
        Light,
        Camera,
        ReflectionProbe
    };
}
