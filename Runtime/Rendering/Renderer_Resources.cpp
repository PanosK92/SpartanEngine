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
#include "Font/Font.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_PipelineCache.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_Sampler.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::CreateConstantBuffers()
    {
        m_buffer_frame_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device);
        m_buffer_frame_gpu->Create<FrameBuffer>();

        m_buffer_uber_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device);
        m_buffer_uber_gpu->Create<UberBuffer>();

        m_buffer_light_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device);
        m_buffer_light_gpu->Create<LightBuffer>();
    }

    void Renderer::CreateDepthStencilStates()
    {
        m_depth_stencil_enabled_write       = make_shared<RHI_DepthStencilState>(m_rhi_device, true,    true,   GetComparisonFunction(), false);
        m_depth_stencil_enabled_no_write    = make_shared<RHI_DepthStencilState>(m_rhi_device, true,    false,  GetComparisonFunction(), false);
        m_depth_stencil_disabled            = make_shared<RHI_DepthStencilState>(m_rhi_device, false,   false,  GetComparisonFunction(), false);
    }

    void Renderer::CreateRasterizerStates()
    {
        m_rasterizer_cull_back_solid            = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back,     RHI_Fill_Solid,     true,   false, false, false);
        m_rasterizer_cull_back_solid_no_clip    = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back,     RHI_Fill_Solid,     false,  false, false, false);
        m_rasterizer_cull_front_solid           = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Front,    RHI_Fill_Solid,     true,   false, false, false);
        m_rasterizer_cull_none_solid            = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_None,     RHI_Fill_Solid,     true,   false, false, false);
        m_rasterizer_cull_back_wireframe        = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back,     RHI_Fill_Wireframe, true,   false, false, true);
        m_rasterizer_cull_front_wireframe       = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Front,    RHI_Fill_Wireframe, true,   false, false, true);
        m_rasterizer_cull_none_wireframe        = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_None,     RHI_Fill_Wireframe, true,   false, false, true);
    }

    void Renderer::CreateBlendStates()
    {
        m_blend_disabled    = make_shared<RHI_BlendState>(m_rhi_device, false);
        m_blend_enabled     = make_shared<RHI_BlendState>(m_rhi_device, true);
        m_blend_color_add   = make_shared<RHI_BlendState>(m_rhi_device, true, RHI_Blend_One, RHI_Blend_One, RHI_Blend_Operation_Add);
        m_blend_bloom       = make_shared<RHI_BlendState>(m_rhi_device, true, RHI_Blend_One, RHI_Blend_One, RHI_Blend_Operation_Add, RHI_Blend_One, RHI_Blend_One, RHI_Blend_Operation_Add, 0.5f);
    }

    void Renderer::CreateSamplers()
    {
        m_sampler_compare_depth     = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  RHI_Sampler_Address_Clamp,  GetOptionValue(Render_ReverseZ) ? RHI_Comparison_Greater : RHI_Comparison_Less, false, true);
        m_sampler_point_clamp       = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_POINT,     RHI_Sampler_Address_Clamp,  RHI_Comparison_Always);
        m_sampler_bilinear_clamp    = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  RHI_Sampler_Address_Clamp,  RHI_Comparison_Always);
        m_sampler_bilinear_wrap     = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  RHI_Sampler_Address_Wrap,   RHI_Comparison_Always);
        m_sampler_trilinear_clamp   = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR, RHI_Sampler_Address_Clamp,  RHI_Comparison_Always);
        m_sampler_anisotropic_wrap  = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR, RHI_Sampler_Address_Wrap,   RHI_Comparison_Always, true);
    }

    void Renderer::CreateRenderTextures()
    {
        auto width  = static_cast<uint32_t>(m_resolution.x);
        auto height = static_cast<uint32_t>(m_resolution.y);

        if ((width / 4) == 0 || (height / 4) == 0)
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Full-screen quad
        m_quad = Math::Rectangle(0, 0, m_resolution.x, m_resolution.y);
        m_quad.CreateBuffers(this);

        // G-Buffer
        m_render_targets[RenderTarget_Gbuffer_Albedo]   = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R8G8B8A8_Unorm);
        m_render_targets[RenderTarget_Gbuffer_Normal]   = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float); // At Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding
        m_render_targets[RenderTarget_Gbuffer_Material] = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R8G8B8A8_Unorm);
        m_render_targets[RenderTarget_Gbuffer_Velocity] = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16_Float);
        m_render_targets[RenderTarget_Gbuffer_Depth]    = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_D32_Float);

        // Light
        m_render_targets[RenderTarget_Light_Diffuse]            = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float);
        m_render_targets[RenderTarget_Light_Specular]           = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float);
        m_render_targets[RenderTarget_Light_Volumetric]         = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float);
        m_render_targets[RenderTarget_Light_Volumetric_Blurred] = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float);

        // BRDF Specular Lut
        m_render_targets[RenderTarget_Brdf_Specular_Lut] = make_unique<RHI_Texture2D>(m_context, 400, 400, RHI_Format_R8G8_Unorm);
        m_brdf_specular_lut_rendered = false;

        // Composition
        m_render_targets[RenderTarget_Composition_Hdr]              = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R32G32B32A32_Float);
        m_render_targets[RenderTarget_Composition_Ldr]              = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float);
        m_render_targets[RenderTarget_Composition_Hdr_2]            = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Hdr]->GetFormat()); // Used for Post-Processing   
        m_render_targets[RenderTarget_Composition_Hdr_History]      = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Hdr]->GetFormat()); // Used by TAA and SSR
        m_render_targets[RenderTarget_Composition_Hdr_History_2]    = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Hdr]->GetFormat()); // Used by TAA
        m_render_targets[RenderTarget_Composition_Ldr_2]            = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Composition_Ldr]->GetFormat()); // Used for Post-Processing   

        // SSAO
        float ssao_scale                                = m_option_values[Option_Value_Ssao_Scale];
        m_render_targets[RenderTarget_Ssao_Raw]         = make_unique<RHI_Texture2D>(m_context, static_cast<uint32_t>(width * ssao_scale), static_cast<uint32_t>(height * ssao_scale), RHI_Format_R8_Unorm);                                    // Raw
        m_render_targets[RenderTarget_Ssao_Blurred]     = make_unique<RHI_Texture2D>(m_context, static_cast<uint32_t>(width * ssao_scale), static_cast<uint32_t>(height * ssao_scale), m_render_targets[RenderTarget_Ssao_Raw]->GetFormat());   // Blurred
        m_render_targets[RenderTarget_Ssao]             = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Ssao_Raw]->GetFormat());                                                                           // Upscaled

        // SSR
        m_render_targets[RenderTarget_Ssr]          = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float);
        m_render_targets[RenderTarget_Ssr_Blurred]  = make_shared<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Ssr]->GetFormat());

        // Bloom
        {
            // Create as many bloom textures as required to scale down to or below 16px (in any dimension)
            m_render_tex_bloom.clear();
            m_render_tex_bloom.emplace_back(make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, RHI_Format_R16G16B16A16_Float));
            while (m_render_tex_bloom.back()->GetWidth() > 16 && m_render_tex_bloom.back()->GetHeight() > 16)
            {
                m_render_tex_bloom.emplace_back(
                    make_unique<RHI_Texture2D>(
                        m_context,
                        m_render_tex_bloom.back()->GetWidth() / 2,
                        m_render_tex_bloom.back()->GetHeight() / 2,
                        RHI_Format_R16G16B16A16_Float
                        )
                );
            }
        }
    }

    void Renderer::CreateShaders()
    {
        // Get standard shader directory
        const auto dir_shaders = m_resource_cache->GetDataDirectory(Asset_Shaders) + "/";

        // Quad - Used by almost everything
        m_shaders[Shader_Quad_V] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Quad_V]->CompileAsync<RHI_Vertex_PosTex>(m_context, Shader_Vertex, dir_shaders + "Quad.hlsl");

        // Depth Vertex
        m_shaders[Shader_Depth_V] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Depth_V]->CompileAsync<RHI_Vertex_Pos>(m_context, Shader_Vertex, dir_shaders + "Depth.hlsl");

        // G-Buffer
        m_shaders[Shader_Gbuffer_V] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Gbuffer_V]->CompileAsync<RHI_Vertex_PosTexNorTan>(m_context, Shader_Vertex, dir_shaders + "GBuffer.hlsl");

        // BRDF - Specular Lut
        m_shaders[Shader_BrdfSpecularLut] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BrdfSpecularLut]->AddDefine("BRDF_ENV_SPECULAR_LUT");
        m_shaders[Shader_BrdfSpecularLut]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "BRDF.hlsl");

        // Light - Directional
        m_shaders[Shader_LightDirectional_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_LightDirectional_P]->AddDefine("DIRECTIONAL");
        m_shaders[Shader_LightDirectional_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Light.hlsl");

        // Light - Point
        m_shaders[Shader_LightPoint_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_LightPoint_P]->AddDefine("POINT");
        m_shaders[Shader_LightPoint_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Light.hlsl");

        // Light - Spot
        m_shaders[Shader_LightSpot_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_LightSpot_P]->AddDefine("SPOT");
        m_shaders[Shader_LightSpot_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Light.hlsl");

        // Texture
        m_shaders[Shader_Texture_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Texture_P]->AddDefine("PASS_TEXTURE");
        m_shaders[Shader_Texture_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // FXAA
        m_shaders[Shader_Fxaa_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Fxaa_P]->AddDefine("PASS_FXAA");
        m_shaders[Shader_Fxaa_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Luma
        m_shaders[Shader_Luma_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Luma_P]->AddDefine("PASS_LUMA");
        m_shaders[Shader_Luma_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Sharpening - Lumasharpen
        m_shaders[Shader_Sharpen_Luma_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Sharpen_Luma_P]->AddDefine("PASS_LUMA_SHARPEN");
        m_shaders[Shader_Sharpen_Luma_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Chromatic aberration
        m_shaders[Shader_ChromaticAberration_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_ChromaticAberration_P]->AddDefine("PASS_CHROMATIC_ABERRATION");
        m_shaders[Shader_ChromaticAberration_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Blur Box
        m_shaders[Shader_BlurBox_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BlurBox_P]->AddDefine("PASS_BLUR_BOX");
        m_shaders[Shader_BlurBox_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Bloom - downsample luminance
        m_shaders[Shader_BloomDownsampleLuminance_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BloomDownsampleLuminance_P]->AddDefine("PASS_BLOOM_DOWNSAMPLE_LUMINANCE");
        m_shaders[Shader_BloomDownsampleLuminance_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Bloom - Downsample anti-flicker
        m_shaders[Shader_BloomDownsample_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BloomDownsample_P]->AddDefine("PASS_BLOOM_DOWNSAMPLE");
        m_shaders[Shader_BloomDownsample_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Bloom - blend additive
        m_shaders[Shader_BloomBlend_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BloomBlend_P]->AddDefine("PASS_BLOOM_BLEND_ADDITIVE");
        m_shaders[Shader_BloomBlend_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Tone-mapping
        m_shaders[Shader_ToneMapping_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_ToneMapping_P]->AddDefine("PASS_TONEMAPPING");
        m_shaders[Shader_ToneMapping_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Gamma correction
        m_shaders[Shader_GammaCorrection_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_GammaCorrection_P]->AddDefine("PASS_GAMMA_CORRECTION");
        m_shaders[Shader_GammaCorrection_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // TAA
        m_shaders[Shader_Taa_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Taa_P]->AddDefine("PASS_TAA_RESOLVE");
        m_shaders[Shader_Taa_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Motion Blur
        m_shaders[Shader_MotionBlur_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_MotionBlur_P]->AddDefine("PASS_MOTION_BLUR");
        m_shaders[Shader_MotionBlur_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Dithering
        m_shaders[Shader_Dithering_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Dithering_P]->AddDefine("PASS_DITHERING");
        m_shaders[Shader_Dithering_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Upsample box
        m_shaders[Shader_Upsample_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Upsample_P]->AddDefine("PASS_UPSAMPLE_BOX");
        m_shaders[Shader_Upsample_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Downsample box
        m_shaders[Shader_Downsample_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Downsample_P]->AddDefine("PASS_DOWNSAMPLE_BOX");
        m_shaders[Shader_Downsample_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Debug Normal
        m_shaders[Shader_DebugNormal_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_DebugNormal_P]->AddDefine("DEBUG_NORMAL");
        m_shaders[Shader_DebugNormal_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Debug velocity
        m_shaders[Shader_DebugVelocity_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_DebugVelocity_P]->AddDefine("DEBUG_VELOCITY");
        m_shaders[Shader_DebugVelocity_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Debug R channel
        m_shaders[Shader_DebugChannelR_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_DebugChannelR_P]->AddDefine("DEBUG_R_CHANNEL");
        m_shaders[Shader_DebugChannelR_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Debug A channel
        m_shaders[Shader_DebugChannelA_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_DebugChannelA_P]->AddDefine("DEBUG_A_CHANNEL");
        m_shaders[Shader_DebugChannelA_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Debug A channel
        m_shaders[Shader_DebugChannelRgbGammaCorrect_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_DebugChannelRgbGammaCorrect_P]->AddDefine("DEBUG_RGB_CHANNEL_GAMMA_CORRECT");
        m_shaders[Shader_DebugChannelRgbGammaCorrect_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Blur Gaussian
        m_shaders[Shader_BlurGaussian_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BlurGaussian_P]->AddDefine("PASS_BLUR_GAUSSIAN");
        m_shaders[Shader_BlurGaussian_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // Blur Bilateral Gaussian
        m_shaders[Shader_BlurGaussianBilateral_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_BlurGaussianBilateral_P]->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
        m_shaders[Shader_BlurGaussianBilateral_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

        // SSAO
        m_shaders[Shader_Ssao_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Ssao_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "SSAO.hlsl");

        // SSR
        m_shaders[Shader_Ssr_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Ssr_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "SSR.hlsl");

        // Transform gizmo
        m_shaders[Shader_GizmoTransform_V] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_GizmoTransform_V]->CompileAsync<RHI_Vertex_PosTexNorTan>(m_context, Shader_Vertex, dir_shaders + "TransformGizmo.hlsl");
        m_shaders[Shader_GizmoTransform_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_GizmoTransform_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "TransformGizmo.hlsl");

        // Composition
        m_shaders[Shader_Composition_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Composition_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Composition.hlsl");

        // Font
        m_shaders[Shader_Font_V] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Font_V]->CompileAsync<RHI_Vertex_PosTex>(m_context, Shader_Vertex, dir_shaders + "Font.hlsl");
        m_shaders[Shader_Font_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Font_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Font.hlsl");

        // Color
        m_shaders[Shader_Color_V] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Color_V]->CompileAsync<RHI_Vertex_PosCol>(m_context, Shader_Vertex, dir_shaders + "Color.hlsl");
        m_shaders[Shader_Color_P] = make_shared<RHI_Shader>(m_rhi_device);
        m_shaders[Shader_Color_P]->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Color.hlsl");
    }

    void Renderer::CreateFonts()
    {
        // Get standard font directory
        const auto dir_font = m_resource_cache->GetDataDirectory(Asset_Fonts) + "/";

        // Load a font (used for performance metrics)
        m_font = make_unique<Font>(m_context, dir_font + "CalibriBold.ttf", 14, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
    }

    void Renderer::CreateTextures()
    {
        // Get standard texture directory
        const auto dir_texture = m_resource_cache->GetDataDirectory(Asset_Textures) + "/";

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
