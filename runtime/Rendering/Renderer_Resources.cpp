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
#include "Window.h"
#include "Renderer.h"
#include "Geometry.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_Texture2DArray.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_AMD_FidelityFX.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommandPool.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        // graphics states
        array<shared_ptr<RHI_RasterizerState>, 5>   m_rasterizer_states;
        array<shared_ptr<RHI_DepthStencilState>, 5> m_depth_stencil_states;
        array<shared_ptr<RHI_BlendState>, 3>        m_blend_states;

        // renderer resources
        array<shared_ptr<RHI_Texture>, 28>       m_render_targets;
        array<shared_ptr<RHI_Shader>, 48>        m_shaders;
        array<shared_ptr<RHI_Sampler>, 7>        m_samplers;
        array<shared_ptr<RHI_ConstantBuffer>, 3> m_constant_buffers;
        shared_ptr<RHI_StructuredBuffer>         m_structured_buffer;

        // asset resources
        array<shared_ptr<RHI_Texture>, 10> m_standard_textures;
        array<shared_ptr<Mesh>, 6>         m_standard_meshes;
    }

    void Renderer::CreateConstantBuffers()
    {
        #define constant_buffer(x) m_constant_buffers[static_cast<uint8_t>(x)]

        constant_buffer(Renderer_ConstantBuffer::Frame) = make_shared<RHI_ConstantBuffer>(string("frame"));
        constant_buffer(Renderer_ConstantBuffer::Frame)->Create<Cb_Frame>(m_frames_in_flight);

        constant_buffer(Renderer_ConstantBuffer::Light) = make_shared<RHI_ConstantBuffer>(string("light"));
        constant_buffer(Renderer_ConstantBuffer::Light)->Create<Cb_Light>(1600 * m_frames_in_flight);

        constant_buffer(Renderer_ConstantBuffer::Material) = make_shared<RHI_ConstantBuffer>(string("material"));
        constant_buffer(Renderer_ConstantBuffer::Material)->Create<Cb_Material>(30000 * m_frames_in_flight);
    }

    void Renderer::CreateStructuredBuffers()
    {
        const uint32_t offset_count = 32;
        m_structured_buffer = make_shared<RHI_StructuredBuffer>(static_cast<uint32_t>(sizeof(uint32_t)), offset_count, "spd_counter");
    }

    void Renderer::CreateDepthStencilStates()
    {
        RHI_Comparison_Function reverse_z_aware_comp_func = RHI_Comparison_Function::GreaterEqual; // reverse-z

        #define depth_stencil_state(x) m_depth_stencil_states[static_cast<uint8_t>(x)]
        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        depth_stencil_state(Renderer_DepthStencilState::Off)                            = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never, false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::Depth_read_write_stencil_read)  = make_shared<RHI_DepthStencilState>(true,  true,  reverse_z_aware_comp_func,      false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::Depth_read)                     = make_shared<RHI_DepthStencilState>(true,  false, reverse_z_aware_comp_func,      false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::Stencil_read)                   = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never, true,  false, RHI_Comparison_Function::Equal);
        depth_stencil_state(Renderer_DepthStencilState::Depth_read_write_stencil_write) = make_shared<RHI_DepthStencilState>(true,  true,  reverse_z_aware_comp_func,      false, true,  RHI_Comparison_Function::Always);
    }

    void Renderer::CreateRasterizerStates()
    {
        static const float depth_bias              = -0.004f;
        static const float depth_bias_clamp        = 0.0f;
        static const float depth_bias_slope_scaled = -2.0f;
        static const float line_width              = 2.0f;

        #define rasterizer_state(x) m_rasterizer_states[static_cast<uint8_t>(x)]
        // cull mode, filled mode, depth clip, scissor, bias, bias clamp, slope scaled bias, line width
        rasterizer_state(Renderer_RasterizerState::Solid_cull_back)     = make_shared<RHI_RasterizerState>(RHI_CullMode::Back, RHI_PolygonMode::Solid,     true,  false, 0.0f,              0.0f,             0.0f,                    line_width);
        rasterizer_state(Renderer_RasterizerState::Solid_cull_none)     = make_shared<RHI_RasterizerState>(RHI_CullMode::None, RHI_PolygonMode::Solid,     true,  false, 0.0f,              0.0f,             0.0f,                    line_width);
        rasterizer_state(Renderer_RasterizerState::Wireframe_cull_none) = make_shared<RHI_RasterizerState>(RHI_CullMode::None, RHI_PolygonMode::Wireframe, true,  false, 0.0f,              0.0f,             0.0f,                    line_width);
        rasterizer_state(Renderer_RasterizerState::Light_point_spot)    = make_shared<RHI_RasterizerState>(RHI_CullMode::Back, RHI_PolygonMode::Solid,     true,  false, depth_bias,        depth_bias_clamp, depth_bias_slope_scaled, 0.0f);
        rasterizer_state(Renderer_RasterizerState::Light_directional)   = make_shared<RHI_RasterizerState>(RHI_CullMode::Back, RHI_PolygonMode::Solid,     false, false, depth_bias * 0.1f, depth_bias_clamp, depth_bias_slope_scaled, 0.0f);
    }

    void Renderer::CreateBlendStates()
    {
        #define blend_state(x) m_blend_states[static_cast<uint8_t>(x)]
        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        blend_state(Renderer_BlendState::Disabled) = make_shared<RHI_BlendState>(false);
        blend_state(Renderer_BlendState::Alpha)    = make_shared<RHI_BlendState>(true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        blend_state(Renderer_BlendState::Additive) = make_shared<RHI_BlendState>(true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers(const bool create_only_anisotropic /*= false*/)
    {
        // Compute mip bias
        float mip_bias = 0.0f;
        if (GetResolutionOutput().x > GetResolutionRender().x)
        {
            // Progressively negative values when upsampling for increased texture fidelity
            mip_bias = log2(GetResolutionRender().x / GetResolutionOutput().x) - 1.0f;
            SP_LOG_INFO("Mip bias set to %f", mip_bias);
        }
        
        #define sampler(x) m_samplers[static_cast<uint8_t>(x)]
        if (!create_only_anisotropic)
        {
            // arguments:                                                         min,                 max,                 mip,                 address mode,                    comparison,                 anisotropy, comparison enabled
            sampler(Renderer_Sampler::Compare_depth)   = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Greater, 0.0f, true); // reverse-z
            sampler(Renderer_Sampler::Point_clamp)     = make_shared<RHI_Sampler>(RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Point_wrap)      = make_shared<RHI_Sampler>(RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,  RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Bilinear_clamp)  = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Bilinear_wrap)   = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,  RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Trilinear_clamp) = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Address_Mode::Clamp, RHI_Comparison_Function::Always);
        }

        float anisotropy = GetOption<float>(Renderer_Option::Anisotropy);
        sampler(Renderer_Sampler::Anisotropic_wrap) = make_shared<RHI_Sampler>(RHI_Filter::Linear, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_bias);

        // Make the samplers bindless and always present in the shaders
        RHI_Device::SetBindlessSamplers(m_samplers);
    }

    void Renderer::CreateRenderTextures(const bool create_render, const bool create_output, const bool create_fixed, const bool create_dynamic)
    {
        // get render resolution
        uint32_t width_render  = static_cast<uint32_t>(GetResolutionRender().x);
        uint32_t height_render = static_cast<uint32_t>(GetResolutionRender().y);

        // get output resolution
        uint32_t width_output  = static_cast<uint32_t>(GetResolutionOutput().x);
        uint32_t height_output = static_cast<uint32_t>(GetResolutionOutput().y);

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
        #define render_target(x) m_render_targets[static_cast<uint8_t>(x)]

        // Render resolution
        if (create_render)
        {
            // Frame - Mips are used to emulate roughness when blending with transparent surfaces
            render_target(Renderer_RenderTexture::frame_render)   = make_unique<RHI_Texture2D>(width_render, height_render, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Rtv | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit, "rt_frame_render");
            render_target(Renderer_RenderTexture::frame_render_2) = make_unique<RHI_Texture2D>(width_render, height_render, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Rtv | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit, "rt_frame_render_2");

            // G-Buffer
            render_target(Renderer_RenderTexture::gbuffer_albedo)            = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Rtv | RHI_Texture_Srv, "rt_gbuffer_albedo");
            render_target(Renderer_RenderTexture::gbuffer_normal)            = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Rtv | RHI_Texture_Srv, "rt_gbuffer_normal");
            render_target(Renderer_RenderTexture::gbuffer_material)          = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Rtv | RHI_Texture_Srv, "rt_gbuffer_material");
            render_target(Renderer_RenderTexture::gbuffer_material_2)        = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Rtv | RHI_Texture_Srv, "rt_gbuffer_material_2");
            render_target(Renderer_RenderTexture::gbuffer_velocity)          = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16_Float,       RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_gbuffer_velocity");
            render_target(Renderer_RenderTexture::gbuffer_velocity_previous) = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16_Float,       RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_gbuffer_velocity_previous");
            render_target(Renderer_RenderTexture::gbuffer_depth)             = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::D32_Float,          RHI_Texture_Rtv | RHI_Texture_Srv, "rt_gbuffer_depth");

            // Light
            render_target(Renderer_RenderTexture::light_diffuse)              = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_light_diffuse");
            render_target(Renderer_RenderTexture::light_diffuse_transparent)  = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_light_diffuse_transparent");
            render_target(Renderer_RenderTexture::light_specular)             = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_light_specular");
            render_target(Renderer_RenderTexture::light_specular_transparent) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_light_specular_transparent");
            render_target(Renderer_RenderTexture::light_volumetric)           = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_light_volumetric");

            // SSR - Mips are used to emulate roughness for surfaces which require it
            render_target(Renderer_RenderTexture::ssr) = make_shared<RHI_Texture2D>(width_render, height_render, mip_count, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "rt_ssr");

            // SSGI
            render_target(Renderer_RenderTexture::ssgi)          = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_ssgi");
            render_target(Renderer_RenderTexture::ssgi_filtered) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_ssgi_filtered");

            // Dof
            render_target(Renderer_RenderTexture::dof_half)   = make_unique<RHI_Texture2D>(width_render / 2, height_render / 2, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_dof_half");
            render_target(Renderer_RenderTexture::dof_half_2) = make_unique<RHI_Texture2D>(width_render / 2, height_render / 2, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_dof_half_2");

            // FSR 2 masks
            render_target(Renderer_RenderTexture::fsr2_mask_reactive)     = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8_Unorm, RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_fsr2_reactive_mask");
            render_target(Renderer_RenderTexture::fsr2_mask_transparency) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8_Unorm, RHI_Texture_Rtv | RHI_Texture_Srv, "rt_fsr2_transparency_mask");

            // Selection outline
            render_target(Renderer_RenderTexture::outline) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_Uav, "rt_outline");
        }

        // Output resolution
        if (create_output)
        {
            // Frame
            render_target(Renderer_RenderTexture::frame_output)   = make_unique<RHI_Texture2D>(width_output, height_output, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Rtv | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_frame_output");
            render_target(Renderer_RenderTexture::frame_output_2) = make_unique<RHI_Texture2D>(width_output, height_output, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Rtv | RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "rt_frame_output_2");

            // Bloom
            render_target(Renderer_RenderTexture::bloom) = make_shared<RHI_Texture2D>(width_output, height_output, mip_count, RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "rt_bloom");
        }

        // Fixed resolution
        if (create_fixed)
        {
            render_target(Renderer_RenderTexture::brdf_specular_lut) = make_unique<RHI_Texture2D>(400, 400, 1, RHI_Format::R8G8_Unorm, RHI_Texture_Uav | RHI_Texture_Srv, "rt_brdf_specular_lut");
            m_brdf_specular_lut_rendered = false;
        }

        // Dynamic resolution
        if (create_dynamic)
        {
            // Blur
            bool is_output_larger = width_output > width_render && height_output > height_render;
            uint32_t width        = is_output_larger ? width_output : width_render;
            uint32_t height       = is_output_larger ? height_output : height_render;
            render_target(Renderer_RenderTexture::blur) = make_unique<RHI_Texture2D>(width, height, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "rt_blur");
        }

        RHI_Device::QueueWaitAll();
        RHI_AMD_FidelityFX::FSR2_Resize(GetResolutionRender(), GetResolutionOutput());
    }

    void Renderer::CreateShaders()
    {
        const bool async        = true;
        const string shader_dir = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "\\";
        #define shader(x) m_shaders[static_cast<uint8_t>(x)]

        // depth prepass
        {
            shader(Renderer_Shader::depth_prepass_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_prepass.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_prepass_instanced_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_instanced_v)->AddDefine("INSTANCED");
            shader(Renderer_Shader::depth_prepass_instanced_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_prepass.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        }

        // g-buffer
        {
            shader(Renderer_Shader::gbuffer_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::gbuffer_v)->Compile(RHI_Shader_Vertex, shader_dir + "g_buffer.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::gbuffer_instanced_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::gbuffer_instanced_v)->AddDefine("INSTANCED");
            shader(Renderer_Shader::gbuffer_instanced_v)->Compile(RHI_Shader_Vertex, shader_dir + "g_buffer.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::gbuffer_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::gbuffer_p)->Compile(RHI_Shader_Pixel, shader_dir + "g_buffer.hlsl", async);
        }

        // light
        shader(Renderer_Shader::light_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::light_c)->Compile(RHI_Shader_Compute, shader_dir + "light.hlsl", async);

        // triangle & quad
        {
            shader(Renderer_Shader::fullscreen_triangle_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::fullscreen_triangle_v)->Compile(RHI_Shader_Vertex, shader_dir + "fullscreen_triangle.hlsl", async, RHI_Vertex_Type::Undefined);

            shader(Renderer_Shader::quad_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::quad_v)->Compile(RHI_Shader_Vertex, shader_dir + "quad.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::quad_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::quad_p)->Compile(RHI_Shader_Pixel, shader_dir + "quad.hlsl", async);
        }

        // light depth
        {
            shader(Renderer_Shader::depth_light_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_light_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_light.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_light_instanced_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_light_instanced_v)->AddDefine("INSTANCED");
            shader(Renderer_Shader::depth_light_instanced_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_light.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_light_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_light_p)->Compile(RHI_Shader_Pixel, shader_dir + "depth_light.hlsl", async);
        }

        // Depth alpha testing (used for the depth prepass as well as the light depth pass)
        shader(Renderer_Shader::depth_alpha_test_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::depth_alpha_test_p)->Compile(RHI_Shader_Pixel, shader_dir + "depth_alpha_test.hlsl", async);

        // Font
        shader(Renderer_Shader::font_v) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::font_v)->Compile(RHI_Shader_Vertex, shader_dir + "font.hlsl", async, RHI_Vertex_Type::PosUv);
        shader(Renderer_Shader::font_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::font_p)->Compile(RHI_Shader_Pixel, shader_dir + "font.hlsl", async);

        // Line
        shader(Renderer_Shader::line_v) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::line_v)->Compile(RHI_Shader_Vertex, shader_dir + "line.hlsl", async, RHI_Vertex_Type::PosCol);
        shader(Renderer_Shader::line_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::line_p)->Compile(RHI_Shader_Pixel, shader_dir + "line.hlsl", async);

        // Outline
        shader(Renderer_Shader::outline_v) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::outline_v)->Compile(RHI_Shader_Vertex, shader_dir + "outline.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        shader(Renderer_Shader::outline_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::outline_p)->Compile(RHI_Shader_Pixel, shader_dir + "outline.hlsl", async);
        shader(Renderer_Shader::outline_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::outline_c)->Compile(RHI_Shader_Compute, shader_dir + "outline.hlsl", async);

        // Reflection probe
        shader(Renderer_Shader::reflection_probe_v) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::reflection_probe_v)->Compile(RHI_Shader_Vertex, shader_dir + "reflection_probe.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
        shader(Renderer_Shader::reflection_probe_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::reflection_probe_p)->Compile(RHI_Shader_Pixel, shader_dir + "reflection_probe.hlsl", async);

        // Debug
        {
            shader(Renderer_Shader::debug_reflection_probe_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::debug_reflection_probe_v)->Compile(RHI_Shader_Vertex, shader_dir + "debug_reflection_probe.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
            shader(Renderer_Shader::debug_reflection_probe_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::debug_reflection_probe_p)->Compile(RHI_Shader_Pixel, shader_dir + "debug_reflection_probe.hlsl", async);
        }

        // Blur
        {
            // Gaussian
            shader(Renderer_Shader::blur_gaussian_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_c)->AddDefine("PASS_BLUR_GAUSSIAN");
            shader(Renderer_Shader::blur_gaussian_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);

            // Gaussian bilateral 
            shader(Renderer_Shader::blur_gaussian_bilaterial_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_bilaterial_c)->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
            shader(Renderer_Shader::blur_gaussian_bilaterial_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);
        }

        // Bloom
        {
            // Downsample luminance
            shader(Renderer_Shader::bloom_luminance_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_luminance_c)->AddDefine("LUMINANCE");
            shader(Renderer_Shader::bloom_luminance_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);

            // Upsample blend (with previous mip)
            shader(Renderer_Shader::bloom_upsample_blend_mip_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_upsample_blend_mip_c)->AddDefine("UPSAMPLE_BLEND_MIP");
            shader(Renderer_Shader::bloom_upsample_blend_mip_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);

            // Upsample blend (with frame)
            shader(Renderer_Shader::bloom_blend_frame_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_blend_frame_c)->AddDefine("BLEND_FRAME");
            shader(Renderer_Shader::bloom_blend_frame_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);
        }

        // Film grain
        shader(Renderer_Shader::film_grain_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::film_grain_c)->Compile(RHI_Shader_Compute, shader_dir + "film_grain.hlsl", async);

        // Chromatic aberration
        shader(Renderer_Shader::chromatic_aberration_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::chromatic_aberration_c)->Compile(RHI_Shader_Compute, shader_dir + "chromatic_aberration.hlsl", async);

        // Tone-mapping & gamma correction
        shader(Renderer_Shader::tonemapping_gamma_correction_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::tonemapping_gamma_correction_c)->Compile(RHI_Shader_Compute, shader_dir + "tone_mapping_gamma_correction.hlsl", async);

        // FXAA
        shader(Renderer_Shader::fxaa_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::fxaa_c)->Compile(RHI_Shader_Compute, shader_dir + "fxaa\\fxaa.hlsl", async);

        // Depth of Field
        {
            shader(Renderer_Shader::dof_downsample_coc_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::dof_downsample_coc_c)->AddDefine("DOWNSAMPLE_CIRCLE_OF_CONFUSION");
            shader(Renderer_Shader::dof_downsample_coc_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);

            shader(Renderer_Shader::dof_bokeh_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::dof_bokeh_c)->AddDefine("BOKEH");
            shader(Renderer_Shader::dof_bokeh_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);

            shader(Renderer_Shader::dof_tent_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::dof_tent_c)->AddDefine("TENT");
            shader(Renderer_Shader::dof_tent_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);

            shader(Renderer_Shader::dof_upscale_blend_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::dof_upscale_blend_c)->AddDefine("UPSCALE_BLEND");
            shader(Renderer_Shader::dof_upscale_blend_c)->Compile(RHI_Shader_Compute, shader_dir + "depth_of_field.hlsl", async);
        }

        // Motion Blur
        shader(Renderer_Shader::motion_blur_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::motion_blur_c)->Compile(RHI_Shader_Compute, shader_dir + "motion_blur.hlsl", async);

        // Dithering
        shader(Renderer_Shader::debanding_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::debanding_c)->Compile(RHI_Shader_Compute, shader_dir + "debanding.hlsl", async);

        // SSGI
        shader(Renderer_Shader::ssgi_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::ssgi_c)->Compile(RHI_Shader_Compute, shader_dir + "ssgi.hlsl", async);

        // Light
        {
            shader(Renderer_Shader::light_composition_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_composition_c)->Compile(RHI_Shader_Compute, shader_dir + "light_composition.hlsl", async);

            shader(Renderer_Shader::light_image_based_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_image_based_p)->Compile(RHI_Shader_Pixel, shader_dir + "light_image_based.hlsl", async);
        }

        // SSR
        shader(Renderer_Shader::ssr_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::ssr_c)->Compile(RHI_Shader_Compute, shader_dir + "ssr.hlsl", async);

        // Temporal filter
        shader(Renderer_Shader::temporal_filter_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::temporal_filter_c)->Compile(RHI_Shader_Compute, shader_dir + "temporal_filter.hlsl", async);

        // AMD FidelityFX CAS - Contrast Adaptive Sharpening
        shader(Renderer_Shader::ffx_cas_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::ffx_cas_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\cas.hlsl", async);

        // Compiled immediately, they are needed the moment the engine starts.
        {
            // AMD FidelityFX SPD - Single Pass Downsample
            shader(Renderer_Shader::ffx_spd_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::ffx_spd_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

            // BRDF - Specular Lut
            shader(Renderer_Shader::brdf_specular_lut_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::brdf_specular_lut_c)->Compile(RHI_Shader_Compute, shader_dir + "brdf_specular_lut.hlsl", false);
        }
    }

    void Renderer::CreateFonts()
    {
        // Get standard font directory
        const string dir_font = ResourceCache::GetResourceDirectory(ResourceDirectory::Fonts) + "\\";

        // Load a font (used for performance metrics)
        m_font = make_unique<Font>(dir_font + "CalibriBold.ttf", static_cast<uint32_t>(13 * Window::GetDpiScale()), Vector4(0.8f, 0.8f, 0.8f, 1.0f));
    }

    void Renderer::CreateStandardMeshes()
    {
        auto create_mesh = [](const Renderer_MeshType type)
        {
            const string project_directory = ResourceCache::GetProjectDirectory();
            shared_ptr<Mesh> mesh = make_shared<Mesh>();
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;

            if (type == Renderer_MeshType::Cube)
            {
                Geometry::CreateCube(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_cube" + EXTENSION_MODEL);
            }
            else if (type == Renderer_MeshType::Quad)
            {
                Geometry::CreateQuad(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_quad" + EXTENSION_MODEL);
            }
            else if (type == Renderer_MeshType::Sphere)
            {
                Geometry::CreateSphere(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_sphere" + EXTENSION_MODEL);
            }
            else if (type == Renderer_MeshType::Cylinder)
            {
                Geometry::CreateCylinder(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_cylinder" + EXTENSION_MODEL);
            }
            else if (type == Renderer_MeshType::Cone)
            {
                Geometry::CreateCone(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_cone" + EXTENSION_MODEL);
            }

            mesh->AddIndices(indices);
            mesh->AddVertices(vertices);
            mesh->ComputeAabb();
            mesh->ComputeNormalizedScale();
            mesh->CreateGpuBuffers();

            m_standard_meshes[static_cast<uint8_t>(type)] = mesh;
        };

        create_mesh(Renderer_MeshType::Cube);
        create_mesh(Renderer_MeshType::Quad);
        create_mesh(Renderer_MeshType::Sphere);
        create_mesh(Renderer_MeshType::Cylinder);
        create_mesh(Renderer_MeshType::Cone);

        // misc
        m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(true, "lines");
        m_world_grid          = make_unique<Grid>();
    }

    void Renderer::CreateStandardTextures()
    {
        const string dir_texture = ResourceCache::GetResourceDirectory(ResourceDirectory::Textures) + "\\";
        #define standard_texture(x) m_standard_textures[static_cast<uint8_t>(x)]

        // Noise textures
        {
            standard_texture(Renderer_StandardTexture::Noise_normal) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_noise_normal");
            standard_texture(Renderer_StandardTexture::Noise_normal)->LoadFromFile(dir_texture + "noise_normal.png");

            standard_texture(Renderer_StandardTexture::Noise_blue) = static_pointer_cast<RHI_Texture>(make_shared<RHI_Texture2DArray>(RHI_Texture_Srv, "standard_noise_blue"));
            standard_texture(Renderer_StandardTexture::Noise_blue)->LoadFromFile(dir_texture + "noise_blue_0.png");
        }

        // Color textures
        {
            standard_texture(Renderer_StandardTexture::White) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_white");
            standard_texture(Renderer_StandardTexture::White)->LoadFromFile(dir_texture + "white.png");

            standard_texture(Renderer_StandardTexture::Black) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_black");
            standard_texture(Renderer_StandardTexture::Black)->LoadFromFile(dir_texture + "black.png");

            standard_texture(Renderer_StandardTexture::Transparent) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_transparent");
            standard_texture(Renderer_StandardTexture::Transparent)->LoadFromFile(dir_texture + "transparent.png");

            standard_texture(Renderer_StandardTexture::Checkerboard) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_transparent");
            standard_texture(Renderer_StandardTexture::Checkerboard)->LoadFromFile(dir_texture + "no_texture.png");
        }

        // Gizmo icons
        {
            standard_texture(Renderer_StandardTexture::Gizmo_light_directional) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_icon_light_directional");
            standard_texture(Renderer_StandardTexture::Gizmo_light_directional)->LoadFromFile(dir_texture + "sun.png");

            standard_texture(Renderer_StandardTexture::Gizmo_light_point) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_icon_light_point");
            standard_texture(Renderer_StandardTexture::Gizmo_light_point)->LoadFromFile(dir_texture + "light_bulb.png");

            standard_texture(Renderer_StandardTexture::Gizmo_light_spot) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_icon_light_spot");
            standard_texture(Renderer_StandardTexture::Gizmo_light_spot)->LoadFromFile(dir_texture + "flashlight.png");

            standard_texture(Renderer_StandardTexture::Gizmo_audio_source) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_icon_audio_source");
            standard_texture(Renderer_StandardTexture::Gizmo_audio_source)->LoadFromFile(dir_texture + "audio.png");
        }
    }

    void Renderer::DestroyResources()
    {
        m_render_targets.fill(nullptr);
        m_shaders.fill(nullptr);
        m_samplers.fill(nullptr);
        m_standard_textures.fill(nullptr);
        m_standard_meshes.fill(nullptr);
        m_constant_buffers.fill(nullptr);
        m_structured_buffer = nullptr;
    }

    array<shared_ptr<RHI_Texture>, 28>& Renderer::GetRenderTargets()
    {
        return m_render_targets;
    }

    array<shared_ptr<RHI_Shader>, 48>& Renderer::GetShaders()
    {
        return m_shaders;
    }

    array<shared_ptr<RHI_ConstantBuffer>, 3>& Renderer::GetConstantBuffers()
    {
        return m_constant_buffers;
    }

    shared_ptr<RHI_RasterizerState> Renderer::GetRasterizerState(const Renderer_RasterizerState type)
    {
        return m_rasterizer_states[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_DepthStencilState> Renderer::GetDepthStencilState(const Renderer_DepthStencilState type)
    {
        return m_depth_stencil_states[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_BlendState> Renderer::GetBlendState(const Renderer_BlendState type)
    {
        return m_blend_states[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Texture> Renderer::GetRenderTarget(const Renderer_RenderTexture type)
    {
        return m_render_targets[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Shader> Renderer::GetShader(const Renderer_Shader type)
    {
        return m_shaders[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Sampler> Renderer::GetSampler(const Renderer_Sampler type)
    {
        return m_samplers[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_ConstantBuffer> Renderer::GetConstantBuffer(const Renderer_ConstantBuffer type)
    {
        return m_constant_buffers[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_StructuredBuffer> Renderer::GetStructuredBuffer()
    {
        return m_structured_buffer;
    }

    shared_ptr<RHI_Texture> Renderer::GetStandardTexture(const Renderer_StandardTexture type)
    {
        return m_standard_textures[static_cast<uint8_t>(type)];
    }

    shared_ptr<Mesh> Renderer::GetStandardMesh(const Renderer_MeshType type)
    {
        return m_standard_meshes[static_cast<uint8_t>(type)];
    }
}
