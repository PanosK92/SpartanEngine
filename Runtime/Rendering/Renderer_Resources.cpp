/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ============================
#include "Spartan.h"
#include "Renderer.h"
#include "ShaderGBuffer.h"
#include "ShaderLight.h"
#include "Font/Font.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_SwapChain.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::CreateConstantBuffers()
    {
        bool is_dynamic = true;

        m_buffer_frame_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "frame", is_dynamic);
        m_buffer_frame_gpu->Create<BufferFrame>(m_swap_chain_buffer_count);

        m_buffer_material_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "material", is_dynamic);
        m_buffer_material_gpu->Create<BufferMaterial>(m_swap_chain_buffer_count);

        m_buffer_uber_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "uber", is_dynamic);
        m_buffer_uber_gpu->Create<BufferUber>(64);

        m_buffer_object_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "object", is_dynamic);
        m_buffer_object_gpu->Create<BufferObject>(m_swap_chain_buffer_count);

        m_buffer_light_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device, "light", is_dynamic);
        m_buffer_light_gpu->Create<BufferLight>(m_swap_chain_buffer_count);
    }

    void Renderer::CreateDepthStencilStates()
    {
        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        m_depth_stencil_off_off     = make_shared<RHI_DepthStencilState>(m_rhi_device, false,   false,  RHI_Comparison_Never,    false, false,  RHI_Comparison_Never);  // no depth or stencil
        m_depth_stencil_on_off_w    = make_shared<RHI_DepthStencilState>(m_rhi_device, true,    true,   GetComparisonFunction(), false, false,  RHI_Comparison_Never);  // depth
        m_depth_stencil_on_off_r    = make_shared<RHI_DepthStencilState>(m_rhi_device, true,    false,  GetComparisonFunction(), false, false,  RHI_Comparison_Never);  // depth
        m_depth_stencil_off_on_r    = make_shared<RHI_DepthStencilState>(m_rhi_device, false,   false,  RHI_Comparison_Never,    true,  false,  RHI_Comparison_Equal);  // depth + stencil
        m_depth_stencil_on_on_w     = make_shared<RHI_DepthStencilState>(m_rhi_device, true,    true,   GetComparisonFunction(), true,  true,   RHI_Comparison_Always); // depth + stencil
    }

    void Renderer::CreateRasterizerStates()
    {
        float depth_bias                = GetOption(Render_ReverseZ) ? -m_depth_bias : m_depth_bias;
        float depth_bias_slope_scaled   = GetOption(Render_ReverseZ) ? -m_depth_bias_slope_scaled : m_depth_bias_slope_scaled;

        m_rasterizer_cull_back_solid        = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back, RHI_Fill_Solid,     true,  false, false, false);
        m_rasterizer_cull_back_wireframe    = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back, RHI_Fill_Wireframe, true,  false, false, true);
        m_rasterizer_light_point_spot       = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back, RHI_Fill_Solid,     true,  false, false, false, depth_bias,         m_depth_bias_clamp, depth_bias_slope_scaled);
        m_rasterizer_light_directional      = make_shared<RHI_RasterizerState>(m_rhi_device, RHI_Cull_Back, RHI_Fill_Solid,     false, false, false, false, depth_bias * 0.1f,  m_depth_bias_clamp, depth_bias_slope_scaled);
    }

    void Renderer::CreateBlendStates()
    {
        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        m_blend_disabled    = make_shared<RHI_BlendState>(m_rhi_device, false);
        m_blend_alpha       = make_shared<RHI_BlendState>(m_rhi_device, true, RHI_Blend_Src_Alpha,  RHI_Blend_Inv_Src_Alpha,    RHI_Blend_Operation_Add, RHI_Blend_One, RHI_Blend_One, RHI_Blend_Operation_Add);
        m_blend_additive    = make_shared<RHI_BlendState>(m_rhi_device, true, RHI_Blend_One,        RHI_Blend_One,              RHI_Blend_Operation_Add, RHI_Blend_One, RHI_Blend_One, RHI_Blend_Operation_Add);
    }

    void Renderer::CreateSamplers()
    {
        m_sampler_compare_depth     = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  RHI_Sampler_Address_Clamp,  GetOption(Render_ReverseZ) ? RHI_Comparison_Greater : RHI_Comparison_Less, false, true);
        m_sampler_point_clamp       = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_POINT,     RHI_Sampler_Address_Clamp,  RHI_Comparison_Always);
        m_sampler_bilinear_clamp    = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  RHI_Sampler_Address_Clamp,  RHI_Comparison_Always);
        m_sampler_bilinear_wrap     = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,  RHI_Sampler_Address_Wrap,   RHI_Comparison_Always);
        m_sampler_trilinear_clamp   = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR, RHI_Sampler_Address_Clamp,  RHI_Comparison_Always);
        m_sampler_anisotropic_wrap  = make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR, RHI_Sampler_Address_Wrap,   RHI_Comparison_Always, true);
    }

    void Renderer::CreateRenderTextures()
    {
        uint32_t width  = static_cast<uint32_t>(m_resolution.x);
        uint32_t height = static_cast<uint32_t>(m_resolution.y);

        if ((width / 4) == 0 || (height / 4) == 0)
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        Flush();

        // G-Buffer
        // Stencil is used to mask transparent objects and also has a read only version
        // From and below Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding
        m_render_targets[RendererRt::Gbuffer_Albedo]   = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R8G8B8A8_Unorm,       1, 0,                                   "rt_gbuffer_albedo");
        m_render_targets[RendererRt::Gbuffer_Normal]   = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float,   1, 0,                                   "rt_gbuffer_normal");
        m_render_targets[RendererRt::Gbuffer_Material] = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R8G8B8A8_Unorm,       1, 0,                                   "rt_gbuffer_material");
        m_render_targets[RendererRt::Gbuffer_Velocity] = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16_Float,         1, 0,                                   "rt_gbuffer_velocity");
        m_render_targets[RendererRt::Gbuffer_Depth]    = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_D32_Float_S8X24_Uint, 1, RHI_Texture_DepthStencilReadOnly,    "gbuffer_depth");

        // Light
        m_render_targets[RendererRt::Light_Diffuse]                = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_light_diffuse");
        m_render_targets[RendererRt::Light_Diffuse_Transparent]    = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_light_diffuse_transparent");
        m_render_targets[RendererRt::Light_Specular]               = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_light_specular");
        m_render_targets[RendererRt::Light_Specular_Transparent]   = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_light_specular_transparent");
        m_render_targets[RendererRt::Light_Volumetric]             = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_light_volumetric");

        // BRDF Specular Lut
        m_render_targets[RendererRt::Brdf_Specular_Lut] = make_unique<RHI_Texture2D>(m_context, 400, 400, RHI_Format_R8G8_Unorm, 1, 0, "rt_brdf_specular_lut");
        m_brdf_specular_lut_rendered = false;

        // Main HDR and LDR textures with secondary copies (necessary for ping-ponging during post-processing)
        m_render_targets[RendererRt::Frame_Hdr]      = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_hdr");  // Investigate using less bits but have an alpha channel
        m_render_targets[RendererRt::Frame_Ldr]      = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_ldr");  // Investigate using less bits but have an alpha channel
        m_render_targets[RendererRt::Frame_Hdr_2]    = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_hdr2"); // Investigate using less bits but have an alpha channel
        m_render_targets[RendererRt::Frame_Ldr_2]    = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_ldr2"); // Investigate using less bits but have an alpha channel

         // Depth of Field
        m_render_targets[RendererRt::Dof_Half]     = make_unique<RHI_Texture2D>(m_context, width * 0.5f, height * 0.5f, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_dof_half");   // Investigate using less bits but have an alpha channel
        m_render_targets[RendererRt::Dof_Half_2]   = make_unique<RHI_Texture2D>(m_context, width * 0.5f, height * 0.5f, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_dof_half_2"); // Investigate using less bits but have an alpha channel

        // HBAO
        m_render_targets[RendererRt::Hbao]   = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R8_Unorm, 1, 0, "rt_hbao_noisy");
        m_render_targets[RendererRt::Hbao_Blurred]         = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R8_Unorm, 1, 0, "rt_hbao");

        // SSGI
        m_render_targets[RendererRt::Ssgi] = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_ssgi");

        // SSR
        m_render_targets[RendererRt::Ssr] = make_shared<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16_Float, 1, RHI_Texture_Storage, "rt_ssr");

        // Accumulation
        m_render_targets[RendererRt::Accumulation_Taa]     = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R16G16B16A16_Float, 1, 0, "rt_accumulation_taa");
        m_render_targets[RendererRt::Accumulation_Ssgi]    = make_unique<RHI_Texture2D>(m_context, width, height, RHI_Format_R11G11B10_Float, 1, 0, "rt_accumulation_ssgi");

        // Bloom
        {
            // Create as many bloom textures as required to scale down to or below 16px (in any dimension)
            m_render_tex_bloom.clear();
            m_render_tex_bloom.emplace_back(make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, RHI_Format_R11G11B10_Float, 1, 0, "rt_bloom"));
            while (m_render_tex_bloom.back()->GetWidth() > 16 && m_render_tex_bloom.back()->GetHeight() > 16)
            {
                m_render_tex_bloom.emplace_back(
                    make_unique<RHI_Texture2D>(
                        m_context,
                        m_render_tex_bloom.back()->GetWidth() / 2,
                        m_render_tex_bloom.back()->GetHeight() / 2,
                        RHI_Format_R11G11B10_Float,
                        1, 0, "rt_bloom_downscaled"
                        )
                );
            }
        }
    }

    void Renderer::CreateShaders()
    {
        // Get standard shader directory
        const auto dir_shaders = m_resource_cache->GetDataDirectory(Asset_Shaders) + "/";

        // Shader which compile different variations when needed
        m_shaders[RendererShader::Gbuffer_P] = make_shared<ShaderGBuffer>(m_context);
        m_shaders[RendererShader::Light_C]   = make_shared<ShaderLight>(m_context);

        // G-Buffer
        m_shaders[RendererShader::Gbuffer_V] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Gbuffer_V]->CompileAsync<RHI_Vertex_PosTexNorTan>(RHI_Shader_Vertex, dir_shaders + "GBuffer.hlsl");

        // Quad
        {
            // Vertex
            m_shaders[RendererShader::Quad_V] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Quad_V]->CompileAsync<RHI_Vertex_PosTex>(RHI_Shader_Vertex, dir_shaders + "Quad.hlsl");

            // Pixel - Just a texture pass
            m_shaders[RendererShader::Texture_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Texture_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Quad.hlsl");
        }

        // Depth Vertex
        m_shaders[RendererShader::Depth_V] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Depth_V]->CompileAsync<RHI_Vertex_PosTex>(RHI_Shader_Vertex, dir_shaders + "Depth.hlsl");
        m_shaders[RendererShader::Depth_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Depth_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Depth.hlsl");

        // BRDF - Specular Lut
        m_shaders[RendererShader::BrdfSpecularLut_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::BrdfSpecularLut_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "BRDF_SpecularLut.hlsl");

        // Copy
        m_shaders[RendererShader::Copy_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Copy_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Copy.hlsl");

        // Blur
        {
            // Box
            m_shaders[RendererShader::BlurBox_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BlurBox_P]->AddDefine("PASS_BLUR_BOX");
            m_shaders[RendererShader::BlurBox_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Blur.hlsl");

            // Gaussian
            m_shaders[RendererShader::BlurGaussian_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BlurGaussian_P]->AddDefine("PASS_BLUR_GAUSSIAN");
            m_shaders[RendererShader::BlurGaussian_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Blur.hlsl");

            // Bilateral Gaussian
            m_shaders[RendererShader::BlurGaussianBilateral_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BlurGaussianBilateral_P]->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
            m_shaders[RendererShader::BlurGaussianBilateral_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Blur.hlsl");
        }

        // Bloom
        {
            // Downsample luminance
            m_shaders[RendererShader::BloomDownsampleLuminance_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomDownsampleLuminance_C]->AddDefine("DOWNSAMPLE_LUMINANCE");
            m_shaders[RendererShader::BloomDownsampleLuminance_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Bloom.hlsl");

            // Downsample anti-flicker
            m_shaders[RendererShader::BloomDownsample_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomDownsample_C]->AddDefine("DOWNSAMPLE");
            m_shaders[RendererShader::BloomDownsample_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Bloom.hlsl");

            // Upsample blend (with previous mip)
            m_shaders[RendererShader::BloomUpsampleBlendMip_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomUpsampleBlendMip_C]->AddDefine("UPSAMPLE_BLEND_MIP");
            m_shaders[RendererShader::BloomUpsampleBlendMip_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Bloom.hlsl");

            // Upsample blend (with frame)
            m_shaders[RendererShader::BloomUpsampleBlendFrame_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::BloomUpsampleBlendFrame_C]->AddDefine("UPSAMPLE_BLEND_FRAME");
            m_shaders[RendererShader::BloomUpsampleBlendFrame_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Bloom.hlsl");
        }

        // Film grain
        m_shaders[RendererShader::FilmGrain_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::FilmGrain_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "FilmGrain.hlsl");

        // Sharpening
        m_shaders[RendererShader::Sharpening_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Sharpening_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Sharpening.hlsl");

        // Chromatic aberration
        m_shaders[RendererShader::ChromaticAberration_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::ChromaticAberration_C]->AddDefine("PASS_CHROMATIC_ABERRATION");
        m_shaders[RendererShader::ChromaticAberration_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "ChromaticAberration.hlsl");

        // Tone-mapping
        m_shaders[RendererShader::ToneMapping_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::ToneMapping_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "ToneMapping.hlsl");

        // Gamma correction
        m_shaders[RendererShader::GammaCorrection_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::GammaCorrection_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "GammaCorrection.hlsl");

        // Anti-aliasing
        {
            // TAA
            m_shaders[RendererShader::Taa_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Taa_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "TemporalAntialiasing.hlsl");

            // Luminance (encodes luminance into alpha channel)
            m_shaders[RendererShader::Fxaa_Luminance_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Fxaa_Luminance_C]->AddDefine("LUMINANCE");
            m_shaders[RendererShader::Fxaa_Luminance_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "FXAA.hlsl");

            // FXAA
            m_shaders[RendererShader::Fxaa_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Fxaa_C]->AddDefine("FXAA");
            m_shaders[RendererShader::Fxaa_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "FXAA.hlsl");
        }

        // Depth of Field
        {
            m_shaders[RendererShader::Dof_DownsampleCoc_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_DownsampleCoc_C]->AddDefine("DOWNSAMPLE_CIRCLE_OF_CONFUSION");
            m_shaders[RendererShader::Dof_DownsampleCoc_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "DepthOfField.hlsl");

            m_shaders[RendererShader::Dof_Bokeh_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_Bokeh_C]->AddDefine("BOKEH");
            m_shaders[RendererShader::Dof_Bokeh_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "DepthOfField.hlsl");

            m_shaders[RendererShader::Dof_Tent_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_Tent_C]->AddDefine("TENT");
            m_shaders[RendererShader::Dof_Tent_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "DepthOfField.hlsl");

            m_shaders[RendererShader::Dof_UpscaleBlend_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Dof_UpscaleBlend_C]->AddDefine("UPSCALE_BLEND");
            m_shaders[RendererShader::Dof_UpscaleBlend_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "DepthOfField.hlsl");
        }

        // Motion Blur
        m_shaders[RendererShader::MotionBlur_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::MotionBlur_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "MotionBlur.hlsl");

        // Dithering
        m_shaders[RendererShader::Dithering_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Dithering_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Dithering.hlsl");

        // HBAO
        m_shaders[RendererShader::Hbao_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Hbao_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "HBAO.hlsl");

        // SSGI
        m_shaders[RendererShader::Ssgi_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Ssgi_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "SSGI.hlsl");

        // SSR
        m_shaders[RendererShader::Ssr_C] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Ssr_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "SSR.hlsl");

        // Entity
        m_shaders[RendererShader::Entity_V] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Entity_V]->CompileAsync<RHI_Vertex_PosTexNorTan>(RHI_Shader_Vertex, dir_shaders + "Entity.hlsl");

        // Entity - Transform
        m_shaders[RendererShader::Entity_Transform_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Entity_Transform_P]->AddDefine("TRANSFORM");
        m_shaders[RendererShader::Entity_Transform_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Entity.hlsl");

        // Entity - Outline
        m_shaders[RendererShader::Entity_Outline_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Entity_Outline_P]->AddDefine("OUTLINE");
        m_shaders[RendererShader::Entity_Outline_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Entity.hlsl");

        // Composition
        {
            m_shaders[RendererShader::Composition_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Composition_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Composition.hlsl");

            m_shaders[RendererShader::Composition_Transparent_P] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::Composition_Transparent_P]->AddDefine("TRANSPARENT");
            m_shaders[RendererShader::Composition_Transparent_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Composition.hlsl");
        }

        // Font
        m_shaders[RendererShader::Font_V] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Font_V]->CompileAsync<RHI_Vertex_PosTex>(RHI_Shader_Vertex, dir_shaders + "Font.hlsl");
        m_shaders[RendererShader::Font_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Font_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Font.hlsl");

        // Color
        m_shaders[RendererShader::Color_V] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Color_V]->CompileAsync<RHI_Vertex_PosCol>(RHI_Shader_Vertex, dir_shaders + "Color.hlsl");
        m_shaders[RendererShader::Color_P] = make_shared<RHI_Shader>(m_context);
        m_shaders[RendererShader::Color_P]->CompileAsync(RHI_Shader_Pixel, dir_shaders + "Color.hlsl");

        // Debug
        {
            // Normal
            m_shaders[RendererShader::DebugNormal_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::DebugNormal_C]->AddDefine("NORMAL");
            m_shaders[RendererShader::DebugNormal_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Debug.hlsl");

            // Velocity
            m_shaders[RendererShader::DebugVelocity_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::DebugVelocity_C]->AddDefine("VELOCITY");
            m_shaders[RendererShader::DebugVelocity_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Debug.hlsl");

            // R channel
            m_shaders[RendererShader::DebugChannelR_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::DebugChannelR_C]->AddDefine("R_CHANNEL");
            m_shaders[RendererShader::DebugChannelR_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Debug.hlsl");

            // A channel
            m_shaders[RendererShader::DebugChannelA_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::DebugChannelA_C]->AddDefine("A_CHANNEL");
            m_shaders[RendererShader::DebugChannelA_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Debug.hlsl");

            // A channel with gamma correction
            m_shaders[RendererShader::DebugChannelRgbGammaCorrect_C] = make_shared<RHI_Shader>(m_context);
            m_shaders[RendererShader::DebugChannelRgbGammaCorrect_C]->AddDefine("RGB_CHANNEL_GAMMA_CORRECT");
            m_shaders[RendererShader::DebugChannelRgbGammaCorrect_C]->CompileAsync(RHI_Shader_Compute, dir_shaders + "Debug.hlsl");
        }
    }

    void Renderer::CreateFonts()
    {
        // Get standard font directory
        const auto dir_font = m_resource_cache->GetDataDirectory(Asset_Fonts) + "/";

        // Load a font (used for performance metrics)
        m_font = make_unique<Font>(m_context, dir_font + "CalibriBold.ttf", 12, Vector4(0.8f, 0.8f, 0.8f, 1.0f));
    }

    void Renderer::CreateTextures()
    {
        // Get standard texture directory
        const auto dir_texture = m_resource_cache->GetDataDirectory(Asset_Textures) + "/";

        auto generate_mipmaps = false;

        m_default_tex_white = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_default_tex_white->LoadFromFile(dir_texture + "white.png");

        m_default_tex_black = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_default_tex_black->LoadFromFile(dir_texture + "black.png");

        m_default_tex_transparent = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_default_tex_transparent->LoadFromFile(dir_texture + "transparent.png");

        // Gizmo icons
        m_gizmo_tex_light_directional = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_gizmo_tex_light_directional->LoadFromFile(dir_texture + "sun.png");

        m_gizmo_tex_light_point = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_gizmo_tex_light_point->LoadFromFile(dir_texture + "light_bulb.png");

        m_gizmo_tex_light_spot = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
        m_gizmo_tex_light_spot->LoadFromFile(dir_texture + "flashlight.png");
    }
}
