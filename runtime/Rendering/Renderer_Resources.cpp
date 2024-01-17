#/*
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

//= INCLUDES ============================
#include "pch.h"
#include "Window.h"
#include "Renderer.h"
#include "Geometry.h"
#include "ThreadPool.h"
#include "../World/Components/Light.h"
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
#include "../RHI/RHI_FidelityFX.h"
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
        array<shared_ptr<RHI_RasterizerState>, 5>   rasterizer_states;
        array<shared_ptr<RHI_DepthStencilState>, 5> depth_stencil_states;
        array<shared_ptr<RHI_BlendState>, 3>        blend_states;

        // renderer resources
        array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTexture::max)> render_targets;
        array<shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>         shaders;
        array<shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>       samplers;
        shared_ptr<RHI_ConstantBuffer>                                                     constant_buffer_frame;
        array<shared_ptr<RHI_StructuredBuffer>, 3>                                         structured_buffers;

        // asset resources
        array<shared_ptr<RHI_Texture>, 10>                standard_textures;
        array<shared_ptr<Mesh>, 7>                        standard_meshes;
        array<shared_ptr<Font>, resources_frame_lifetime> standard_font;
        shared_ptr<Material>                              standard_material;
    }

    void Renderer::CreateConstantBuffers()
    {
        // todo: the material index has to be removed from the frame buffer so that it only needs one update per frame

        uint32_t times_used_in_frame = 4096;
        uint32_t element_count       = times_used_in_frame * resources_frame_lifetime;

        constant_buffer_frame = make_shared<RHI_ConstantBuffer>(string("frame"));
        constant_buffer_frame->Create<Cb_Frame>(element_count);
    }

    void Renderer::CreateStructuredBuffers()
    {
        #define structured_buffer(x) structured_buffers[static_cast<uint8_t>(x)]

        {
            uint32_t times_used_in_frame = 12; // safe to tweak this, if it's not enough the engine will assert
            uint32_t element_count       = times_used_in_frame * resources_frame_lifetime;
            uint32_t stride              = static_cast<uint32_t>(sizeof(uint32_t));
            structured_buffer(Renderer_StructuredBuffer::Spd) = make_shared<RHI_StructuredBuffer>(stride, element_count, "spd_counter");

            // only needs to be set once, then after each use SPD resets it itself
            uint32_t counter_value = 0;
            structured_buffer(Renderer_StructuredBuffer::Spd)->Update(&counter_value);
        }

        uint32_t stride        = static_cast<uint32_t>(sizeof(Sb_Material)) * rhi_max_array_size;
        uint32_t element_count = 1; // non dynamic (multiple offsets)
        structured_buffer(Renderer_StructuredBuffer::Materials) = make_shared<RHI_StructuredBuffer>(stride, element_count, "materials");

        stride = static_cast<uint32_t>(sizeof(Sb_Light)) * rhi_max_array_size;
        structured_buffer(Renderer_StructuredBuffer::Lights) = make_shared<RHI_StructuredBuffer>(stride, element_count, "lights");
    }

    void Renderer::CreateDepthStencilStates()
    {
        RHI_Comparison_Function reverse_z_aware_comp_func = RHI_Comparison_Function::GreaterEqual; // reverse-z

        #define depth_stencil_state(x) depth_stencil_states[static_cast<uint8_t>(x)]
        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        depth_stencil_state(Renderer_DepthStencilState::Off)                            = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never, false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::Depth_read_write_stencil_read)  = make_shared<RHI_DepthStencilState>(true,  true,  reverse_z_aware_comp_func,      false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::Depth_read)                     = make_shared<RHI_DepthStencilState>(true,  false, reverse_z_aware_comp_func,      false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::Stencil_read)                   = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never, true,  false, RHI_Comparison_Function::Equal);
        depth_stencil_state(Renderer_DepthStencilState::Depth_read_write_stencil_write) = make_shared<RHI_DepthStencilState>(true,  true,  reverse_z_aware_comp_func,      false, true,  RHI_Comparison_Function::Always);
    }

    void Renderer::CreateRasterizerStates()
    {
        float depth_bias              = Light::GetBias();
        float depth_bias_clamp        = 0.0f;
        float depth_bias_slope_scaled = Light::GetBiasSlopeScaled();
        float line_width              = 2.0f;

        #define rasterizer_state(x) rasterizer_states[static_cast<uint8_t>(x)]
        // cull mode, filled mode, depth clip, scissor, bias, bias clamp, slope scaled bias, line width
        rasterizer_state(Renderer_RasterizerState::Solid_cull_back)     = make_shared<RHI_RasterizerState>(RHI_CullMode::Back, RHI_PolygonMode::Solid,     true,  false, 0.0f,              0.0f,             0.0f,                    line_width);
        rasterizer_state(Renderer_RasterizerState::Solid_cull_none)     = make_shared<RHI_RasterizerState>(RHI_CullMode::None, RHI_PolygonMode::Solid,     true,  false, 0.0f,              0.0f,             0.0f,                    line_width);
        rasterizer_state(Renderer_RasterizerState::Wireframe_cull_none) = make_shared<RHI_RasterizerState>(RHI_CullMode::None, RHI_PolygonMode::Wireframe, true,  false, 0.0f,              0.0f,             0.0f,                    line_width);
        rasterizer_state(Renderer_RasterizerState::Light_point_spot)    = make_shared<RHI_RasterizerState>(RHI_CullMode::Back, RHI_PolygonMode::Solid,     true,  false, depth_bias,        depth_bias_clamp, depth_bias_slope_scaled, 0.0f);
        rasterizer_state(Renderer_RasterizerState::Light_directional)   = make_shared<RHI_RasterizerState>(RHI_CullMode::Back, RHI_PolygonMode::Solid,     false, false, depth_bias * 0.1f, depth_bias_clamp, depth_bias_slope_scaled, 0.0f);
    }

    void Renderer::CreateBlendStates()
    {
        #define blend_state(x) blend_states[static_cast<uint8_t>(x)]
        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        blend_state(Renderer_BlendState::Disabled) = make_shared<RHI_BlendState>(false);
        blend_state(Renderer_BlendState::Alpha)    = make_shared<RHI_BlendState>(true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        blend_state(Renderer_BlendState::Additive) = make_shared<RHI_BlendState>(true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers(const bool create_only_anisotropic /*= false*/)
    {
        // compute mip bias
        float mip_bias = 0.0f;
        if (GetResolutionOutput().x > GetResolutionRender().x)
        {
            // progressively negative values when upsampling for increased texture fidelity
            mip_bias = log2(GetResolutionRender().x / GetResolutionOutput().x) - 1.0f;
            SP_LOG_INFO("Mip bias set to %f", mip_bias);
        }

        #define sampler(x) samplers[static_cast<uint8_t>(x)]
        if (!create_only_anisotropic)
        {
            // note: clamp to border uses black transparent RGBA(0, 0, 0, 0)
            // arguments:                                                               min,                 max,                 mip,                 address mode,                            comparison,                 anisotropy, comparison enabled
            sampler(Renderer_Sampler::Compare_depth)         = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToBorder, RHI_Comparison_Function::Greater, 0.0f, true); // reverse-z
            sampler(Renderer_Sampler::Point_clamp_edge)      = make_shared<RHI_Sampler>(RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToEdge,   RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Point_clamp_border)    = make_shared<RHI_Sampler>(RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToBorder, RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Point_wrap)            = make_shared<RHI_Sampler>(RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,          RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Bilinear_clamp_edge)   = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToEdge,   RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Bilienar_clamp_border) = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToBorder, RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Bilinear_wrap)         = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,          RHI_Comparison_Function::Always);
            sampler(Renderer_Sampler::Trilinear_clamp)       = make_shared<RHI_Sampler>(RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Address_Mode::ClampToEdge,   RHI_Comparison_Function::Always);
        }

        float anisotropy = GetOption<float>(Renderer_Option::Anisotropy);
        sampler(Renderer_Sampler::Anisotropic_wrap) = make_shared<RHI_Sampler>(RHI_Filter::Linear, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_bias);

        RHI_Device::UpdateBindlessResources(&samplers, nullptr);
    }

    void Renderer::CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic)
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

        // notes:
        // - gbuffer_normal: any format with or below 8 bits per channel, will produce banding
        #define render_target(x) render_targets[static_cast<uint8_t>(x)]

        // typical usage flags
        uint32_t flags_standard      = RHI_Texture_Uav | RHI_Texture_Srv;
        uint32_t flags_render_target = flags_standard | RHI_Texture_Rtv;

        // render resolution
        if (create_render)
        {
            // frame
            {
                uint32_t frame_render_flags    = flags_render_target | RHI_Texture_ClearBlit;
                RHI_Format frame_render_format = RHI_Format::R16G16B16A16_Float;

                render_target(Renderer_RenderTexture::frame_render)        = make_unique<RHI_Texture2D>(width_render, height_render, mip_count, frame_render_format, frame_render_flags | RHI_Texture_PerMipViews, "rt_frame_render");
                render_target(Renderer_RenderTexture::frame_render_2)      = make_unique<RHI_Texture2D>(width_render, height_render, mip_count, frame_render_format, frame_render_flags | RHI_Texture_PerMipViews, "rt_frame_render_2");
                render_target(Renderer_RenderTexture::frame_render_opaque) = make_unique<RHI_Texture2D>(width_render, height_render, 1,         frame_render_format, frame_render_flags,                           "rt_frame_render_opaque");
                //render_target(Renderer_RenderTexture::frame_render_history) = make_unique<RHI_Texture2D>(width_render, height_render, 1, frame_render_format, frame_render_flags, "rt_frame_render_history");
            }

            // g-buffer
            {
                uint32_t flags_depth_buffer = RHI_Texture_Rtv | RHI_Texture_Srv; // depth buffer can't have RHI_Texture_Uav

                render_target(Renderer_RenderTexture::gbuffer_color)        = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm,     flags_render_target,                         "rt_gbuffer_color");
                render_target(Renderer_RenderTexture::gbuffer_normal)       = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16B16A16_Float, flags_render_target,                         "rt_gbuffer_normal");
                render_target(Renderer_RenderTexture::gbuffer_material)     = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm,     flags_render_target,                         "rt_gbuffer_material");
                render_target(Renderer_RenderTexture::gbuffer_velocity)     = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16_Float,       flags_render_target | RHI_Texture_ClearBlit, "rt_gbuffer_velocity");
                render_target(Renderer_RenderTexture::gbuffer_depth)        = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::D32_Float,          flags_depth_buffer  | RHI_Texture_ClearBlit, "rt_gbuffer_depth");
                render_target(Renderer_RenderTexture::gbuffer_depth_opaque) = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::D32_Float,          flags_depth_buffer  | RHI_Texture_ClearBlit, "rt_gbuffer_depth_opaque");
            }

            // light
            {
                uint32_t light_flags    = flags_standard | RHI_Texture_ClearBlit;
                RHI_Format light_format = RHI_Format::R11G11B10_Float;

                render_target(Renderer_RenderTexture::light_diffuse)              = make_unique<RHI_Texture2D>(width_render, height_render, 1, light_format, light_flags, "rt_light_diffuse");
                render_target(Renderer_RenderTexture::light_diffuse_transparent)  = make_unique<RHI_Texture2D>(width_render, height_render, 1, light_format, light_flags, "rt_light_diffuse_transparent");
                render_target(Renderer_RenderTexture::light_specular)             = make_unique<RHI_Texture2D>(width_render, height_render, 1, light_format, light_flags, "rt_light_specular");
                render_target(Renderer_RenderTexture::light_specular_transparent) = make_unique<RHI_Texture2D>(width_render, height_render, 1, light_format, light_flags, "rt_light_specular_transparent");
                render_target(Renderer_RenderTexture::light_volumetric)           = make_unique<RHI_Texture2D>(width_render, height_render, 1, light_format, light_flags, "rt_light_volumetric");
            }

            // ssr
            {
                uint32_t mip_count_ssr = 5; // we use mips to emulate high roughness, low roughness is emulated via a gaussian blur, therefore we don't need a full mip chain, just enough to get believable results
                render_target(Renderer_RenderTexture::ssr)           = make_shared<RHI_Texture2D>(width_render, height_render, mip_count_ssr, RHI_Format::R16G16B16A16_Float, flags_standard | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit, "rt_ssr");
                render_target(Renderer_RenderTexture::ssr_roughness) = make_shared<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16_Float, flags_standard, "rt_ssr_roughness");
            }

            // sss
            render_target(Renderer_RenderTexture::sss) = make_shared<RHI_Texture2DArray>(width_render, height_render, RHI_Format::R16_Float, 4, flags_standard | RHI_Texture_ClearBlit, "rt_sss");

            // ssgi
            render_target(Renderer_RenderTexture::ssgi) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16B16A16_Float, flags_standard, "rt_ssgi");

            // dof
            render_target(Renderer_RenderTexture::dof_half)   = make_unique<RHI_Texture2D>(width_render / 2, height_render / 2, 1, RHI_Format::R16G16B16A16_Float, flags_standard, "rt_dof_half");
            render_target(Renderer_RenderTexture::dof_half_2) = make_unique<RHI_Texture2D>(width_render / 2, height_render / 2, 1, RHI_Format::R16G16B16A16_Float, flags_standard, "rt_dof_half_2");

            // selection outline
            render_target(Renderer_RenderTexture::outline) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R8G8B8A8_Unorm, flags_render_target, "rt_outline");
        }

        // output resolution
        if (create_output)
        {
            // frame
            uint32_t frame_flags = flags_render_target | RHI_Texture_ClearBlit;
            render_target(Renderer_RenderTexture::frame_output)   = make_unique<RHI_Texture2D>(width_output, height_output, 1, RHI_Format::R16G16B16A16_Float, frame_flags, "rt_frame_output");
            render_target(Renderer_RenderTexture::frame_output_2) = make_unique<RHI_Texture2D>(width_output, height_output, 1, RHI_Format::R16G16B16A16_Float, frame_flags, "rt_frame_output_2");

            // bloom
            render_target(Renderer_RenderTexture::bloom) = make_shared<RHI_Texture2D>(width_output, height_output, mip_count, RHI_Format::R11G11B10_Float, flags_standard | RHI_Texture_PerMipViews, "rt_bloom");
        }

        // fixed resolution - these are only done once
        if (!render_target(Renderer_RenderTexture::brdf_specular_lut))
        {
            // brdf
            render_target(Renderer_RenderTexture::brdf_specular_lut) = make_unique<RHI_Texture2D>(512, 512, 1, RHI_Format::R8G8_Unorm, flags_standard, "rt_brdf_specular_lut");

            // atmospheric scattering
            render_target(Renderer_RenderTexture::skysphere) = make_unique<RHI_Texture2D>(4096, 2048, mip_count, RHI_Format::R11G11B10_Float, flags_standard | RHI_Texture_PerMipViews, "rt_skysphere");
        }
        
        // scratch textures
        {
            render_target(Renderer_RenderTexture::scratch_blur)        = make_unique<RHI_Texture2D>(4096, 4096, 1, RHI_Format::R16G16B16A16_Float, flags_standard, "rt_scratch_blur");
            render_target(Renderer_RenderTexture::scratch_antiflicker) = make_unique<RHI_Texture2D>(width_render, height_render, 1, RHI_Format::R16G16B16A16_Float, flags_standard | RHI_Texture_ClearBlit, "rt_scratch_antiflicker");
        }

        RHI_Device::QueueWaitAll();
        RHI_FidelityFX::FSR2_Resize(GetResolutionRender(), GetResolutionOutput());
    }

    void Renderer::CreateShaders()
    {
        const bool async        = true;
        const string shader_dir = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "\\";
        #define shader(x) shaders[static_cast<uint8_t>(x)]

        // debug
        {
            // reflection probe
            shader(Renderer_Shader::debug_reflection_probe_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::debug_reflection_probe_v)->Compile(RHI_Shader_Vertex, shader_dir + "debug_reflection_probe.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
            shader(Renderer_Shader::debug_reflection_probe_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::debug_reflection_probe_p)->Compile(RHI_Shader_Pixel, shader_dir + "debug_reflection_probe.hlsl", async);

            // line
            shader(Renderer_Shader::line_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::line_v)->Compile(RHI_Shader_Vertex, shader_dir + "line.hlsl", async, RHI_Vertex_Type::PosCol);
            shader(Renderer_Shader::line_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::line_p)->Compile(RHI_Shader_Pixel, shader_dir + "line.hlsl", async);

            // grid
            shader(Renderer_Shader::grid_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::grid_p)->Compile(RHI_Shader_Pixel, shader_dir + "grid.hlsl", async);

            // outline
            shader(Renderer_Shader::outline_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::outline_v)->Compile(RHI_Shader_Vertex, shader_dir + "outline.hlsl", async, RHI_Vertex_Type::PosUvNorTan);
            shader(Renderer_Shader::outline_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::outline_p)->Compile(RHI_Shader_Pixel, shader_dir + "outline.hlsl", async);
            shader(Renderer_Shader::outline_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::outline_c)->Compile(RHI_Shader_Compute, shader_dir + "outline.hlsl", async);
        }

        // depth pre-pass
        {
            shader(Renderer_Shader::depth_prepass_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_prepass.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_prepass_instanced_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_instanced_v)->AddDefine("INSTANCED");
            shader(Renderer_Shader::depth_prepass_instanced_v)->Compile(RHI_Shader_Vertex, shader_dir + "depth_prepass.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_prepass_alpha_test_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_alpha_test_p)->Compile(RHI_Shader_Pixel, shader_dir + "depth_prepass.hlsl", async);
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

            shader(Renderer_Shader::depth_light_alpha_test_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_light_alpha_test_p)->Compile(RHI_Shader_Pixel, shader_dir + "depth_light_alpha_test.hlsl", async);
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
        {
            // brdf specular lut - compile synchronously as it's needed immediately
            shader(Renderer_Shader::light_integration_brdf_specular_lut_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_integration_brdf_specular_lut_c)->AddDefine("BRDF_SPECULAR_LUT");
            shader(Renderer_Shader::light_integration_brdf_specular_lut_c)->Compile(RHI_Shader_Compute, shader_dir + "light_integration.hlsl", false);

            // environment prefilter - compile synchronously as it's needed immediately
            shader(Renderer_Shader::light_integration_environment_filter_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_integration_environment_filter_c)->AddDefine("ENVIRONMENT_FILTER");
            shader(Renderer_Shader::light_integration_environment_filter_c)->Compile(RHI_Shader_Compute, shader_dir + "light_integration.hlsl", async);

            // light
            shader(Renderer_Shader::light_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_c)->Compile(RHI_Shader_Compute, shader_dir + "light.hlsl", async);

            // composition
            shader(Renderer_Shader::light_composition_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_composition_c)->Compile(RHI_Shader_Compute, shader_dir + "light_composition.hlsl", async);

            // image based
            shader(Renderer_Shader::light_image_based_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_image_based_c)->Compile(RHI_Shader_Compute, shader_dir + "light_image_based.hlsl", async);
        }

        // triangle & quad
        {
            shader(Renderer_Shader::fullscreen_triangle_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::fullscreen_triangle_v)->Compile(RHI_Shader_Vertex, shader_dir + "fullscreen_triangle.hlsl", async, RHI_Vertex_Type::Max);

            shader(Renderer_Shader::quad_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::quad_v)->Compile(RHI_Shader_Vertex, shader_dir + "quad.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::quad_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::quad_p)->Compile(RHI_Shader_Pixel, shader_dir + "quad.hlsl", async);
        }

        // blur
        {
            // gaussian
            shader(Renderer_Shader::blur_gaussian_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);

            // gaussian bilateral - or depth aware
            shader(Renderer_Shader::blur_gaussian_bilaterial_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_bilaterial_c)->AddDefine("PASS_BLUR_GAUSSIAN_BILATERAL");
            shader(Renderer_Shader::blur_gaussian_bilaterial_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);

            // gaussian bilateral - where the alpha is used as the blur radius
            shader(Renderer_Shader::blur_gaussian_bilaterial_radius_from_texture_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_bilaterial_radius_from_texture_c)->AddDefine("PASS_BLUR_GAUSSIAN_BILATERAL");
            shader(Renderer_Shader::blur_gaussian_bilaterial_radius_from_texture_c)->AddDefine("RADIUS_FROM_TEXTURE");
            shader(Renderer_Shader::blur_gaussian_bilaterial_radius_from_texture_c)->Compile(RHI_Shader_Compute, shader_dir + "blur.hlsl", async);
        }

        // bloom
        {
            // downsample luminance
            shader(Renderer_Shader::bloom_luminance_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_luminance_c)->AddDefine("LUMINANCE");
            shader(Renderer_Shader::bloom_luminance_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);

            // upsample blend (with previous mip)
            shader(Renderer_Shader::bloom_upsample_blend_mip_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_upsample_blend_mip_c)->AddDefine("UPSAMPLE_BLEND_MIP");
            shader(Renderer_Shader::bloom_upsample_blend_mip_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);

            // upsample blend (with frame)
            shader(Renderer_Shader::bloom_blend_frame_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_blend_frame_c)->AddDefine("BLEND_FRAME");
            shader(Renderer_Shader::bloom_blend_frame_c)->Compile(RHI_Shader_Compute, shader_dir + "bloom.hlsl", async);
        }

        // depth of field
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

        // amd fidelityfx
        {
            // cas - contrast adaptive sharpening
            shader(Renderer_Shader::ffx_cas_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::ffx_cas_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\cas.hlsl", async);

            // spd - single pass downsample - compile synchronously as they are needed everywhere
            {
                shader(Renderer_Shader::ffx_spd_average_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_average_c)->AddDefine("AVERAGE");
                shader(Renderer_Shader::ffx_spd_average_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

                shader(Renderer_Shader::ffx_spd_highest_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_highest_c)->AddDefine("HIGHEST");
                shader(Renderer_Shader::ffx_spd_highest_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

                shader(Renderer_Shader::ffx_spd_antiflicker_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_antiflicker_c)->AddDefine("ANTIFLICKER");
                shader(Renderer_Shader::ffx_spd_antiflicker_c)->Compile(RHI_Shader_Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);
            }
        }

        // anti-aliasing
        {
            // temporal
            //shader(Renderer_Shader::taa_c) = make_shared<RHI_Shader>();
            //shader(Renderer_Shader::taa_c)->Compile(RHI_Shader_Compute, shader_dir + "temporal_antialiasing.hlsl", async);

            // fxaa
            shader(Renderer_Shader::fxaa_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::fxaa_c)->Compile(RHI_Shader_Compute, shader_dir + "fxaa\\fxaa.hlsl", async);
        }

        // skysphere
        shader(Renderer_Shader::skysphere_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::skysphere_c)->Compile(RHI_Shader_Compute, shader_dir + "skysphere.hlsl", async);

        // font
        shader(Renderer_Shader::font_v) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::font_v)->Compile(RHI_Shader_Vertex, shader_dir + "font.hlsl", async, RHI_Vertex_Type::PosUv);
        shader(Renderer_Shader::font_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::font_p)->Compile(RHI_Shader_Pixel, shader_dir + "font.hlsl", async);

        // film grain
        shader(Renderer_Shader::film_grain_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::film_grain_c)->Compile(RHI_Shader_Compute, shader_dir + "film_grain.hlsl", async);

        // chromatic aberration
        shader(Renderer_Shader::chromatic_aberration_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::chromatic_aberration_c)->Compile(RHI_Shader_Compute, shader_dir + "chromatic_aberration.hlsl", async);

        // tone-mapping & gamma correction
        shader(Renderer_Shader::tonemapping_gamma_correction_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::tonemapping_gamma_correction_c)->Compile(RHI_Shader_Compute, shader_dir + "tone_mapping_gamma_correction.hlsl", async);

        // motion blur
        shader(Renderer_Shader::motion_blur_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::motion_blur_c)->Compile(RHI_Shader_Compute, shader_dir + "motion_blur.hlsl", async);

        // dithering
        shader(Renderer_Shader::debanding_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::debanding_c)->Compile(RHI_Shader_Compute, shader_dir + "debanding.hlsl", async);

        // ssgi
        shader(Renderer_Shader::ssgi_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::ssgi_c)->Compile(RHI_Shader_Compute, shader_dir + "ssgi.hlsl", async);

        // screen space reflections
        shader(Renderer_Shader::ssr_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::ssr_c)->Compile(RHI_Shader_Compute, shader_dir + "ssr.hlsl", async);

        // screen space shadows
        shader(Renderer_Shader::sss_c_bend) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::sss_c_bend)->Compile(RHI_Shader_Compute, shader_dir + "screen_space_shadows\\bend_sss.hlsl", async);

        // antiflicker
        shader(Renderer_Shader::antiflicker_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::antiflicker_c)->Compile(RHI_Shader_Compute, shader_dir + "antiflicker.hlsl", async);
    }

    void Renderer::CreateFonts()
    {
        // get standard font directory
        const string dir_font = ResourceCache::GetResourceDirectory(ResourceDirectory::Fonts) + "\\";

        // load a font
        for (uint32_t i = 0; i < 5; i++) // as many as m_resources_frame_lifetime
        {
            // ResourceCache will ensure that the font resource is only loaded once
            standard_font[i] = make_shared<Font>(dir_font + "OpenSans/OpenSans-Medium.ttf", static_cast<uint32_t>(11 * Window::GetDpiScale()), Vector4(0.9f, 0.9f, 0.9f, 1.0f));
        }
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
            else if (type == Renderer_MeshType::Grid)
            {
                uint32_t resolution = 512; // a high number here can kill performance in the forest scene
                Geometry::CreateGrid(&vertices, &indices, resolution);
                mesh->SetResourceFilePath(project_directory + "standard_grid" + EXTENSION_MODEL);
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

            standard_meshes[static_cast<uint8_t>(type)] = mesh;
        };

        create_mesh(Renderer_MeshType::Cube);
        create_mesh(Renderer_MeshType::Quad);
        create_mesh(Renderer_MeshType::Sphere);
        create_mesh(Renderer_MeshType::Cylinder);
        create_mesh(Renderer_MeshType::Cone);
        create_mesh(Renderer_MeshType::Grid);

        // this buffers holds all debug primitives that can be drawn
        m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(true, "lines");
    }

    void Renderer::CreateStandardTextures()
    {
        const string dir_texture = ResourceCache::GetResourceDirectory(ResourceDirectory::Textures) + "\\";
        #define standard_texture(x) standard_textures[static_cast<uint8_t>(x)]

        // Noise textures
        {
            standard_texture(Renderer_StandardTexture::Noise_normal) = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "standard_noise_normal");
            standard_texture(Renderer_StandardTexture::Noise_normal)->LoadFromFile(dir_texture + "noise_normal.png");

            standard_texture(Renderer_StandardTexture::Noise_blue) = make_shared<RHI_Texture2DArray>(RHI_Texture_Srv, "standard_noise_blue");
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

    void Renderer::CreateStandardMaterials()
    {
        const string data_dir = ResourceCache::GetDataDirectory() + "\\";
        FileSystem::CreateDirectory(data_dir);

        standard_material = make_shared<Material>();
        standard_material->SetResourceFilePath(ResourceCache::GetProjectDirectory() + "standard" + EXTENSION_MATERIAL); // set resource file path so it can be used by the resource cache
        standard_material->SetProperty(MaterialProperty::CanBeEdited,    0.0f);
        standard_material->SetProperty(MaterialProperty::TextureTilingX, 10.0f);
        standard_material->SetProperty(MaterialProperty::TextureTilingY, 10.0f);
        standard_material->SetProperty(MaterialProperty::ColorR,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorG,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorB,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorA,         1.0f);
        standard_material->SetTexture(MaterialTexture::Color,            Renderer::GetStandardTexture(Renderer_StandardTexture::Checkerboard));
    }

    void Renderer::DestroyResources()
    {
        render_targets.fill(nullptr);
        shaders.fill(nullptr);
        samplers.fill(nullptr);
        standard_textures.fill(nullptr);
        standard_meshes.fill(nullptr);
        structured_buffers.fill(nullptr);
        standard_font.fill(nullptr);
        standard_material = nullptr;
        constant_buffer_frame = nullptr;
    }

    array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTexture::max)>& Renderer::GetRenderTargets()
    {
        return render_targets;
    }

    array<shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& Renderer::GetShaders()
    {
        return shaders;
    }

    array<shared_ptr<RHI_StructuredBuffer>, 3>& Renderer::GetStructuredBuffers()
    {
        return structured_buffers;
    }

    shared_ptr<RHI_RasterizerState> Renderer::GetRasterizerState(const Renderer_RasterizerState type)
    {
        return rasterizer_states[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_DepthStencilState> Renderer::GetDepthStencilState(const Renderer_DepthStencilState type)
    {
        return depth_stencil_states[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_BlendState> Renderer::GetBlendState(const Renderer_BlendState type)
    {
        return blend_states[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Texture> Renderer::GetRenderTarget(const Renderer_RenderTexture type)
    {
        return render_targets[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Shader> Renderer::GetShader(const Renderer_Shader type)
    {
        return shaders[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Sampler> Renderer::GetSampler(const Renderer_Sampler type)
    {
        return samplers[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_ConstantBuffer>& Renderer::GetConstantBufferFrame()
    {
        return constant_buffer_frame;
    }

    shared_ptr<RHI_StructuredBuffer> Renderer::GetStructuredBuffer(const Renderer_StructuredBuffer type)
    {
        return structured_buffers[static_cast<uint8_t>(type)];
    }

    shared_ptr<RHI_Texture> Renderer::GetStandardTexture(const Renderer_StandardTexture type)
    {
        return standard_textures[static_cast<uint8_t>(type)];
    }

    shared_ptr<Mesh> Renderer::GetStandardMesh(const Renderer_MeshType type)
    {
        return standard_meshes[static_cast<uint8_t>(type)];
    }

    shared_ptr<Font>& Renderer::GetFont()
    {
        return standard_font[m_resource_index];
    }

    shared_ptr<Material> Renderer::GetStandardMaterial()
    {
        return standard_material;
    }
}
