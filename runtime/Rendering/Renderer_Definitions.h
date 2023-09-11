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
    #define DEBUG_COLOR Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)

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
        VolumetricFog,
        Ssgi,
        ScreenSpaceShadows,
        ScreenSpaceReflections,
        MotionBlur,
        DepthOfField,
        FilmGrain,
        ChromaticAberration,
        Debanding,
        DepthPrepass,
        Anisotropy,
        ShadowResolution,
        Gamma,
        Exposure,
        PaperWhite,
        FogDensity,
        Antialiasing,
        Tonemapping,
        Upsampling,
        UpsamplingSharpness,
        Sharpness,
        Hdr,
        Vsync
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
        frame    = 0,
        light    = 1,
        material = 2
    };
    
    enum class Renderer_BindingsSrv
    {
        // Material
        material_albedo    = 0,
        material_albedo2   = 1,
        material_roughness = 2,
        material_metallic  = 3,
        material_normal    = 4,
        material_height    = 5,
        material_occlusion = 6,
        material_emission  = 7,
        material_mask      = 8,
    
        // G-buffer
        gbuffer_albedo            = 9,
        gbuffer_normal            = 10,
        gbuffer_material          = 11,
        gbuffer_material_2        = 12,
        gbuffer_velocity          = 13,
        gbuffer_velocity_previous = 14,
        gbuffer_depth             = 15,
    
        // Lighting
        light_diffuse              = 16,
        light_diffuse_transparent  = 17,
        light_specular             = 18,
        light_specular_transparent = 19,
        light_volumetric           = 20,
    
        // Light depth/color maps
        light_directional_depth = 21,
        light_directional_color = 22,
        light_point_depth       = 23,
        light_point_color       = 24,
        light_spot_depth        = 25,
        light_spot_color        = 26,
    
        // Noise
        noise_normal = 27,
        noise_blue   = 28,
    
        // Misc
        lutIbl           = 29,
        environment      = 30,
        ssgi             = 31,
        ssr              = 32,
        frame            = 33,
        tex              = 34,
        tex2             = 35,
        font_atlas       = 36,
        reflection_probe = 37
    };

    enum class Renderer_BindingsUav
    {
        tex            = 0,
        tex2           = 1,
        tex3           = 2,
        atomic_counter = 3,
        tex_array      = 4
    };

    enum class Renderer_Shader : uint8_t
    {
        gbuffer_v,
        gbuffer_p,
        depth_prepass_v,
        depth_prepass_p,
        depth_light_V,
        depth_light_p,
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
        outline_v,
        outline_p,
        outline_c,
        font_v,
        font_p,
        ssgi_c,
        ssr_c,
        temporal_filter_c,
        blur_gaussian_c,
        blur_gaussian_bilaterial_c,
        reflection_probe_v,
        reflection_probe_p,
        ffx_cas_c,
        ffx_spd_c
    };
    
    enum class Renderer_RenderTexture : uint8_t
    {
        undefined,
        gbuffer_albedo,
        gbuffer_normal,
        gbuffer_material,
        gbuffer_material_2,
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
        frame_render_2,
        frame_output,
        frame_output_2,
        dof_half,
        dof_half_2,
        ssgi,
        ssgi_filtered,
        ssr,
        bloom,
        blur,
        fsr2_mask_reactive,
        fsr2_mask_transparency,
        outline
    };
    
    enum class Renderer_Entity
    {
        Geometry,
        GeometryTransparent,
        Light,
        Camera,
        ReflectionProbe,
        AudioSource
    };

    enum class Renderer_Sampler
    {
        Compare_depth,
        Point_clamp,
        Point_wrap,
        Bilinear_clamp,
        Bilinear_wrap,
        Trilinear_clamp,
        Anisotropic_wrap
    };

    enum class Renderer_ConstantBuffer
    {
        Frame,
        Light,
        Material
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
}
