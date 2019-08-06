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

//= INCLUDES ==============================
#include "Renderer.h"
#include <algorithm>
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "Shaders/ShaderBuffered.h"
#include "Utilities/Sampling.h"
#include "Font/Font.h"
#include "../Math/MathHelper.h"
#include "../Core/Engine.h"
#include "../Core/Context.h"
#include "../Core/Timer.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ResourceCache.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Skybox.h"
#include "../World/Components/Camera.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_PipelineCache.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Texture2D.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	Renderer::Renderer(Context* context) : ISubsystem(context)
	{	
		m_flags			|= Render_Gizmo_Transform;
		m_flags			|= Render_Gizmo_Grid;
		m_flags			|= Render_Gizmo_Lights;
		m_flags			|= Render_Gizmo_Physics;
		m_flags			|= Render_PostProcess_Bloom;	
		m_flags			|= Render_PostProcess_SSAO;	
		m_flags			|= Render_PostProcess_MotionBlur;
		m_flags			|= Render_PostProcess_TAA;
		//m_flags		|= Render_PostProcess_FXAA;                 // Disabled by default: TAA is superior
		m_flags			|= Render_PostProcess_Sharpening;	
		m_flags			|= Render_PostProcess_SSR;
		//m_flags		|= Render_PostProcess_Dithering;			// Disabled by default: It's only needed in very dark scenes to fix smooth color gradients
		//m_flags		|= Render_PostProcess_ChromaticAberration;	// Disabled by default: It doesn't improve the image quality, it's more of a stylistic effect		

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_World_Submit, EVENT_HANDLER_VARIANT(RenderablesAcquire));
	}

	Renderer::~Renderer()
	{
		// Unsubscribe from events
		UNSUBSCRIBE_FROM_EVENT(Event_World_Submit, EVENT_HANDLER_VARIANT(RenderablesAcquire));

		m_entities.clear();
		m_camera = nullptr;

		// Log to file as the renderer is no more
		LOG_TO_FILE(true);
	}

	bool Renderer::Initialize()
	{
        // Get required systems		
        m_resource_cache    = m_context->GetSubsystem<ResourceCache>().get();
        m_profiler          = m_context->GetSubsystem<Profiler>().get();

        // Create device
        m_rhi_device = make_shared<RHI_Device>(m_context);
        if (!m_rhi_device->IsInitialized())
        {
            LOG_ERROR("Failed to create device");
            return false;
        }

        // Create swap chain
        {
            m_swap_chain = make_shared<RHI_SwapChain>
            (
                m_context->m_engine->GetWindowHandle(),
                m_rhi_device,
                static_cast<uint32_t>(m_context->m_engine->GetWindowWidth()),
                static_cast<uint32_t>(m_context->m_engine->GetWindowHeight()),
                Format_R8G8B8A8_UNORM,
                2,
                Present_Immediate | Swap_Flip_Discard
            );

            if (!m_swap_chain->IsInitialized())
            {
                LOG_ERROR("Failed to create swap chain");
                return false;
            }
        }

        // Create pipeline cache
        m_pipeline_cache = make_shared<RHI_PipelineCache>(m_rhi_device);

        // Create command list
        m_cmd_list = make_shared<RHI_CommandList>(m_rhi_device, m_profiler);

		// Editor specific
		m_gizmo_grid		= make_unique<Grid>(m_rhi_device);
		m_gizmo_transform	= make_unique<Transform_Gizmo>(m_context);

		// Create a constant buffer that will be used for most shaders
		m_uber_buffer = make_shared<RHI_ConstantBuffer>(m_rhi_device);
		m_uber_buffer->Create<UberBuffer>();

		// Line buffer
		m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device);

		CreateShaders();
		CreateDepthStencilStates();
		CreateRasterizerStates();
		CreateBlendStates();
		CreateRenderTextures();
		CreateFonts();	
		CreateSamplers();
		CreateTextures();

		if (!m_initialized)
		{
			// Log on-screen as the renderer is ready
			LOG_TO_FILE(false);
			m_initialized = true;
		}

		return true;
	}

	void Renderer::CreateDepthStencilStates()
	{
		m_depth_stencil_enabled		= make_shared<RHI_DepthStencilState>(m_rhi_device, true, GetComparisonFunction());
		m_depth_stencil_disabled	= make_shared<RHI_DepthStencilState>(m_rhi_device, false, GetComparisonFunction());
	}

	void Renderer::CreateRasterizerStates()
	{
		m_rasterizer_cull_back_solid		= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Back,		Fill_Solid,		true, false, false, false);
		m_rasterizer_cull_front_solid		= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Front,	Fill_Solid,		true, false, false, false);
		m_rasterizer_cull_none_solid		= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_None,		Fill_Solid,		true, false, false, false);
		m_rasterizer_cull_back_wireframe	= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Back,		Fill_Wireframe,	true, false, false, true);
		m_rasterizer_cull_front_wireframe	= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Front,	Fill_Wireframe,	true, false, false, true);
		m_rasterizer_cull_none_wireframe	= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_None,		Fill_Wireframe,	true, false, false, true);
	}

	void Renderer::CreateBlendStates()
	{	
		m_blend_disabled    = make_shared<RHI_BlendState>(m_rhi_device, false);
        m_blend_enabled     = make_shared<RHI_BlendState>(m_rhi_device, true);
        m_blend_color_add   = make_shared<RHI_BlendState>(m_rhi_device, true, Blend_One, Blend_One, Blend_Operation_Add);
        m_blend_color_max   = make_shared<RHI_BlendState>(m_rhi_device, true, Blend_One, Blend_One, Blend_Operation_Max);
		m_blend_color_min   = make_shared<RHI_BlendState>(m_rhi_device, true, Blend_One, Blend_One, Blend_Operation_Min);
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

		m_tex_brdf_specular_lut = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
		m_tex_brdf_specular_lut->LoadFromFile(dir_texture + "ibl_brdf_lut.png");

		// Gizmo icons
		m_gizmo_tex_light_directional = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
		m_gizmo_tex_light_directional->LoadFromFile(dir_texture + "sun.png");

		m_gizmo_tex_light_point = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
		m_gizmo_tex_light_point->LoadFromFile(dir_texture + "light_bulb.png");

		m_gizmo_tex_light_spot = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
		m_gizmo_tex_light_spot->LoadFromFile(dir_texture + "flashlight.png");
	}

	void Renderer::CreateRenderTextures()
	{
		auto width	= static_cast<uint32_t>(m_resolution.x);
		auto height	= static_cast<uint32_t>(m_resolution.y);

		if ((width / 4) == 0 || (height / 4) == 0)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Full-screen quad
		m_quad = Math::Rectangle(0, 0, m_resolution.x, m_resolution.y);
		m_quad.CreateBuffers(this);

		// G-Buffer
		m_g_buffer_albedo	= make_shared<RHI_Texture2D>(m_context, width, height, Format_R8G8B8A8_UNORM);
		m_g_buffer_normal	= make_shared<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT); // At Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding
		m_g_buffer_material = make_shared<RHI_Texture2D>(m_context, width, height, Format_R8G8B8A8_UNORM);
		m_g_buffer_velocity = make_shared<RHI_Texture2D>(m_context, width, height, Format_R16G16_FLOAT);
		m_g_buffer_depth	= make_shared<RHI_Texture2D>(m_context, width, height, Format_D32_FLOAT);

        // Light
        m_render_tex_light_diffuse     = make_unique<RHI_Texture2D>(m_context, width, height, Format_R32G32B32A32_FLOAT);
        m_render_tex_light_specular    = make_unique<RHI_Texture2D>(m_context, width, height, Format_R32G32B32A32_FLOAT);

        // BRDF Specular Lut
        m_tex_brdf_specular_lut         = make_unique<RHI_Texture2D>(m_context, 400, 400, Format_R8G8_UNORM);
        m_brdf_specular_lut_rendered    = false;

		// Composition
		m_render_tex_composition			= make_unique<RHI_Texture2D>(m_context, width, height, Format_R32G32B32A32_FLOAT);
		m_render_tex_composition_previous	= make_unique<RHI_Texture2D>(m_context, width, height, Format_R32G32B32A32_FLOAT);

        // TAA
		m_render_tex_taa_current = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);
		m_render_tex_taa_history = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);

        // Final
		m_render_tex_final = make_unique<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);

		// SSAO
		m_render_tex_half_ssao	        = make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, Format_R8_UNORM);  // Raw
        m_render_tex_half_ssao_blurred	= make_unique<RHI_Texture2D>(m_context, width / 2, height / 2, Format_R8_UNORM);  // Blurred
        m_render_tex_ssao	            = make_unique<RHI_Texture2D>(m_context, width, height, Format_R8_UNORM);          // Upscaled

        // SSR
        m_render_tex_ssr = make_shared<RHI_Texture2D>(m_context, width, height, Format_R16G16B16A16_FLOAT);

		// Quarter res
		m_render_tex_quarter_blur1 = make_unique<RHI_Texture2D>(m_context, width / 4, height / 4, Format_R16G16B16A16_FLOAT);
		m_render_tex_quarter_blur2 = make_unique<RHI_Texture2D>(m_context, width / 4, height / 4, Format_R16G16B16A16_FLOAT);

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

        // BRDF specular lut
        auto shader_brdf_specular_lut = make_shared<RHI_Shader>(m_rhi_device);
        shader_brdf_specular_lut->CompileAsync(m_context, Shader_Pixel, dir_shaders + "BRDF_SpecularLut.hlsl");
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

		// Sharpening
		auto shader_sharpening = make_shared<RHI_Shader>(m_rhi_device);
		shader_sharpening->AddDefine("PASS_SHARPENING");
		shader_sharpening->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
		m_shaders[Shader_Sharperning_P] = shader_sharpening;

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
		shader_blurGaussian->AddBuffer<Struct_Blur>();
		m_shaders[Shader_BlurGaussian_P] = shader_blurGaussian;

		// Blur Bilateral Gaussian
		auto shader_blurGaussianBilateral = make_shared<ShaderBuffered>(m_rhi_device);
		shader_blurGaussianBilateral->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
		shader_blurGaussianBilateral->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");
		shader_blurGaussianBilateral->AddBuffer<Struct_Blur>();
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

	void Renderer::CreateSamplers()
	{
		m_sampler_compare_depth		= make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,	Sampler_Address_Clamp,	m_reverse_z ? Comparison_Greater : Comparison_Less, false, true);
		m_sampler_point_clamp		= make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_POINT,		Sampler_Address_Clamp,	Comparison_Always, false);
		m_sampler_bilinear_clamp	= make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,	Sampler_Address_Clamp,	Comparison_Always, false);
		m_sampler_bilinear_wrap		= make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_BILINEAR,	Sampler_Address_Wrap,	Comparison_Always, false);
		m_sampler_trilinear_clamp	= make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR,	Sampler_Address_Clamp,	Comparison_Always, false);
		m_sampler_anisotropic_wrap	= make_shared<RHI_Sampler>(m_rhi_device, SAMPLER_TRILINEAR,	Sampler_Address_Wrap,	Comparison_Always, true);
	}

	const shared_ptr<Entity>& Renderer::SnapTransformGizmoTo(const shared_ptr<Entity>& entity) const
	{
		return m_gizmo_transform->SetSelectedEntity(entity);
	}

    void Renderer::SetShadowResolution(uint32_t resolution)
    {
        resolution = Clamp(resolution, m_resolution_shadow_min, m_max_resolution);

        if (resolution == m_resolution_shadow)
            return;

        m_resolution_shadow = resolution;

        const auto& light_entities = m_entities[Renderer_Object_Light];
        for (const auto& light_entity : light_entities)
        {
            auto& light = light_entity->GetComponent<Light>();
            if (light->GetCastShadows())
            {
                light->CreateShadowMap(true);
            }
        }
    }

    void Renderer::SetAnisotropy(uint32_t anisotropy)
    {
        uint32_t min = 0;
        uint32_t max = 16;
        m_anisotropy = Math::Clamp(anisotropy, min, max);
    }

    void Renderer::Tick(float delta_time)
	{
#ifdef API_GRAPHICS_VULKAN
		return;
#endif
		if (!m_rhi_device || !m_rhi_device->IsInitialized())
			return;

		// If there is no camera, do nothing
		if (!m_camera)
		{
			m_cmd_list->ClearRenderTarget(m_render_tex_final->GetResource_RenderTarget(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_entities.empty())
		{
			m_cmd_list->ClearRenderTarget(m_render_tex_final->GetResource_RenderTarget(), m_camera->GetClearColor());
			return;
		}

		m_frame_num++;
		m_is_odd_frame = (m_frame_num % 2) == 1;

		// Get camera matrices
		{
			m_near_plane	= m_camera->GetNearPlane();
			m_far_plane		= m_camera->GetFarPlane();
			m_view			= m_camera->GetViewMatrix();
			m_view_base		= m_camera->GetBaseViewMatrix();
			m_projection	= m_camera->GetProjectionMatrix();

			// TAA - Generate jitter
			if (FlagEnabled(Render_PostProcess_TAA))
			{
				m_taa_jitter_previous = m_taa_jitter;

				// Halton(2, 3) * 16 seems to work nice
				const uint64_t samples	= 16;
				const uint64_t index	= m_frame_num % samples;
				m_taa_jitter			= Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f;
				m_taa_jitter.x			= m_taa_jitter.x / m_resolution.x;
				m_taa_jitter.y			= m_taa_jitter.y / m_resolution.y;
				m_projection			*= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
			}
			else
			{
				m_taa_jitter			= Vector2::Zero;
				m_taa_jitter_previous	= Vector2::Zero;		
			}

			m_view_projection				= m_view * m_projection;
			m_view_projection_inv			= Matrix::Invert(m_view_projection);
			m_projection_orthographic		= Matrix::CreateOrthographicLH(m_resolution.x, m_resolution.y, m_near_plane, m_far_plane);
			m_view_projection_orthographic	= m_view_base * m_projection_orthographic;
		}

        // Deduce some light data
        {
            uint32_t light_directional_count = 0;
            m_directional_light_avg_dir = Vector3::Zero;
            m_directional_light_avg_intensity = 0.0f;

            for (const auto& light_entity : m_entities[Renderer_Object_LightDirectional])
            {
                const auto& light = light_entity->GetComponent<Light>();
                m_directional_light_avg_dir += light->GetDirection();
                m_directional_light_avg_intensity += light->GetIntensity();
                light_directional_count++;
            }

            // Compute average directional light direction and intensity
            m_directional_light_avg_dir /= static_cast<float>(light_directional_count);
            m_directional_light_avg_intensity /= static_cast<float>(light_directional_count);
        }

		m_is_rendering = true;
		Pass_Main();
		m_is_rendering = false;
	}

	void Renderer::SetResolution(uint32_t width, uint32_t height)
	{
		// Return if resolution is invalid
		if (width == 0 || width > m_max_resolution || height == 0 || height > m_max_resolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

        // Silently return if resolution is already set
        if (m_resolution.x == width && m_resolution.y == height)
            return;

		// Set resolution
		m_resolution.x = static_cast<float>(width);
		m_resolution.y = static_cast<float>(height);

		// Re-create render textures
		CreateRenderTextures();

		// Log
		LOGF_INFO("Resolution set to %dx%d", width, height);
	}

	void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Vector4& color_from, const Vector4& color_to, const bool depth /*= true*/)
	{
		if (depth)
		{
			m_lines_list_depth_enabled.emplace_back(from, color_from);
			m_lines_list_depth_enabled.emplace_back(to, color_to);
		}
		else
		{
			m_lines_list_depth_disabled.emplace_back(from, color_from);
			m_lines_list_depth_disabled.emplace_back(to, color_to);
		}
	}

	void Renderer::DrawBox(const BoundingBox& box, const Vector4& color, const bool depth /*= true*/)
	{
		const auto& min = box.GetMin();
		const auto& max = box.GetMax();
	
		DrawLine(Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z), color, depth);
		DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, depth);
		DrawLine(Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z), color, depth);
		DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, min.y, min.z), color, depth);
		DrawLine(Vector3(min.x, min.y, min.z), Vector3(min.x, min.y, max.z), color, depth);
		DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, depth);
		DrawLine(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, depth);
		DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, max.y, max.z), color, depth);
		DrawLine(Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z), color, depth);
		DrawLine(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, depth);
		DrawLine(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, depth);
		DrawLine(Vector3(min.x, max.y, max.z), Vector3(min.x, min.y, max.z), color, depth);
	}

	bool Renderer::UpdateUberBuffer(const uint32_t resolution_width, const uint32_t resolution_height, const Matrix& mvp)
	{
		auto buffer = static_cast<UberBuffer*>(m_uber_buffer->Map());
		if (!buffer)
		{
			LOGF_ERROR("Failed to map buffer");
			return false;
		}

		buffer->m_mvp					    = mvp;
		buffer->m_view					    = m_view;
		buffer->m_projection			    = m_projection;
		buffer->m_projection_ortho		    = m_projection_orthographic;
		buffer->m_view_projection		    = m_view_projection;
		buffer->m_view_projection_inv	    = m_view_projection_inv;
		buffer->m_view_projection_ortho	    = m_view_projection_orthographic;
		buffer->camera_position			    = m_camera->GetTransform()->GetPosition();
		buffer->camera_near				    = m_camera->GetNearPlane();
		buffer->camera_far				    = m_camera->GetFarPlane();
		buffer->resolution				    = Vector2(static_cast<float>(resolution_width), static_cast<float>(resolution_height));
		buffer->fxaa_sub_pixel			    = m_fxaa_sub_pixel;
		buffer->fxaa_edge_threshold		    = m_fxaa_edge_threshold;
		buffer->fxaa_edge_threshold_min	    = m_fxaa_edge_threshold_min;
		buffer->bloom_intensity			    = m_bloom_intensity;
		buffer->sharpen_strength		    = m_sharpen_strength;
		buffer->sharpen_clamp			    = m_sharpen_clamp;
		buffer->taa_jitter_offset		    = m_taa_jitter - m_taa_jitter_previous;
		buffer->motion_blur_strength	    = m_motion_blur_strength;
		buffer->fps_current				    = m_profiler->GetFps();
		buffer->fps_target				    = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTargetFps());	
		buffer->tonemapping				    = static_cast<float>(m_tonemapping);
		buffer->exposure				    = m_exposure;
		buffer->gamma					    = m_gamma;
        buffer->directional_light_intensity = m_directional_light_avg_intensity;
        buffer->ssr_enabled                 = FlagEnabled(Render_PostProcess_SSR) ? 1.0f : 0.0f;
        buffer->shadow_resolution           = static_cast<float>(m_resolution_shadow);

		return m_uber_buffer->Unmap();
	}

	void Renderer::RenderablesAcquire(const Variant& entities_variant)
	{
        while(m_acquiring_renderables)
        {
            LOGF_WARNING("Waiting for previous operation to finish...");
        }
        m_acquiring_renderables = true;

		TIME_BLOCK_START_CPU(m_profiler);

		// Clear previous state
		m_entities.clear();
		m_camera = nullptr;
		m_skybox = nullptr;
		
		auto entities_vec = entities_variant.Get<vector<shared_ptr<Entity>>>();
		for (const auto& entitieshared : entities_vec)
		{
			auto entity = entitieshared.get();
			if (!entity)
				continue;

			// Get all the components we are interested in
			auto renderable = entity->GetComponent<Renderable>();
			auto light		= entity->GetComponent<Light>();
			auto skybox		= entity->GetComponent<Skybox>();
			auto camera		= entity->GetComponent<Camera>();

			if (renderable)
			{
				const auto is_transparent = !renderable->HasMaterial() ? false : renderable->GetMaterial()->GetColorAlbedo().w < 1.0f;
				if (!skybox) // Ignore skybox
				{
					m_entities[is_transparent ? Renderer_Object_Transparent : Renderer_Object_Opaque].emplace_back(entity);
				}
			}

			if (light)
			{
				m_entities[Renderer_Object_Light].emplace_back(entity);

                if (light->GetLightType() == LightType_Directional) m_entities[Renderer_Object_LightDirectional].emplace_back(entity);
                if (light->GetLightType() == LightType_Point)       m_entities[Renderer_Object_LightPoint].emplace_back(entity);
                if (light->GetLightType() == LightType_Spot)        m_entities[Renderer_Object_LightSpot].emplace_back(entity);
			}

			if (skybox)
			{
				m_skybox = skybox;
			}

			if (camera)
			{
				m_entities[Renderer_Object_Camera].emplace_back(entity);
				m_camera = camera;
			}
		}

		RenderablesSort(&m_entities[Renderer_Object_Opaque]);
		RenderablesSort(&m_entities[Renderer_Object_Transparent]);

		TIME_BLOCK_END(m_profiler);

        m_acquiring_renderables = false;
	}

	void Renderer::RenderablesSort(vector<Entity*>* renderables)
	{
		if (!m_camera || renderables->size() <= 2)
			return;

		auto render_hash = [this](Entity* entity)
		{
			// Get renderable
			auto renderable = entity->GetRenderable_PtrRaw();
			if (!renderable)
				return 0.0f;

			// Get material
			const auto material = renderable->GetMaterial();
			if (!material)
				return 0.0f;

			const auto num_depth    = (renderable->GetAabb().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();
			const auto num_material = static_cast<float>(material->GetId());

			return stof(to_string(num_depth) + "-" + to_string(num_material));
		};

		// Sort by depth (front to back), then sort by material		
		sort(renderables->begin(), renderables->end(), [&render_hash](Entity* a, Entity* b)
		{
            return render_hash(a) < render_hash(b);
		});
	}

	shared_ptr<RHI_RasterizerState>& Renderer::GetRasterizerState(const RHI_Cull_Mode cull_mode, const RHI_Fill_Mode fill_mode)
	{
		if (cull_mode == Cull_Back)		return (fill_mode == Fill_Solid) ? m_rasterizer_cull_back_solid		: m_rasterizer_cull_back_wireframe;
		if (cull_mode == Cull_Front)	return (fill_mode == Fill_Solid) ? m_rasterizer_cull_front_solid	: m_rasterizer_cull_front_wireframe;
		if (cull_mode == Cull_None)		return (fill_mode == Fill_Solid) ? m_rasterizer_cull_none_solid		: m_rasterizer_cull_none_wireframe;

		return m_rasterizer_cull_back_solid;
	}
}
