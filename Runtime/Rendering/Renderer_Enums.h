/*
Copyright(c) 2016-2021 Panos Karabelas

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

namespace Spartan
{
    // Shader resource view bindings
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
        gbuffer_albedo     = 8,
        gbuffer_normal     = 9,
        gbuffer_material   = 10,
        gbuffer_velocity   = 11,
        gbuffer_depth      = 12,

        // Lighting
        light_diffuse               = 13,
        light_diffuse_transparent   = 14,
        light_specular              = 15,
        light_specular_transparent  = 16,
        light_volumetric            = 17,

        // Light depth/color maps
        light_directional_depth    = 18,
        light_directional_color    = 19,
        light_point_depth          = 20,
        light_point_color          = 21,
        light_spot_depth           = 22,
        light_spot_color           = 23,

        // Noise
        noise_normal    = 24,
        noise_blue      = 25,

        // Misc
        lutIbl      = 26,
        environment = 27,
        ssao        = 28,
        ssr         = 29,
        frame       = 30,
        tex         = 31,
        tex2        = 32,
        font_atlas  = 33,
        ssgi        = 34
    };

    // Unordered access views bindings
    enum class RendererBindingsUav
    {
        r           = 0,
        rg          = 1,
        rgb         = 2,
        rgba        = 3,
        rgb2        = 4,
        rgb3        = 5,
        array_rgba  = 6
    };

    // Shaders
    enum class RendererShader : uint8_t
    {
        Gbuffer_V,
        Gbuffer_P,
        Depth_V,
        Depth_P,
        Quad_V,
        Copy_Point_C,
        Copy_Bilinear_C,
        Copy_Point_P,
        Copy_Bilinear_P,
        Fxaa_C,
        Fxaa_Luminance_C,
        FilmGrain_C,
        Taa_C,
        MotionBlur_C,
        Dof_DownsampleCoc_C,
        Dof_Bokeh_C,
        Dof_Tent_C,
        Dof_UpscaleBlend_C,
        Sharpening_C,
        ChromaticAberration_C,
        BloomDownsampleLuminance_C,
        BloomDownsample_C,
        BloomUpsampleBlendFrame_C,
        BloomUpsampleBlendMip_C,
        ToneMapping_C,
        GammaCorrection_C,
        Dithering_C,
        DebugNormal_C,
        DebugVelocity_C,
        DebugChannelR_C,
        DebugChannelA_C,
        DebugChannelRgbGammaCorrect_C,
        BrdfSpecularLut_C,
        Light_C,
        Light_Composition_C,
        Light_ImageBased_P,
        Color_V,
        Color_P,
        Font_V,
        Font_P,
        Ssao_C,
        Ssgi_C,
        SsgiInject_C,
        SsrTrace_C,
        Reflections_P,
        Entity_V,
        Entity_Transform_P,
        BlurBox_P,
        BlurGaussian_P,
        BlurGaussianBilateral_P,
        Entity_Outline_P
    };

    // Render targets
    enum class RendererRt : uint8_t
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
        Frame,
        Frame_2,
        Frame_PostProcess,
        Frame_PostProcess_2,
        Dof_Half,
        Dof_Half_2,
        Ssao,
        Ssao_Blurred,
        Ssgi,
        Ssr,
        TaaHistory,
        Ssgi_Accumulation
    };

    // Renderer/graphics options
    enum Renderer_Option : uint64_t
    {
        Render_Debug_Aabb                   = 1 << 0,
        Render_Debug_PickingRay             = 1 << 1,
        Render_Debug_Grid                   = 1 << 2,
        Render_Debug_Transform              = 1 << 3,
        Render_Debug_SelectionOutline       = 1 << 4,
        Render_Debug_Lights                 = 1 << 5,
        Render_Debug_PerformanceMetrics     = 1 << 6,
        Render_Debug_Physics                = 1 << 7,
        Render_Debug_Wireframe              = 1 << 8,
        Render_Bloom                        = 1 << 9,
        Render_VolumetricFog                = 1 << 10,
        Render_AntiAliasing_Taa             = 1 << 11,
        Render_AntiAliasing_Fxaa            = 1 << 12,
        Render_Ssao                         = 1 << 13,
        Render_Ssgi                         = 1 << 14,
        Render_ScreenSpaceShadows           = 1 << 15,
        Render_ScreenSpaceReflections       = 1 << 16,
        Render_MotionBlur                   = 1 << 17,
        Render_DepthOfField                 = 1 << 18,
        Render_FilmGrain                    = 1 << 19,
        Render_Sharpening_LumaSharpen       = 1 << 20,
        Render_ChromaticAberration          = 1 << 21,
        Render_Dithering                    = 1 << 22,
        Render_ReverseZ                     = 1 << 23,
        Render_DepthPrepass                 = 1 << 24
    };

    // Renderer/graphics options values
    enum class Renderer_Option_Value
    {
        Anisotropy,
        ShadowResolution,
        Tonemapping,
        Gamma,
        Intensity,
        Sharpen_Strength,
        Fog,
        Taa_AllowUpsampling
    };

    // Tonemapping
    enum Renderer_ToneMapping_Type
    {
        Renderer_ToneMapping_Off,
        Renderer_ToneMapping_ACES,
        Renderer_ToneMapping_Reinhard,
        Renderer_ToneMapping_Uncharted2
    };

    // Renderable object types
    enum Renderer_Object_Type
    {
        Renderer_Object_Opaque,
        Renderer_Object_Transparent,
        Renderer_Object_Light,
        Renderer_Object_Camera
    };
}
