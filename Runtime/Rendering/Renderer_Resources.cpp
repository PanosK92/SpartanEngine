/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =========================
#include "Renderer.h"
#include "Shaders/ShaderBuffered.h"
#include "Font/Font.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_PipelineCache.h"
#include "../RHI/RHI_Texture2D.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::CreateDepthStencilStates()
    {
        m_depth_stencil_enabled     = make_shared<RHI_DepthStencilState>(m_rhi_device, true,    GetComparisonFunction());
        m_depth_stencil_disabled    = make_shared<RHI_DepthStencilState>(m_rhi_device, false,   GetComparisonFunction());
    }

    void Renderer::CreateRasterizerStates()
    {
        m_rasterizer_cull_back_solid        = make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Back,     Fill_Solid,     false, true, false, false, false);
        m_rasterizer_cull_front_solid       = make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Front,    Fill_Solid,     false, true, false, false, false);
        m_rasterizer_cull_none_solid        = make_shared<RHI_RasterizerState>(m_rhi_device, Cull_None,     Fill_Solid,     false, true, false, false, false);
        m_rasterizer_cull_back_wireframe    = make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Back,     Fill_Wireframe, false, true, false, false, true);
        m_rasterizer_cull_front_wireframe   = make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Front,    Fill_Wireframe, false, true, false, false, true);
        m_rasterizer_cull_none_wireframe    = make_shared<RHI_RasterizerState>(m_rhi_device, Cull_None,     Fill_Wireframe, false, true, false, false, true);
    }

    void Renderer::CreateBlendStates()
    {
        m_blend_disabled    = make_shared<RHI_BlendState>(m_rhi_device, false);
        m_blend_enabled     = make_shared<RHI_BlendState>(m_rhi_device, true);
        m_blend_color_add   = make_shared<RHI_BlendState>(m_rhi_device, true, Blend_One, Blend_One, Blend_Operation_Add);
        m_blend_bloom       = make_shared<RHI_BlendState>(m_rhi_device, true, Blend_One, Blend_One, Blend_Operation_Add, Blend_One, Blend_One, Blend_Operation_Add, 0.5f);
    }

    void Renderer::CreateSamplers()
    {
        m_sampler_compare_depth     = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  Sampler_Address_Clamp,  GetReverseZ() ? Comparison_Greater : Comparison_Less, false, true);
        m_sampler_point_clamp       = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_POINT,     Sampler_Address_Clamp,  Comparison_Always);
        m_sampler_bilinear_clamp    = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  Sampler_Address_Clamp,  Comparison_Always);
        m_sampler_bilinear_wrap     = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  Sampler_Address_Wrap,   Comparison_Always);
        m_sampler_trilinear_clamp   = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR, Sampler_Address_Clamp,  Comparison_Always);
        m_sampler_anisotropic_wrap  = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR, Sampler_Address_Wrap,   Comparison_Always, true);
    }

    void Renderer::CreateRenderTextures()
    {
        auto width  = static_cast<uint32_t>(m_resolution.x);
        auto height = static_cast<uint32_t>(m_resolution.y);

        if ((width / 4) == 0 || (height / 4) == 0)
        {
            LOGF_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Full-screen quad
        m_quad = Math::Rectangle(0, 0, m_resolution.x, m_resolution.y);
        m_quad.CreateBuffers(this);

        // G-Buffer
        m_render_targets[RenderTarget_Gbuffer_Albedo]   = make_shared<RHI_Texture2D>(m_context, width, height, Format_R8G8B8A8_UNORM);
        m_render_targets[RenderTarget_Gbuffer_Normal]   = make_shared<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT); // At Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding
        m_render_targets[RenderTarget_Gbuffer_Material] = make_shared<RHI_Texture2D>(m_context, width, height, Format_R8G8B8A8_UNORM);
        m_render_targets[RenderTarget_Gbuffer_Velocity] = make_shared<RHI_Texture2D>(m_context, width, height, Format_R16G16_FLOAT);
        m_render_targets[RenderTarget_Gbuffer_Depth]    = make_shared<RHI_Texture2D>(m_context, width, height, Format_D32_FLOAT);

        // Light
        m_render_targets[RenderTarget_Light_Diffuse]            = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);
        m_render_targets[RenderTarget_Light_Specular]           = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);
        m_render_targets[RenderTarget_Light_Volumetric]         = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);
        m_render_targets[RenderTarget_Light_Volumetric_Blurred] = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);

        // BRDF Specular Lut
        m_render_targets[RenderTarget_Brdf_Specular_Lut] = make_unique<RHI_Texture2D>(m_context, 400, 400, Format_R8G8_UNORM);
        m_brdf_specular_lut_rendered = false;

        // Composition
        m_render_targets[RenderTarget_Composition_Hdr]              = make_unique<RHI_Texture2D>(m_context, width, height, Format_R32G32B32A32_FLOAT);
        m_render_targets[RenderTarget_Composition_Ldr]              = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);
        m_render_targets[RenderTarget_Composition_Hdr_2]            = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Hdr]->GetFormat()); // Used for Post-Processing   
        m_render_targets[RenderTarget_Composition_Hdr_History]      = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Hdr]->GetFormat()); // Used by TAA and SSR
        m_render_targets[RenderTarget_Composition_Hdr_History_2]    = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Hdr]->GetFormat()); // Used by TAA
        m_render_targets[RenderTarget_Composition_Ldr_2]            = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Ldr]->GetFormat()); // Used for Post-Processing   

        // SSAO
        m_render_targets[RenderTarget_Ssao_Half]            = make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, Format_R8_UNORM);                                       // Raw
        m_render_targets[RenderTarget_Ssao_Half_Blurred]    = make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, m_render_targets[RenderTarget_Ssao_Half]->GetFormat()); // Blurred
        m_render_targets[RenderTarget_Ssao]                 = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Ssao_Half]->GetFormat()); // Upscaled

        // SSR
        m_render_targets[RenderTarget_Ssr]          = make_shared<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);
        m_render_targets[RenderTarget_Ssr_Blurred]  = make_shared<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Ssr]->GetFormat());

        // Bloom
        {
            // Create as many bloom textures as required to scale down to or below 16px (in any dimension)
            m_render_tex_bloom.clear();
            m_render_tex_bloom.emplace_back(make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, Format_R16G16B16A16_FLOAT));
            while (m_render_tex_bloom.back()->GetWidth() > 16 && m_render_tex_bloom.back()->GetHeight() > 16)
            {
                m_render_tex_bloom.emplace_back(
                    make_unique<RHI_Texture2D>(
                        m_context,
                        m_render_tex_bloom.back()->GetWidth() / 2,
                        m_render_tex_bloom.back()->GetHeight() / 2,
                        Format_R16G16B16A16_FLOAT
                        )
                );
            }
        }
    }

    void Renderer::CreateShaders()
    {
        // Get standard shader directory
        const auto dir_shaders = m_resource_cache->GetDataDirectory(Asset_Shaders);

        // Quad - Used by almost everything
        auto shader_quad = make_shared<RHI_Shader>(m_rhi_device);
        shader_quad->CompileAsync<RHI_Vertex_PosTex>(m_context, Shader_Vertex, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Quad_V] = shader_quad;

        // Depth
        auto shader_depth = make_shared<RHI_Shader>(m_rhi_device);
        shader_depth->CompileAsync<RHI_Vertex_Pos>(m_context, Shader_Vertex, dir_shaders + "Depth.hlsl");
        m_shaders[Shader_Depth_V] = shader_depth;

        // G-Buffer
        auto shader_gbuffer = make_shared<RHI_Shader>(m_rhi_device);
        shader_gbuffer->CompileAsync<RHI_Vertex_PosTexNorTan>(m_context, Shader_Vertex, dir_shaders + "GBuffer.hlsl");
        m_shaders[Shader_Gbuffer_V] = shader_gbuffer;

        // BRDF - Specular Lut
        auto shader_brdf_specular_lut = make_shared<RHI_Shader>(m_rhi_device);
        shader_brdf_specular_lut->AddDefine("BRDF_ENV_SPECULAR_LUT");
        shader_brdf_specular_lut->CompileAsync(m_context, Shader_Pixel, dir_shaders + "BRDF.hlsl");
        m_shaders[Shader_BrdfSpecularLut] = shader_brdf_specular_lut;

        // Light - Directional
        auto shader_light_directional = make_shared<RHI_Shader>(m_rhi_device);
        shader_light_directional->AddDefine("DIRECTIONAL");
        shader_light_directional->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Light.hlsl");
        m_shaders[Shader_LightDirectional_P] = shader_light_directional;

        // Light - Point
        auto shader_light_point = make_shared<RHI_Shader>(m_rhi_device);
        shader_light_point->AddDefine("POINT");
        shader_light_point->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Light.hlsl");
        m_shaders[Shader_LightPoint_P] = shader_light_point;

        // Light - Spot
        auto shader_light_spot = make_shared<RHI_Shader>(m_rhi_device);
        shader_light_spot->AddDefine("SPOT");
        shader_light_spot->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Light.hlsl");
        m_shaders[Shader_LightSpot_P] = shader_light_spot;

        // Composition
        auto shader_composition = make_shared<ShaderBuffered>(m_rhi_device);
        shader_composition->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Composition.hlsl");
        m_shaders[Shader_Composition_P] = shader_composition;

        // Font
        auto font = make_shared<ShaderBuffered>(m_rhi_device);
        font->CompileAsync<RHI_Vertex_PosTex>(m_context, Shader_VertexPixel, dir_shaders + "Font.hlsl");
        font->AddBuffer<Struct_Matrix_Vector4>();
        m_shaders[Shader_Font_Vp] = font;

        // Transform gizmo
        auto shader_gizmoTransform = make_shared<ShaderBuffered>(m_rhi_device);
        shader_gizmoTransform->CompileAsync<RHI_Vertex_PosTexNorTan>(m_context, Shader_VertexPixel, dir_shaders + "TransformGizmo.hlsl");
        shader_gizmoTransform->AddBuffer<Struct_Matrix_Vector3>();
        shader_gizmoTransform->AddBuffer<Struct_Matrix_Vector3>();
        shader_gizmoTransform->AddBuffer<Struct_Matrix_Vector3>();
        shader_gizmoTransform->AddBuffer<Struct_Matrix_Vector3>();
        m_shaders[Shader_GizmoTransform_Vp] = shader_gizmoTransform;

        // SSAO
        auto shader_ssao = make_shared<ShaderBuffered>(m_rhi_device);
        shader_ssao->CompileAsync(m_context, Shader_Pixel, dir_shaders + "SSAO.hlsl");
        m_shaders[Shader_Ssao_P] = shader_ssao;

        // SSR
        auto shader_ssr = make_shared<ShaderBuffered>(m_rhi_device);
        shader_ssr->CompileAsync(m_context, Shader_Pixel, dir_shaders + "SSR.hlsl");
        m_shaders[Shader_Ssr_P] = shader_ssr;

        // Color
        auto shader_color = make_shared<ShaderBuffered>(m_rhi_device);
        shader_color->CompileAsync<RHI_Vertex_PosCol>(m_context, Shader_VertexPixel, dir_shaders + "Color.hlsl");
        shader_color->AddBuffer<Struct_Matrix_Matrix>();
        m_shaders[Shader_Color_Vp] = shader_color;

        // Texture
        auto shader_texture = make_shared<RHI_Shader>(m_rhi_device);
        shader_texture->AddDefine("PASS_TEXTURE");
        shader_texture->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Texture_P] = shader_texture;

        // FXAA
        auto shader_fxaa = make_shared<RHI_Shader>(m_rhi_device);
        shader_fxaa->AddDefine("PASS_FXAA");
        shader_fxaa->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Fxaa_P] = shader_fxaa;

        // Luma
        auto shader_luma = make_shared<RHI_Shader>(m_rhi_device);
        shader_luma->AddDefine("PASS_LUMA");
        shader_luma->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Luma_P] = shader_luma;

        // Sharpening - Lumasharpen
        auto shader_sharpen_luma = make_shared<RHI_Shader>(m_rhi_device);
        shader_sharpen_luma->AddDefine("PASS_LUMA_SHARPEN");
        shader_sharpen_luma->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Sharpen_Luma_P] = shader_sharpen_luma;

        // Sharpening - TAA sharpen
        auto shader_sharpen_taa = make_shared<RHI_Shader>(m_rhi_device);
        shader_sharpen_taa->AddDefine("PASS_TAA_SHARPEN");
        shader_sharpen_taa->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Sharpen_Taa_P] = shader_sharpen_taa;

        // Chromatic aberration
        auto shader_chromaticAberration = make_shared<RHI_Shader>(m_rhi_device);
        shader_chromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
        shader_chromaticAberration->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_ChromaticAberration_P] = shader_chromaticAberration;

        // Blur Box
        auto shader_blurBox = make_shared<RHI_Shader>(m_rhi_device);
        shader_blurBox->AddDefine("PASS_BLUR_BOX");
        shader_blurBox->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_BlurBox_P] = shader_blurBox;

        // Blur Gaussian
        auto shader_blurGaussian = make_shared<ShaderBuffered>(m_rhi_device);
        shader_blurGaussian->AddDefine("PASS_BLUR_GAUSSIAN");
        shader_blurGaussian->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        shader_blurGaussian->AddBuffer<Struct_Blur>();
        m_shaders[Shader_BlurGaussian_P] = shader_blurGaussian;

        // Blur Bilateral Gaussian
        auto shader_blurGaussianBilateral = make_shared<ShaderBuffered>(m_rhi_device);
        shader_blurGaussianBilateral->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
        shader_blurGaussianBilateral->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        shader_blurGaussianBilateral->AddBuffer<Struct_Blur>();
        m_shaders[Shader_BlurGaussianBilateral_P] = shader_blurGaussianBilateral;

        // Bloom - downsample luminance
        auto shader_bloom_downsample_luminance = make_shared<RHI_Shader>(m_rhi_device);
        shader_bloom_downsample_luminance->AddDefine("PASS_BLOOM_DOWNSAMPLE_LUMINANCE");
        shader_bloom_downsample_luminance->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_BloomDownsampleLuminance_P] = shader_bloom_downsample_luminance;

        // Bloom - Downsample anti-flicker
        auto shader_bloom_downsample = make_shared<RHI_Shader>(m_rhi_device);
        shader_bloom_downsample->AddDefine("PASS_BLOOM_DOWNSAMPLE");
        shader_bloom_downsample->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_BloomDownsample_P] = shader_bloom_downsample;

        // Bloom - blend additive
        auto shader_bloomBlend = make_shared<RHI_Shader>(m_rhi_device);
        shader_bloomBlend->AddDefine("PASS_BLOOM_BLEND_ADDITIVE");
        shader_bloomBlend->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_BloomBlend_P] = shader_bloomBlend;

        // Tone-mapping
        auto shader_toneMapping = make_shared<RHI_Shader>(m_rhi_device);
        shader_toneMapping->AddDefine("PASS_TONEMAPPING");
        shader_toneMapping->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_ToneMapping_P] = shader_toneMapping;

        // Gamma correction
        auto shader_gammaCorrection = make_shared<RHI_Shader>(m_rhi_device);
        shader_gammaCorrection->AddDefine("PASS_GAMMA_CORRECTION");
        shader_gammaCorrection->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_GammaCorrection_P] = shader_gammaCorrection;

        // TAA
        auto shader_taa = make_shared<RHI_Shader>(m_rhi_device);
        shader_taa->AddDefine("PASS_TAA_RESOLVE");
        shader_taa->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Taa_P] = shader_taa;

        // Motion Blur
        auto shader_motionBlur = make_shared<RHI_Shader>(m_rhi_device);
        shader_motionBlur->AddDefine("PASS_MOTION_BLUR");
        shader_motionBlur->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_MotionBlur_P] = shader_motionBlur;

        // Dithering
        auto shader_dithering = make_shared<RHI_Shader>(m_rhi_device);
        shader_dithering->AddDefine("PASS_DITHERING");
        shader_dithering->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Dithering_P] = shader_dithering;

        // Upsample box
        auto shader_upsampleBox = make_shared<RHI_Shader>(m_rhi_device);
        shader_upsampleBox->AddDefine("PASS_UPSAMPLE_BOX");
        shader_upsampleBox->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_Upsample_P] = shader_upsampleBox;

        // Debug Normal
        auto shader_debugNormal = make_shared<RHI_Shader>(m_rhi_device);
        shader_debugNormal->AddDefine("DEBUG_NORMAL");
        shader_debugNormal->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_DebugNormal_P] = shader_debugNormal;

        // Debug velocity
        auto shader_debugVelocity = make_shared<RHI_Shader>(m_rhi_device);
        shader_debugVelocity->AddDefine("DEBUG_VELOCITY");
        shader_debugVelocity->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_DebugVelocity_P] = shader_debugVelocity;

        // Debug R channel
        auto shader_debugRChannel = make_shared<RHI_Shader>(m_rhi_device);
        shader_debugRChannel->AddDefine("DEBUG_R_CHANNEL");
        shader_debugRChannel->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_DebugChannelR_P] = shader_debugRChannel;

        // Debug A channel
        auto shader_debugAChannel = make_shared<RHI_Shader>(m_rhi_device);
        shader_debugAChannel->AddDefine("DEBUG_A_CHANNEL");
        shader_debugAChannel->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_DebugChannelA_P] = shader_debugAChannel;

        // Debug A channel
        auto shader_debugRgbGammaCorrect = make_shared<RHI_Shader>(m_rhi_device);
        shader_debugRgbGammaCorrect->AddDefine("DEBUG_RGB_CHANNEL_GAMMA_CORRECT");
        shader_debugRgbGammaCorrect->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
        m_shaders[Shader_DebugChannelRgbGammaCorrect_P] = shader_debugRgbGammaCorrect;
    }

    void Renderer::CreateFonts()
    {
        // Get standard font directory
        const auto dir_font = m_resource_cache->GetDataDirectory(Asset_Fonts);

        // Load a font (used for performance metrics)
        m_font = make_unique<Font>(m_context, dir_font + "CalibriBold.ttf", 14, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
    }

    void Renderer::CreateTextures()
    {
        // Get standard texture directory
        const auto dir_texture = m_resource_cache->GetDataDirectory(Asset_Textures);

        auto generate_mipmaps = false;

        // Noise texture (used by SSAO shader)
        m_tex_noise_normal = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_tex_noise_normal->LoadFromFile(dir_texture + "noise.jpg");

        m_tex_white = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_tex_white->LoadFromFile(dir_texture + "white.png");

        m_tex_black = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_tex_black->LoadFromFile(dir_texture + "black.png");

        // Gizmo icons
        m_gizmo_tex_light_directional = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_gizmo_tex_light_directional->LoadFromFile(dir_texture + "sun.png");

        m_gizmo_tex_light_point = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_gizmo_tex_light_point->LoadFromFile(dir_texture + "light_bulb.png");

        m_gizmo_tex_light_spot = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_gizmo_tex_light_spot->LoadFromFile(dir_texture + "flashlight.png");
    }
}
