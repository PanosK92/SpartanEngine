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
        Disabled,
        Aces,
        Reinhard,
        Uncharted2
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

    enum class RendererBindingsSb
    {
        counter = 19
    };

    enum class RendererShader : uint8_t
    {
        Gbuffer_V,
        Gbuffer_P,
        Depth_Prepass_V,
        Depth_Prepass_P,
        Depth_Light_V,
        Depth_Light_P,
        FullscreenTriangle_V,
        Quad_V,
        Copy_Point_C,
        Copy_Bilinear_C,
        Copy_Point_P,
        Copy_Bilinear_P,
        Fxaa_C,
        FilmGrain_C,
        MotionBlur_C,
        Dof_DownsampleCoc_C,
        Dof_Bokeh_C,
        Dof_Tent_C,
        Dof_UpscaleBlend_C,
        ChromaticAberration_C,
        BloomLuminance_C,
        BloomDownsample_C,
        BloomBlendFrame_C,
        BloomUpsampleBlendMip_C,
        ToneMappingGammaCorrection_C,
        Debanding_C,
        Debug_ReflectionProbe_V,
        Debug_ReflectionProbe_P,
        BrdfSpecularLut_C,
        Light_C,
        Light_Composition_C,
        Light_ImageBased_P,
        Lines_V,
        Lines_P,
        Font_V,
        Font_P,
        Ssao_C,
        Ssr_C,
        Entity_V,
        Entity_Transform_P,
        BlurGaussian_C,
        BlurGaussianBilateral_C,
        Entity_Outline_P,
        Reflection_Probe_V,
        Reflection_Probe_P,
        Ffx_Cas_C,
        Ffx_Spd_C,
        Ffx_Spd_LuminanceAntiflicker_C
    };
    
    enum class RendererTexture : uint8_t
    {
        Undefined,
        Gbuffer_Albedo,
        Gbuffer_Normal,
        Gbuffer_Material,
        Gbuffer_Velocity,
        Gbuffer_Depth,
        Brdf_Specular_Lut,
        Light_Diffuse,
        Light_Diffuse_Transparent,
        Light_Specular,
        Light_Specular_Transparent,
        Light_Volumetric,
        Frame_Render,
        Frame_Render_2,
        Frame_Output,
        Frame_Output_2,
        Dof_Half,
        Dof_Half_2,
        Ssao,
        Ssao_Gi,
        Ssr,
        Bloom,
        Blur
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
