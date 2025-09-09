/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES =============================
#include "pch.h"
#include "Window.h"
#include "Renderer.h"
#include "Material.h"
#include "../Geometry/GeometryGeneration.h"
#include "../World/Components/Light.h"
#include "../Resource/ResourceCache.h"
#include "../Display/Display.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Device.h"
#ifdef _MSC_VER
#include "../RHI/RHI_VendorTechnology.h"
#endif
//========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // graphics states
        array<shared_ptr<RHI_RasterizerState>, static_cast<uint32_t>(Renderer_RasterizerState::Max)>     rasterizer_states;
        array<shared_ptr<RHI_DepthStencilState>, static_cast<uint32_t>(Renderer_DepthStencilState::Max)> depth_stencil_states;
        array<shared_ptr<RHI_BlendState>, 3>                                                             blend_states;

        // renderer resources
        array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)> render_targets;
        array<shared_ptr<RHI_Shader>,  static_cast<uint32_t>(Renderer_Shader::max)>       shaders;
        array<shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>      samplers;
        array<shared_ptr<RHI_Buffer>,  static_cast<uint32_t>(Renderer_Buffer::Max)>       buffers;

        // asset resources
        array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_StandardTexture::Max)> standard_textures;
        array<shared_ptr<Mesh>, static_cast<uint32_t>(MeshType::Max)>                        standard_meshes;
        shared_ptr<Font>                                                                     standard_font;
        shared_ptr<Material>                                                                 standard_material;
    }

    void Renderer::CreateBuffers()
    {
        uint32_t element_count = renderer_resource_frame_lifetime;
        #define buffer(x) buffers[static_cast<uint8_t>(x)]

        // initialization values
        uint32_t spd_counter_value                          = 0;
        array<Matrix, renderer_max_instance_count> identity = { Matrix::Identity };

        buffer(Renderer_Buffer::ConstantFrame)      = make_shared<RHI_Buffer>(RHI_Buffer_Type::Constant, sizeof(Cb_Frame),                           element_count,                          nullptr,            true, "frame");
        buffer(Renderer_Buffer::SpdCounter)         = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(uint32_t)),    1,                                      &spd_counter_value, true, "spd_counter");
        buffer(Renderer_Buffer::MaterialParameters) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Material)), rhi_max_array_size,                     nullptr,            true, "materials");
        buffer(Renderer_Buffer::LightParameters)    = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Light)),    rhi_max_array_size,                     nullptr,            true, "lights");
        buffer(Renderer_Buffer::DummyInstance)      = make_shared<RHI_Buffer>(RHI_Buffer_Type::Instance, sizeof(Matrix),                             static_cast<uint32_t>(identity.size()), &identity,          true, "dummy_instance_buffer");
        buffer(Renderer_Buffer::Visibility)         = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(uint32_t)),    rhi_max_array_size,                     nullptr,            true, "visibility");
        buffer(Renderer_Buffer::VisibilityPrevious) = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(uint32_t)),    rhi_max_array_size,                     nullptr,            true, "visibility_previous");
        buffer(Renderer_Buffer::AABBs)              = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,  static_cast<uint32_t>(sizeof(Sb_Aabb)),     rhi_max_array_size,                     nullptr,            true, "aabbs");
    }

    void Renderer::CreateDepthStencilStates()
    {
        #define depth_stencil_state(x) depth_stencil_states[static_cast<uint8_t>(x)]

        // arguments: depth_test, depth_write, depth_function, stencil_test, stencil_write, stencil_function
        depth_stencil_state(Renderer_DepthStencilState::Off)              = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Never);
        depth_stencil_state(Renderer_DepthStencilState::ReadEqual)        = make_shared<RHI_DepthStencilState>(true,  false, RHI_Comparison_Function::Equal);
        depth_stencil_state(Renderer_DepthStencilState::ReadGreaterEqual) = make_shared<RHI_DepthStencilState>(true,  false, RHI_Comparison_Function::GreaterEqual);
        depth_stencil_state(Renderer_DepthStencilState::ReadWrite)        = make_shared<RHI_DepthStencilState>(true,  true,  RHI_Comparison_Function::GreaterEqual);
    }

    void Renderer::CreateRasterizerStates()
    {
        float bias              = Light::GetBias();
        float bias_clamp        = 0.0f;
        float bias_slope_scaled = Light::GetBiasSlopeScaled();
        float line_width        = 3.0f;

        #define rasterizer_state(x) rasterizer_states[static_cast<uint8_t>(x)]
        //                                                                                               fill mode,    depth clip enabled,  bias,   bias clamp,       slope scaled bias, line width
        rasterizer_state(Renderer_RasterizerState::Solid)             = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     true,  0.0f,         0.0f,       0.0f,              line_width);
        rasterizer_state(Renderer_RasterizerState::Wireframe)         = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Wireframe, true,  0.0f,         0.0f,       0.0f,              line_width);
        rasterizer_state(Renderer_RasterizerState::Light_point_spot)  = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     true,  bias,         bias_clamp, bias_slope_scaled, line_width);
        rasterizer_state(Renderer_RasterizerState::Light_directional) = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid,     false, bias * 0.1f,  bias_clamp, bias_slope_scaled, line_width);
    }

    void Renderer::CreateBlendStates()
    {
        #define blend_state(x) blend_states[static_cast<uint8_t>(x)]

        // blend_enabled, source_blend, dest_blend, blend_op, source_blend_alpha, dest_blend_alpha, blend_op_alpha, blend_factor
        blend_state(Renderer_BlendState::Off)      = make_shared<RHI_BlendState>(false);
        blend_state(Renderer_BlendState::Alpha)    = make_shared<RHI_BlendState>(true, RHI_Blend::Src_Alpha, RHI_Blend::Inv_Src_Alpha, RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 0.0f);
        blend_state(Renderer_BlendState::Additive) = make_shared<RHI_BlendState>(true, RHI_Blend::One,       RHI_Blend::One,           RHI_Blend_Operation::Add, RHI_Blend::One, RHI_Blend::One, RHI_Blend_Operation::Add, 1.0f);
    }

    void Renderer::CreateSamplers()
    {
        #define sampler(type, filter_min, filter_mag, filter_mip, address_mode, comparison_func, anisotropy, comparison_enabled, mip_bias) \
        samplers[static_cast<uint8_t>(type)] = make_shared<RHI_Sampler>(filter_min, filter_mag, filter_mip, address_mode, comparison_func, anisotropy, comparison_enabled, mip_bias)

        // non anisotropic
        {
            static bool samplers_created = false;
            if (!samplers_created)
            {
                sampler(Renderer_Sampler::Compare_depth,         RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Greater, 0.0f, true,  0.0f); // reverse-z
                sampler(Renderer_Sampler::Point_clamp_edge,      RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Point_clamp_border,    RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Point_wrap,            RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,        RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Bilinear_clamp_edge,   RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Bilienar_clamp_border, RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::ClampToZero, RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Bilinear_wrap,         RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Nearest, RHI_Sampler_Address_Mode::Wrap,        RHI_Comparison_Function::Never,   0.0f, false, 0.0f);
                sampler(Renderer_Sampler::Trilinear_clamp,       RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Filter::Linear,  RHI_Sampler_Address_Mode::Clamp,       RHI_Comparison_Function::Never,   0.0f, false, 0.0f);

                samplers_created = true;
            }
        }

        // anisotropic
        {
            // compute mip bias for enhanced texture detail in upsampling, applicable when output resolution is higher than render resolution
            // this adjustment, beneficial even without FSR, ensures textures remain detailed at higher output resolutions by applying a negative bias
            float mip_bias_new = 0.0f;
            if (GetResolutionOutput().x > GetResolutionRender().x)
            {
                mip_bias_new = log2(GetResolutionRender().x / GetResolutionOutput().x) - 1.0f;
            }

            static float mip_bias = numeric_limits<float>::max();
            if (mip_bias_new != mip_bias)
            {
                mip_bias         = mip_bias_new;
                float anisotropy = GetOption<float>(Renderer_Option::Anisotropy);
                sampler(Renderer_Sampler::Anisotropic_wrap, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Filter::Linear, RHI_Sampler_Address_Mode::Wrap, RHI_Comparison_Function::Always, anisotropy, false, mip_bias);
            }
        }

        m_bindless_samplers_dirty = true;
    }

    void Renderer::CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic)
    {
        // get render and output resolutions
        uint32_t width_render  = static_cast<uint32_t>(GetResolutionRender().x);
        uint32_t height_render = static_cast<uint32_t>(GetResolutionRender().y);
        uint32_t width_output  = static_cast<uint32_t>(GetResolutionOutput().x);
        uint32_t height_output = static_cast<uint32_t>(GetResolutionOutput().y);

        auto compute_mip_count = [&width_render, &height_render](const uint32_t smallest_dimension)
        {
            uint32_t max_dimension = max(width_render, height_render);
            uint32_t mip_count     = 1;
        
            while (max_dimension > smallest_dimension)
            {
                max_dimension /= 2;
                mip_count++;
            }
        
            return mip_count;
        };

        // amd: avoid combining RHI_Texture_Uav with color or depth render targets (RHI_Texture_Rtv), doing so forces less optimal layouts and leaves performance on the table
        // it's acceptable for rarely used targets (e.g., renderer_rendertarget::shading_rate), but not for frequently accessed targets like g-buffer or lighting, which are read/written many times per frame

        #define render_target(x) render_targets[static_cast<uint8_t>(x)]
        // resolution - render
        if (create_render)
        {
            // frame
            {
                render_target(Renderer_RenderTarget::frame_render)        = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_render");
                render_target(Renderer_RenderTarget::frame_render_opaque) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_render_opaque");
            }

            // g-buffer
            {
                uint32_t flags = RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit;

                render_target(Renderer_RenderTarget::gbuffer_color)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_color");
                render_target(Renderer_RenderTarget::gbuffer_normal)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "gbuffer_normal");
                render_target(Renderer_RenderTarget::gbuffer_material) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R8G8B8A8_Unorm,     flags, "gbuffer_material");
                render_target(Renderer_RenderTarget::gbuffer_velocity) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R16G16_Float,       flags, "gbuffer_velocity");
                render_target(Renderer_RenderTarget::gbuffer_depth)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::D32_Float,          flags, "gbuffer_depth");
            }

            // light
            {
                uint32_t flags = RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit;

                render_target(Renderer_RenderTarget::light_diffuse)    = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_diffuse");
                render_target(Renderer_RenderTarget::light_specular)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_specular");
                render_target(Renderer_RenderTarget::light_shadow)     = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R8_Unorm,        flags, "light_shadow");
                render_target(Renderer_RenderTarget::light_volumetric) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::R11G11B10_Float, flags, "light_volumetric");
            }

            // occlusion
            {
                // note #1: amd is very specific with depth formats, so if something is a depth render target, it can only have one mip and flags like RHI_Texture_Uav
                // so we create second texture with the flags we want and then blit to that, not mention that we can't even use vkBlitImage so we do a manual one (AMD is killing is us here)
                // note #2: too many mips can degrade depth to nothing (0), which is infinite distance (in reverse-z), which breaks things
                render_target(Renderer_RenderTarget::gbuffer_depth_occluders)     = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv, "depth_occluders");
                render_target(Renderer_RenderTarget::gbuffer_depth_occluders_hiz) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render, height_render, 1, 5, RHI_Format::R32_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit | RHI_Texture_PerMipViews, "depth_occluders_hiz");
            }

            // misc
            render_target(Renderer_RenderTarget::sss)  = make_shared<RHI_Texture>(RHI_Texture_Type::Type2DArray, width_render, height_render, 4, 1, RHI_Format::R16_Float,          RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "sss");
            render_target(Renderer_RenderTarget::ssr)  = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "ssr");
            render_target(Renderer_RenderTarget::ssao) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,      width_render, height_render, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_ClearBlit, "ssao");
            if (RHI_Device::PropertyIsShadingRateSupported())
            { 
                render_target(Renderer_RenderTarget::shading_rate) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_render / 4, height_render / 4, 1, 1, RHI_Format::R8_Uint, RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_Rtv | RHI_Texture_Vrs, "shading_rate");
            }
            render_target(Renderer_RenderTarget::shadow_atlas) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 8192, 8192, 1, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit, "shadow_atlas");
        }

        // resolution - output
        if (create_output)
        {
            // frame
            render_target(Renderer_RenderTarget::frame_output)   = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, compute_mip_count(16), RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit | RHI_Texture_PerMipViews, "frame_output");
            render_target(Renderer_RenderTarget::frame_output_2) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,                     RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "frame_output_2");

            // misc
            render_target(Renderer_RenderTarget::bloom)                       = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, compute_mip_count(64), RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews, "bloom");
            render_target(Renderer_RenderTarget::outline)                     = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,                     RHI_Format::R8G8B8A8_Unorm,     RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv,         "outline");
            render_target(Renderer_RenderTarget::gbuffer_depth_opaque_output) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width_output, height_output, 1, 1,                     RHI_Format::D32_Float,          RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit,   "depth_opaque_output");
        }

        // resolution - fixed (created once)
        if (!render_target(Renderer_RenderTarget::lut_brdf_specular))
        {
            // lookup tables
            render_target(Renderer_RenderTarget::lut_brdf_specular)      = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 512, 512, 1,  1, RHI_Format::R8G8_Unorm,         RHI_Texture_Uav | RHI_Texture_Srv, "lut_brdf_specular");
            render_target(Renderer_RenderTarget::lut_atmosphere_scatter) = make_shared<RHI_Texture>(RHI_Texture_Type::Type3D, 256, 256, 32, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "lut_atmosphere_scatter");

            // misc
            render_target(Renderer_RenderTarget::blur)      = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 4096, 4096, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv, "blur_scratch");
            uint32_t lowest_dimension = 32; // lowest mip is 32x32, preserving directional detail for diffuse IBL (1x1 loses directionality)
            render_target(Renderer_RenderTarget::skysphere) = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, 4096, 2048, 1, compute_mip_count(lowest_dimension), RHI_Format::R11G11B10_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit, "skysphere");
        }

        RHI_VendorTechnology::Resize(GetResolutionRender(), GetResolutionOutput());
    }

    void Renderer::CreateShaders()
    {
        const bool async        = true;
        const string shader_dir = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "\\";
        #define shader(x) shaders[static_cast<uint8_t>(x)]

        // debug
        {
            // line
            shader(Renderer_Shader::line_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::line_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "line.hlsl", async, RHI_Vertex_Type::PosCol);
            shader(Renderer_Shader::line_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::line_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "line.hlsl", async);

            // grid
            {
                shader(Renderer_Shader::grid_v) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::grid_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "grid.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

                shader(Renderer_Shader::grid_p) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::grid_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "grid.hlsl", async);
            }

            // outline
            {
                shader(Renderer_Shader::outline_v) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::outline_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "outline.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

                shader(Renderer_Shader::outline_p) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::outline_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "outline.hlsl", async);

                shader(Renderer_Shader::outline_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::outline_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "outline.hlsl", async);
            }
        }

        // depth
        {
            shader(Renderer_Shader::depth_prepass_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "depth_prepass.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_prepass_alpha_test_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_prepass_alpha_test_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "depth_prepass.hlsl", async);
        }

        // light depth
        {
            shader(Renderer_Shader::depth_light_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_light_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "depth_light.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::depth_light_alpha_color_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::depth_light_alpha_color_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "depth_light.hlsl", async);
        }

        // g-buffer
        {
            shader(Renderer_Shader::gbuffer_v) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::gbuffer_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "g_buffer.hlsl", async, RHI_Vertex_Type::PosUvNorTan);

            shader(Renderer_Shader::gbuffer_p) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::gbuffer_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "g_buffer.hlsl", async);
        }

        // tessellation
        {
            shader(Renderer_Shader::tessellation_h) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::tessellation_h)->Compile(RHI_Shader_Type::Hull, shader_dir + "common_vertex_processing.hlsl", async);

            shader(Renderer_Shader::tessellation_d) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::tessellation_d)->Compile(RHI_Shader_Type::Domain, shader_dir + "common_vertex_processing.hlsl", async);
        }

        // light
        {
            // brdf specular lut - compile synchronously as it's needed immediately
            shader(Renderer_Shader::light_integration_brdf_specular_lut_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_integration_brdf_specular_lut_c)->AddDefine("BRDF_SPECULAR_LUT");
            shader(Renderer_Shader::light_integration_brdf_specular_lut_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "light_integration.hlsl", false);

            // environment prefilter - compile synchronously as it's needed immediately
            shader(Renderer_Shader::light_integration_environment_filter_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_integration_environment_filter_c)->AddDefine("ENVIRONMENT_FILTER");
            shader(Renderer_Shader::light_integration_environment_filter_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "light_integration.hlsl", async);

            // light
            shader(Renderer_Shader::light_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "light.hlsl", async);

            // composition
            shader(Renderer_Shader::light_composition_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_composition_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "light_composition.hlsl", async);

            // image based
            shader(Renderer_Shader::light_image_based_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::light_image_based_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "light_image_based.hlsl", async);
        }

        // blur
        {
            // gaussian
            shader(Renderer_Shader::blur_gaussian_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "blur.hlsl", async);

            // gaussian bilateral - or depth aware
            shader(Renderer_Shader::blur_gaussian_bilaterial_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::blur_gaussian_bilaterial_c)->AddDefine("PASS_BLUR_GAUSSIAN_BILATERAL");
            shader(Renderer_Shader::blur_gaussian_bilaterial_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "blur.hlsl", async);
        }

        // bloom
        {
            // downsample luminance
            shader(Renderer_Shader::bloom_luminance_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_luminance_c)->AddDefine("LUMINANCE");
            shader(Renderer_Shader::bloom_luminance_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "bloom.hlsl", async);

            // upsample blend (with previous mip)
            shader(Renderer_Shader::bloom_upsample_blend_mip_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_upsample_blend_mip_c)->AddDefine("UPSAMPLE_BLEND_MIP");
            shader(Renderer_Shader::bloom_upsample_blend_mip_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "bloom.hlsl", async);

            // upsample blend (with frame)
            shader(Renderer_Shader::bloom_blend_frame_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::bloom_blend_frame_c)->AddDefine("BLEND_FRAME");
            shader(Renderer_Shader::bloom_blend_frame_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "bloom.hlsl", async);
        }

        // amd fidelityfx
        {
            // cas - contrast adaptive sharpening
            shader(Renderer_Shader::ffx_cas_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::ffx_cas_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "amd_fidelity_fx\\cas.hlsl", async);

            // spd - single pass downsample - compile synchronously as they are needed everywhere
            {
                shader(Renderer_Shader::ffx_spd_average_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_average_c)->AddDefine("AVERAGE");
                shader(Renderer_Shader::ffx_spd_average_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

                shader(Renderer_Shader::ffx_spd_min_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_min_c)->AddDefine("MIN");
                shader(Renderer_Shader::ffx_spd_min_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

                shader(Renderer_Shader::ffx_spd_max_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_max_c)->AddDefine("MAX");
                shader(Renderer_Shader::ffx_spd_max_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);

                shader(Renderer_Shader::ffx_spd_luminance_c) = make_shared<RHI_Shader>();
                shader(Renderer_Shader::ffx_spd_luminance_c)->AddDefine("LUMINANCE");
                shader(Renderer_Shader::ffx_spd_luminance_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "amd_fidelity_fx\\spd.hlsl", false);
            }
        }

        // sky
        {
            shader(Renderer_Shader::skysphere_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::skysphere_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "skysphere.hlsl", async);

            shader(Renderer_Shader::skysphere_lut_c) = make_shared<RHI_Shader>();
            shader(Renderer_Shader::skysphere_lut_c)->AddDefine("LUT");
            shader(Renderer_Shader::skysphere_lut_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "skysphere.hlsl", async);
        }

        // fxaa
        shader(Renderer_Shader::fxaa_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::fxaa_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "fxaa\\fxaa.hlsl", async);

        // font
        shader(Renderer_Shader::font_v) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::font_v)->Compile(RHI_Shader_Type::Vertex, shader_dir + "font.hlsl", async, RHI_Vertex_Type::PosUv);
        shader(Renderer_Shader::font_p) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::font_p)->Compile(RHI_Shader_Type::Pixel, shader_dir + "font.hlsl", async);

        // film grain
        shader(Renderer_Shader::film_grain_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::film_grain_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "film_grain.hlsl", async);

        // chromatic aberration
        shader(Renderer_Shader::chromatic_aberration_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::chromatic_aberration_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "chromatic_aberration.hlsl", async);

        // vhs
        shader(Renderer_Shader::vhs_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::vhs_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "vhs.hlsl", async);

        // tone-mapping & gamma correction
        shader(Renderer_Shader::output_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::output_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "output.hlsl", async);

        // motion blur
        shader(Renderer_Shader::motion_blur_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::motion_blur_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "motion_blur.hlsl", async);

        // screen space global illumination
        shader(Renderer_Shader::ssao_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::ssao_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "ssao.hlsl", async);

        // screen space shadows
        shader(Renderer_Shader::sss_c_bend) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::sss_c_bend)->Compile(RHI_Shader_Type::Compute, shader_dir + "screen_space_shadows\\bend_sss.hlsl", async);

        // depth of field
        shader(Renderer_Shader::depth_of_field_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::depth_of_field_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "depth_of_field.hlsl", async);

        // variable rate shading
        shader(Renderer_Shader::variable_rate_shading_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::variable_rate_shading_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "variable_rate_shading.hlsl", async);

        // blit
        shader(Renderer_Shader::blit_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::blit_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "blit.hlsl", async);

        // occlusion
        shader(Renderer_Shader::occlusion_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::occlusion_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "occlusion.hlsl", async);

        // icon
        shader(Renderer_Shader::icon_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::icon_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "icon.hlsl", async);

        // dithering
        shader(Renderer_Shader::dithering_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::dithering_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "dithering.hlsl", async);

        // dithering
        shader(Renderer_Shader::transparency_reflection_refraction_c) = make_shared<RHI_Shader>();
        shader(Renderer_Shader::transparency_reflection_refraction_c)->Compile(RHI_Shader_Type::Compute, shader_dir + "transparency_reflection_refraction.hlsl", async);
    }

    void Renderer::CreateFonts()
    {
        // get standard font directory
        const string dir_font = ResourceCache::GetResourceDirectory(ResourceDirectory::Fonts) + "\\";

        // load a font
        uint32_t size = static_cast<uint32_t>(10 * Window::GetDpiScale());
        standard_font = make_shared<Font>(dir_font + "OpenSans/OpenSans-Medium.ttf", size, Color(0.9f, 0.9f, 0.9f, 1.0f));
    }

    void Renderer::CreateStandardMeshes()
    {
        auto create_mesh = [](const MeshType type)
        {
            const string project_directory = ResourceCache::GetProjectDirectory();
            shared_ptr<Mesh> mesh = make_shared<Mesh>();
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;

            if (type == MeshType::Cube)
            {
                geometry_generation::generate_cube(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_cube" + EXTENSION_MESH);
            }
            else if (type == MeshType::Quad)
            {
                geometry_generation::generate_quad(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_quad" + EXTENSION_MESH);
            }
            else if (type == MeshType::Sphere)
            {
                geometry_generation::generate_sphere(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_sphere" + EXTENSION_MESH);
            }
            else if (type == MeshType::Cylinder)
            {
                geometry_generation::generate_cylinder(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_cylinder" + EXTENSION_MESH);
            }
            else if (type == MeshType::Cone)
            {
                geometry_generation::generate_cone(&vertices, &indices);
                mesh->SetResourceFilePath(project_directory + "standard_cone" + EXTENSION_MESH);
            }

            // don't optimize this geometry as it's made to spec
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);

            mesh->AddGeometry(vertices, indices, false);
            mesh->SetType(type);
            mesh->CreateGpuBuffers();

            standard_meshes[static_cast<uint8_t>(type)] = mesh;
        };

        create_mesh(MeshType::Cube);
        create_mesh(MeshType::Quad);
        create_mesh(MeshType::Sphere);
        create_mesh(MeshType::Cylinder);
        create_mesh(MeshType::Cone);

        // this buffers holds all debug primitives that can be drawn
        m_lines_vertex_buffer = make_shared<RHI_Buffer>();
    }

    void Renderer::CreateStandardTextures()
    {
        const string dir_texture   = ResourceCache::GetResourceDirectory(ResourceDirectory::Textures) + "\\";
        const string dir_materials = "project\\materials\\";

        #define standard_texture(x) standard_textures[static_cast<uint32_t>(x)]

        // blue noise textures with 2 channels, each channel containing a different pattern
        {
            standard_texture(Renderer_StandardTexture::Noise_blue_0) = make_shared<RHI_Texture>(dir_texture + "noise_blue_0.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_1) = make_shared<RHI_Texture>(dir_texture + "noise_blue_1.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_2) = make_shared<RHI_Texture>(dir_texture + "noise_blue_2.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_3) = make_shared<RHI_Texture>(dir_texture + "noise_blue_3.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_4) = make_shared<RHI_Texture>(dir_texture + "noise_blue_4.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_5) = make_shared<RHI_Texture>(dir_texture + "noise_blue_5.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_6) = make_shared<RHI_Texture>(dir_texture + "noise_blue_6.png");
            standard_texture(Renderer_StandardTexture::Noise_blue_7) = make_shared<RHI_Texture>(dir_texture + "noise_blue_7.png");
        }

        // gizmos
        {
            standard_texture(Renderer_StandardTexture::Gizmo_light_directional) = make_shared<RHI_Texture>(dir_texture + "sun.png");
            standard_texture(Renderer_StandardTexture::Gizmo_light_point)       = make_shared<RHI_Texture>(dir_texture + "light_bulb.png");
            standard_texture(Renderer_StandardTexture::Gizmo_light_spot)        = make_shared<RHI_Texture>(dir_texture + "flashlight.png");
            standard_texture(Renderer_StandardTexture::Gizmo_audio_source)      = make_shared<RHI_Texture>(dir_texture + "audio.png");
        }

        // misc
        {
            standard_texture(Renderer_StandardTexture::Checkerboard) = make_shared<RHI_Texture>(dir_texture + "no_texture.png");
        }

        // black and white
        {
            // black texture (1x1 pixel, rgba = 0,0,0,255)
            std::vector<RHI_Texture_Mip> black_mips = {
                RHI_Texture_Mip{std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}}} // single pixel: r=0, g=0, b=0, a=255
            };
            std::vector<RHI_Texture_Slice> black_data = {
                RHI_Texture_Slice{black_mips}
            };
            standard_texture(Renderer_StandardTexture::Black) = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,          // type
                1,                                 // width
                1,                                 // height
                1,                                 // depth
                1,                                 // mip_count
                RHI_Format::R8G8B8A8_Unorm,        // format
                RHI_Texture_Srv | RHI_Texture_Uav, // flags
                "black_texture",                   // name
                black_data                         // data
            );
        
            // white texture (1x1 pixel, rgba = 255,255,255,255)
            std::vector<RHI_Texture_Mip> white_mips = {
                RHI_Texture_Mip{std::vector<std::byte>{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}} // single pixel: r=255, g=255, b=255, a=255
            };
            std::vector<RHI_Texture_Slice> white_data = {
                RHI_Texture_Slice{white_mips}
            };
            standard_texture(Renderer_StandardTexture::White) = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,          // type
                1,                                 // width
                1,                                 // height
                1,                                 // depth
                1,                                 // mip_count
                RHI_Format::R8G8B8A8_Unorm,        // format
                RHI_Texture_Srv | RHI_Texture_Uav, // flags
                "white_texture",                   // name
                white_data                         // data
            );
        }
    }

    void Renderer::CreateStandardMaterials()
    {
        const string data_dir = ResourceCache::GetDataDirectory() + "\\";
        FileSystem::CreateDirectory_(data_dir);

        standard_material = make_shared<Material>();
        standard_material->SetResourceFilePath(ResourceCache::GetProjectDirectory() + "standard" + EXTENSION_MATERIAL); // set resource file path so it can be used by the resource cache
        standard_material->SetProperty(MaterialProperty::TextureTilingX, 1.0f);
        standard_material->SetProperty(MaterialProperty::TextureTilingY, 1.0f);
        standard_material->SetProperty(MaterialProperty::ColorR,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorG,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorB,         1.0f);
        standard_material->SetProperty(MaterialProperty::ColorA,         1.0f);
        standard_material->SetProperty(MaterialProperty::WorldSpaceUv,   1.0f);
        standard_material->SetTexture(MaterialTextureType::Color,        Renderer::GetStandardTexture(Renderer_StandardTexture::Checkerboard));
    }

    void Renderer::DestroyResources()
    {
        render_targets.fill(nullptr);
        shaders.fill(nullptr);
        samplers.fill(nullptr);
        standard_textures.fill(nullptr);
        standard_meshes.fill(nullptr);
        buffers.fill(nullptr);
        standard_font     = nullptr;
        standard_material = nullptr;
    }

    array<shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)>& Renderer::GetRenderTargets()
    {
        return render_targets;
    }

    array<shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& Renderer::GetShaders()
    {
        return shaders;
    }

    array<shared_ptr<RHI_Buffer>, static_cast<uint32_t>(Renderer_Buffer::Max)>& Renderer::GetStructuredBuffers()
    {
        return buffers;
    }

    array<shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>& Renderer::GetSamplers()
    {
        return samplers;
    }

    RHI_RasterizerState* Renderer::GetRasterizerState(const Renderer_RasterizerState type)
    {
        return rasterizer_states[static_cast<uint8_t>(type)].get();
    }

    RHI_DepthStencilState* Renderer::GetDepthStencilState(const Renderer_DepthStencilState type)
    {
        return depth_stencil_states[static_cast<uint8_t>(type)].get();
    }

    RHI_BlendState* Renderer::GetBlendState(const Renderer_BlendState type)
    {
        return blend_states[static_cast<uint8_t>(type)].get();
    }

    RHI_Texture* Renderer::GetRenderTarget(const Renderer_RenderTarget type)
    {
        return render_targets[static_cast<uint8_t>(type)].get();
    }

    RHI_Shader* Renderer::GetShader(const Renderer_Shader type)
    {
        return shaders[static_cast<uint8_t>(type)].get();
    }

    RHI_Buffer* Renderer::GetBuffer(const Renderer_Buffer type)
    {
        return buffers[static_cast<uint8_t>(type)].get();
    }

    void Renderer::SwapVisibilityBuffers()
    {
        swap(buffers[static_cast<uint8_t>(Renderer_Buffer::Visibility)], buffers[static_cast<uint8_t>(Renderer_Buffer::VisibilityPrevious)]);
    }

    RHI_Texture* Renderer::GetStandardTexture(const Renderer_StandardTexture type)
    {
        return standard_textures[static_cast<uint8_t>(type)].get();
    }

    shared_ptr<Mesh>& Renderer::GetStandardMesh(const MeshType type)
    {
        return standard_meshes[static_cast<uint8_t>(type)];
    }

    shared_ptr<Font>& Renderer::GetFont()
    {
        return standard_font;
    }

    shared_ptr<Material>& Renderer::GetStandardMaterial()
    {
        return standard_material;
    }
}
