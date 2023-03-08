#/*
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

//= INCLUDES ============================
#include "pch.h"
#include "Renderer.h"
#include "Geometry.h"
#include "Grid.h"
#include "Font/Font.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_Texture2DArray.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_TextureCube.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_FSR2.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

#define render_target(rt_enum) m_render_targets[static_cast<uint8_t>(rt_enum)]
#define shader(rt_enum)        m_shaders[static_cast<uint8_t>(rt_enum)]

namespace Spartan
{
    void Renderer::CreateConstantBuffers()
    {
        SP_ASSERT(m_rhi_device != nullptr);

        m_cb_frame_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device.get(), "frame");
        m_cb_frame_gpu->Create<Cb_Frame>(8000);

        m_cb_uber_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device.get(), "uber");
        m_cb_uber_gpu->Create<Cb_Uber>(30000);

        m_cb_light_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device.get(), "light");
        m_cb_light_gpu->Create<Cb_Light>(8000);

        m_cb_material_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device.get(), "material");
        m_cb_material_gpu->Create<Cb_Material>(4000); // Nvidia failed to allocate beyond this point
    }

    void Renderer::CreateStructuredBuffers()
    {
        const uint32_t offset_count = 32;
        m_sb_spd_counter = make_shared<RHI_StructuredBuffer>(m_rhi_device.get(), static_cast<uint32_t>(sizeof(uint32_t)), offset_count, "spd_counter");
    }

    void Renderer::CreateDepthStencilStates()
    {
        SP_ASSERT(m_rhi_device != nullptr);

        RHI_Comparison_Function reverse_z_aware_comp_func = GetOption<bool>(RendererOption::ReverseZ) ? RHI_Comparison_Function::GreaterEqual : RHI_Comparison_Function::LessEqual;

        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        m_depth_stencil_off_off = make_shared<RHI_DepthStencilState>(m_rhi_device.get(), false, false, RHI_Comparison_Function::Never, false, false, RHI_Comparison_Function::Never);  // no depth or stencil
        m_depth_stencil_rw_off  = make_shared<RHI_DepthStencilState>(m_rhi_device.get(), true,  true,  reverse_z_aware_comp_func,      false, false, RHI_Comparison_Function::Never);  // depth
        m_depth_stencil_r_off   = make_shared<RHI_DepthStencilState>(m_rhi_device.get(), true,  false, reverse_z_aware_comp_func,      false, false, RHI_Comparison_Function::Never);  // depth
        m_depth_stencil_off_r   = make_shared<RHI_DepthStencilState>(m_rhi_device.get(), false, false, RHI_Comparison_Function::Never, true,  false, RHI_Comparison_Function::Equal);  // depth + stencil
        m_depth_stencil_rw_w    = make_shared<RHI_DepthStencilState>(m_rhi_device.get(), true,  true,  reverse_z_aware_comp_func,      false, true,  RHI_Comparison_Function::Always); // depth + stencil
    }

    void Renderer::CreateRasterizerStates()
    {
        SP_ASSERT(m_rhi_device != nullptr);

        float depth_bias              = GetOption<bool>(RendererOption::ReverseZ) ? -m_depth_bias : m_depth_bias;
        float depth_bias_slope_scaled = GetOption<bool>(RendererOption::ReverseZ) ? -m_depth_bias_slope_scaled : m_depth_bias_slope_scaled;

        m_rasterizer_cull_back_solid     = make_shared<RHI_RasterizerState>(m_rhi_device.get(), RHI_CullMode::Back, RHI_PolygonMode::Solid,     true,  false, false);
        m_rasterizer_cull_back_wireframe = make_shared<RHI_RasterizerState>(m_rhi_device.get(), RHI_CullMode::Back, RHI_PolygonMode::Wireframe, true,  false, true);
        m_rasterizer_cull_none_solid     = make_shared<RHI_RasterizerState>(m_rhi_device.get(), RHI_CullMode::Back, RHI_PolygonMode::Solid,     true, false, false);
        m_rasterizer_light_point_spot    = make_shared<RHI_RasterizerState>(m_rhi_device.get(), RHI_CullMode::Back, RHI_PolygonMode::Solid,     true,  false, false, depth_bias,        m_depth_bias_clamp, depth_bias_slope_scaled);
        m_rasterizer_light_directional   = make_shared<RHI_RasterizerState>(m_rhi_device.get(), RHI_CullMode::Back, RHI_PolygonMode::Solid,     false, false, false, depth_bias * 0.1f, m_depth_bias_clamp, depth_bias_slope_scaled);
    }

    void Renderer::CreateBlendStates()
    {
        SP_ASSERT(m_rhi_device != nullptr);

        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        m_blend_disabled = make_shared<RHI_BlendState>(m_rhi_device.get(), false);
        m_blend_alpha    = make_shared<RHI_BlendState>(m_rhi_device.get(), true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        m_blend_additive = make_shared<RHI_BlendState>(m_rhi_device.get(), true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers(const bool create_only_anisotropic /*= false*/)
    {
        SP_ASSERT(m_rhi_device != nullptr);

        float anisotropy                         = GetOption<float>(RendererOption::Anisotropy);
        RHI_Comparison_Function depth_comparison = GetOption<bool>(RendererOption::ReverseZ) ? RHI_Comparison_Function::Greater : RHI_Comparison_Function::Less;

        // Compute mip bias
        float mip_bias = 0.0f;
        if (m_resolution_output.x > m_resolution_render.x)
        {
            // Progressively negative values when upsampling for increased texture fidelity
            mip_bias = log2(m_resolution_render.x / m_resolution_output.x) - 1.0f;
        }

        // sampler parameters: minification, magnification, mip, sampler address mode, comparison, anisotropy, comparison enabled, mip lod bias
        if (!create_only_anisotropic)
        {
            m_sampler_compare_depth   = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Clamp, depth_comparison, 0.0f, true);
            m_sampler_point_clamp     = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
            m_sampler_point_wrap      = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Wrap,  RHI_Comparison_Function::Always);
            m_sampler_bilinear_clamp  = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
            m_sampler_bilinear_wrap   = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Nearest, RHI_Sampler_Address_Mode::Wrap,  RHI_Comparison_Function::Always);
            m_sampler_trilinear_clamp = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Mipmap_Mode::Linear,  RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
        }

        m_sampler_anisotropic_wrap = make_shared<RHI_Sampler>(m_rhi_device, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Mipmap_Mode::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_bias);

        SP_LOG_INFO("Mip bias set to %f", mip_bias);
    }

    void Renderer::CreateRenderTextures(const bool create_render, const bool create_output, const bool create_fixed, const bool create_dynamic)
    {
        // Get render resolution
        uint32_t width_render  = static_cast<uint32_t>(m_resolution_render.x);
        uint32_t height_render = static_cast<uint32_t>(m_resolution_render.y);

        // Get output resolution
        uint32_t width_output  = static_cast<uint32_t>(m_resolution_output.x);
        uint32_t height_output = static_cast<uint32_t>(m_resolution_output.y);

        // Deduce how many mips are required to scale down any dimension close to 16px (or exactly)
        uint32_t mip_count          = 1;
        uint32_t width              = width_render;
        uint32_t height             = height_render;
        uint32_t smallest_dimension = 1;
        while (width > smallest_dimension && height > smallest_dimension)
        {
            width /= 2;
            height /= 2;
            mip_count++;
        }

        // Notes.
        // Gbuffer_Normal: Any format with or below 8 bits per channel, will produce banding.

        // Render resolution
        if (create_render)
        {
            // Frame (HDR) - Mips are used to emulate roughness when blending with transparent surfaces
            render_target(RendererTexture::frame_render)   = make_unique<RHI_Texture2D>(m_context, width_render, height_render, mip_count, RHI_Format_R16G16B16A16_Float, RHI_Texture_RenderTarget | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearOrBlit, "rt_frame_render");
            render_target(RendererTexture::frame_render_2) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, mip_count, RHI_Format_R16G16B16A16_Float, RHI_Texture_RenderTarget | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearOrBlit, "rt_frame_render_2");

            // G-Buffer
            render_target(RendererTexture::gbuffer_albedo)   = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8G8B8A8_Unorm,     RHI_Texture_RenderTarget | RHI_Texture_Srv,                                     "rt_gbuffer_albedo");
            render_target(RendererTexture::gbuffer_normal)   = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_RenderTarget | RHI_Texture_Srv,                                     "rt_gbuffer_normal");
            render_target(RendererTexture::gbuffer_material) = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8G8B8A8_Unorm,     RHI_Texture_RenderTarget | RHI_Texture_Srv,                                     "rt_gbuffer_material");
            render_target(RendererTexture::gbuffer_velocity) = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R16G16_Float,       RHI_Texture_RenderTarget | RHI_Texture_Srv,                                     "rt_gbuffer_velocity");
            render_target(RendererTexture::gbuffer_depth)    = make_shared<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_D32_Float,          RHI_Texture_RenderTarget | RHI_Texture_Srv | RHI_Texture_RenderTarget_ReadOnly, "rt_gbuffer_depth");

            // Light
            render_target(RendererTexture::light_diffuse)              = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_light_diffuse");
            render_target(RendererTexture::light_diffuse_transparent)  = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_light_diffuse_transparent");
            render_target(RendererTexture::light_specular)             = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_light_specular");
            render_target(RendererTexture::light_specular_transparent) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_light_specular_transparent");
            render_target(RendererTexture::light_volumetric)           = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_light_volumetric");

            // SSR - Mips are used to emulate roughness for surfaces which require it
            render_target(RendererTexture::ssr) = make_shared<RHI_Texture2D>(m_context, width_render, height_render, mip_count, RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "rt_ssr");

            // SSAO
            render_target(RendererTexture::ssao)    = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R16G16B16A16_Snorm, RHI_Texture_Uav | RHI_Texture_Srv, "rt_ssao");
            render_target(RendererTexture::ssao_gi) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R16G16B16A16_Snorm, RHI_Texture_Uav | RHI_Texture_Srv, "rt_ssao_gi");

            // Dof
            render_target(RendererTexture::dof_half)   = make_unique<RHI_Texture2D>(m_context, width_render / 2, height_render / 2, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_dof_half");
            render_target(RendererTexture::dof_half_2) = make_unique<RHI_Texture2D>(m_context, width_render / 2, height_render / 2, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_dof_half_2");

            // FSR 2 masks
            render_target(RendererTexture::fsr2_mask_reactive)     = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8_Unorm, RHI_Texture_RenderTarget | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_fsr2_reactive_mask");
            render_target(RendererTexture::fsr2_mask_transparency) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8_Unorm, RHI_Texture_RenderTarget | RHI_Texture_Srv, "rt_fsr2_transparency_mask");

            // Selection outline
            render_target(RendererTexture::outline) = make_unique<RHI_Texture2D>(m_context, width_render, height_render, 1, RHI_Format_R8G8B8A8_Unorm, RHI_Texture_RenderTarget | RHI_Texture_Srv | RHI_Texture_Uav, "rt_outline");
        }

        // Output resolution
        if (create_output)
        {
            // Frame (LDR)
            render_target(RendererTexture::frame_output)   = make_unique<RHI_Texture2D>(m_context, width_output, height_output, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_RenderTarget | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_frame_output");
            render_target(RendererTexture::frame_output_2) = make_unique<RHI_Texture2D>(m_context, width_output, height_output, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_RenderTarget | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearOrBlit, "rt_frame_output_2");

            // Bloom
            render_target(RendererTexture::bloom) = make_shared<RHI_Texture2D>(m_context, width_output, height_output, mip_count, RHI_Format_R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "rt_bloom");
        }

        // Fixed resolution
        if (create_fixed)
        {
            render_target(RendererTexture::brdf_specular_lut) = make_unique<RHI_Texture2D>(m_context, 400, 400, 1, RHI_Format_R8G8_Unorm, RHI_Texture_Uav | RHI_Texture_Srv, "rt_brdf_specular_lut");
            m_brdf_specular_lut_rendered = false;
        }

        // Dynamic resolution
        if (create_dynamic)
        {
            // Blur
            bool is_output_larger = width_output > width_render && height_output > height_render;
            uint32_t width        = is_output_larger ? width_output : width_render;
            uint32_t height       = is_output_larger ? height_output : height_render;
            render_target(RendererTexture::blur) = make_unique<RHI_Texture2D>(m_context, width, height, 1, RHI_Format_R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_blur");
        }

        RHI_FSR2::OnResolutionChange(m_rhi_device.get(), m_resolution_render, m_resolution_output);
    }

    void Renderer::CreateShaders()
    {
        const bool async        = true;
        const string shader_dir = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "\\";

        // G-Buffer
        shader(RendererShader::gbuffer_v) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::gbuffer_v)->Compile(RHI_Shader_Vertex, shader_dir + "g_buffer.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        shader(RendererShader::gbuffer_p) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::gbuffer_p)->Compile(RHI_Shader_Pixel, shader_dir + "g_buffer.hlsl", async);

        // Light
        shader(RendererShader::light_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::light_c)->Compile(RHI_Shader_Compute, shader_dir + "light.hlsl", async);

        // Triangle & Quad
        {
            shader(RendererShader::fullscreen_triangle_v) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::fullscreen_triangle_v)->Compile(RHI_Shader_Vertex, shader_dir + "fullscreen_triangle.hlsl", async, RHI_Vertex_Type::Undefined);

            shader(RendererShader::quad_v) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::quad_v)->Compile(RHI_Shader_Vertex, shader_dir + "quad.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        }

        // Depth prepass
        {
            shader(RendererShader::depth_prepass_v) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::depth_prepass_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_prepass.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(RendererShader::depth_prepass_p) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::depth_prepass_p)->Compile(RHI_Shader_Pixel, shader_dir + "depth_prepass.hlsl", async);
        }

        // Depth light
        {
            shader(RendererShader::depth_light_V) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::depth_light_V)->Compile(RHI_Shader_Vertex, shader_dir + "depth_light.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(RendererShader::depth_light_p) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::depth_light_p)->Compile(RHI_Shader_Pixel, shader_dir + "depth_light.hlsl", async);
        }

        // Font
        shader(RendererShader::font_v) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::font_v)->Compile(RHI_Shader_Vertex, shader_dir + "font.hlsl", async, RHI_Vertex_Type::PosUv);
        shader(RendererShader::font_p) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::font_p)->Compile(RHI_Shader_Pixel, shader_dir + "font.hlsl", async);

        // Line
        shader(RendererShader::line_v) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::line_v)->Compile(RHI_Shader_Vertex, shader_dir + "line.hlsl", async, RHI_Vertex_Type::PosCol);
        shader(RendererShader::line_p) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::line_p)->Compile(RHI_Shader_Pixel, shader_dir + "line.hlsl", async);

        // Outline
        shader(RendererShader::outline_v) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::outline_v)->Compile(RHI_Shader_Vertex, shader_dir + "outline.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        shader(RendererShader::outline_p) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::outline_p)->Compile(RHI_Shader_Pixel, shader_dir + "outline.hlsl", async);
        shader(RendererShader::outline_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::outline_c)->Compile(RHI_Shader_Compute, shader_dir + "outline.hlsl", async);

        // Reflection probe
        shader(RendererShader::reflection_probe_v) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::reflection_probe_v)->Compile(RHI_Shader_Vertex, shader_dir + "reflection_probe.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        shader(RendererShader::reflection_probe_p) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::reflection_probe_p)->Compile(RHI_Shader_Pixel, shader_dir + "reflection_probe.hlsl", async);

        // Debug
        {
            shader(RendererShader::debug_reflection_probe_v) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::debug_reflection_probe_v)->Compile(RHI_Shader_Vertex, shader_dir + "debug_reflection_probe.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
            shader(RendererShader::debug_reflection_probe_p) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::debug_reflection_probe_p)->Compile(RHI_Shader_Pixel, shader_dir + "debug_reflection_probe.hlsl", async);
        }

        // Copy
        {
            shader(RendererShader::copy_point_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::copy_point_c)->AddDefine("COMPUTE");
            shader(RendererShader::copy_point_c)->Compile(RHI_Shader_Compute, shader_dir + "copy.hlsl", async);

            shader(RendererShader::copy_bilinear_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::copy_bilinear_c)->AddDefine("COMPUTE");
            shader(RendererShader::copy_bilinear_c)->AddDefine("BILINEAR");
            shader(RendererShader::copy_bilinear_c)->Compile(RHI_Shader_Compute, shader_dir + "copy.hlsl", async);

            shader(RendererShader::copy_point_p) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::copy_point_p)->AddDefine("PIXEL");
            shader(RendererShader::copy_point_p)->AddDefine("BILINEAR");
            shader(RendererShader::copy_point_p)->Compile(RHI_Shader_Pixel, shader_dir + "copy.hlsl", async);

            shader(RendererShader::copy_bilinear_p) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::copy_bilinear_p)->AddDefine("PIXEL");
            shader(RendererShader::copy_bilinear_p)->AddDefine("BILINEAR");
            shader(RendererShader::copy_bilinear_p)->Compile(RHI_Shader_Pixel, shader_dir + "copy.hlsl", async);
        }

        // Blur
        {
            // Gaussian
            shader(RendererShader::blur_gaussian_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::blur_gaussian_c)->AddDefine("PASS_BLUR_GAUSSIAN");
            shader(RendererShader::blur_gaussian_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);

            // Gaussian bilateral 
            shader(RendererShader::blur_gaussian_bilaterial_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::blur_gaussian_bilaterial_c)->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
            shader(RendererShader::blur_gaussian_bilaterial_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);
        }

        // Bloom
        {
            // Downsample luminance
            shader(RendererShader::bloom_luminance_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::bloom_luminance_c)->AddDefine("LUMINANCE");
            shader(RendererShader::bloom_luminance_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);

            // Upsample blend (with previous mip)
            shader(RendererShader::bloom_upsample_blend_mip_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::bloom_upsample_blend_mip_c)->AddDefine("UPSAMPLE_BLEND_MIP");
            shader(RendererShader::bloom_upsample_blend_mip_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);

            // Upsample blend (with frame)
            shader(RendererShader::bloom_blend_frame_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::bloom_blend_frame_c)->AddDefine("BLEND_FRAME");
            shader(RendererShader::bloom_blend_frame_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);
        }

        // Film grain
        shader(RendererShader::film_grain_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::film_grain_c)->Compile(RHI_Shader_Compute, shader_dir + "film_grain.hlsl", async);

        // Chromatic aberration
        shader(RendererShader::chromatic_aberration_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::chromatic_aberration_c)->AddDefine("PASS_CHROMATIC_ABERRATION");
        shader(RendererShader::chromatic_aberration_c)->Compile(RHI_Shader_Compute, shader_dir + "chromatic_aberration.hlsl", async);

        // Tone-mapping & gamma correction
        shader(RendererShader::tonemapping_gamma_correction_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::tonemapping_gamma_correction_c)->Compile(RHI_Shader_Compute, shader_dir + "tone_mapping_gamma_correction.hlsl", async);

        // FXAA
        shader(RendererShader::fxaa_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::fxaa_c)->Compile(RHI_Shader_Compute, shader_dir + "fxaa.hlsl", async);

        // Depth of Field
        {
            shader(RendererShader::dof_downsample_coc_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::dof_downsample_coc_c)->AddDefine("DOWNSAMPLE_CIRCLE_OF_CONFUSION");
            shader(RendererShader::dof_downsample_coc_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);

            shader(RendererShader::dof_bokeh_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::dof_bokeh_c)->AddDefine("BOKEH");
            shader(RendererShader::dof_bokeh_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);

            shader(RendererShader::dof_tent_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::dof_tent_c)->AddDefine("TENT");
            shader(RendererShader::dof_tent_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);

            shader(RendererShader::dof_upscale_blend_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::dof_upscale_blend_c)->AddDefine("UPSCALE_BLEND");
            shader(RendererShader::dof_upscale_blend_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);
        }

        // Motion Blur
        shader(RendererShader::motion_blur_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::motion_blur_c)->Compile(RHI_Shader_Compute, shader_dir + "motion_blur.hlsl", async);

        // Dithering
        shader(RendererShader::debanding_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::debanding_c)->Compile(RHI_Shader_Compute, shader_dir + "debanding.hlsl", async);

        // SSAO
        shader(RendererShader::ssao_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::ssao_c)->Compile(RHI_Shader_Compute, shader_dir + "ssao.hlsl", async);

        // Light
        {
            shader(RendererShader::light_composition_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::light_composition_c)->Compile(RHI_Shader_Compute, shader_dir + "light_composition.hlsl", async);

            shader(RendererShader::light_image_based_p) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::light_image_based_p)->Compile(RHI_Shader_Pixel, shader_dir + "light_image_based.hlsl", async);
        }

        // SSR
        shader(RendererShader::ssr_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::ssr_c)->Compile(RHI_Shader_Compute, shader_dir + "ssr.hlsl", async);

        // AMD FidelityFX CAS - Contrast Adaptive Sharpening
        shader(RendererShader::ffx_cas_c) = make_shared<RHI_Shader>(m_context);
        shader(RendererShader::ffx_cas_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\cas.hlsl", async);

        // Compiled immediately, they are needed the moment the engine starts.
        {
            // AMD FidelityFX SPD - Single Pass Downsample
            shader(RendererShader::ffx_spd_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::ffx_spd_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

            // BRDF - Specular Lut
            shader(RendererShader::brdf_specular_lut_c) = make_shared<RHI_Shader>(m_context);
            shader(RendererShader::brdf_specular_lut_c)->Compile(RHI_Shader_Compute, shader_dir + "brdf_specular_lut.hlsl", false); }
    }

    void Renderer::CreateFonts()
    {
        // Get standard font directory
        const string dir_font = ResourceCache::GetResourceDirectory(ResourceDirectory::Fonts) + "\\";

        // Load a font (used for performance metrics)
        m_font = make_unique<Font>(m_context, dir_font + "CalibriBold.ttf", 16, Vector4(0.8f, 0.8f, 0.8f, 1.0f));
    }

    void Renderer::CreateMeshes()
    {
        // Sphere
        {
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;
            Geometry::CreateSphere(&vertices, &indices, 0.2f, 20, 20);

            m_sphere_vertex_buffer = make_shared<RHI_VertexBuffer>(m_rhi_device.get(), false, "sphere");
            m_sphere_vertex_buffer->Create(vertices);

            m_sphere_index_buffer = make_shared<RHI_IndexBuffer>(m_rhi_device.get(), false, "sphere");
            m_sphere_index_buffer->Create(indices);
        }

        // Quad
        {
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;
            Geometry::CreateQuad(&vertices, &indices);

            m_quad_vertex_buffer = make_shared<RHI_VertexBuffer>(m_rhi_device.get(), false, "rectangle");
            m_quad_vertex_buffer->Create(vertices);

            m_quad_index_buffer = make_shared<RHI_IndexBuffer>(m_rhi_device.get(), false, "rectangle");
            m_quad_index_buffer->Create(indices);
        }

        // Buffer where all the lines are kept
        m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device.get(), true, "lines");

        // World grid
        m_gizmo_grid = make_unique<Grid>(m_rhi_device.get());
    }

    void Renderer::CreateTextures()
    {
        // Get standard texture directory
        const string dir_texture = ResourceCache::GetResourceDirectory(ResourceDirectory::Textures) + "\\";

        // Noise textures
        {
            m_tex_default_noise_normal = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_noise_normal");
            m_tex_default_noise_normal->LoadFromFile(dir_texture + "noise_normal.png");

            m_tex_default_noise_blue = static_pointer_cast<RHI_Texture>(make_shared<RHI_Texture2DArray>(m_context, RHI_Texture_Srv, "default_noise_blue"));
            m_tex_default_noise_blue->LoadFromFile(dir_texture + "noise_blue_0.png");
        }

        // Color textures
        {
            m_tex_default_white = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_white");
            m_tex_default_white->LoadFromFile(dir_texture + "white.png");

            m_tex_default_black = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_black");
            m_tex_default_black->LoadFromFile(dir_texture + "black.png");

            m_tex_default_transparent = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_transparent");
            m_tex_default_transparent->LoadFromFile(dir_texture + "transparent.png");
        }

        // Gizmo icons
        {
            m_tex_gizmo_light_directional = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_icon_light_directional");
            m_tex_gizmo_light_directional->LoadFromFile(dir_texture + "sun.png");

            m_tex_gizmo_light_point = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_icon_light_point");
            m_tex_gizmo_light_point->LoadFromFile(dir_texture + "light_bulb.png");

            m_tex_gizmo_light_spot = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv, "default_icon_light_spot");
            m_tex_gizmo_light_spot->LoadFromFile(dir_texture + "flashlight.png");
        }
    }
}
