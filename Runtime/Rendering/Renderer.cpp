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
#include "ShaderBuffered.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "Deferred/ShaderLight.h"
#include "Utilities/Sampling.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_RenderTexture.h"
#include "../RHI/RHI_CommandList.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Skybox.h"
#include "../World/Components/Camera.h"
#include <algorithm>
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

namespace Directus
{
	static ResourceCache* g_resource_cache	= nullptr;
	bool Renderer::m_is_rendering			= false;

	Renderer::Renderer(Context* context) : ISubsystem(context)
	{	
		m_near_plane	= 0.0f;
		m_far_plane		= 0.0f;
		m_camera		= nullptr;
		m_rhi_device	= nullptr;
		m_frame_num		= 0;
		m_flags			= 0;
		m_flags			|= Render_Gizmo_Transform;
		m_flags			|= Render_Gizmo_Grid;
		m_flags			|= Render_Gizmo_Lights;
		m_flags			|= Render_Gizmo_Physics;
		m_flags			|= Render_PostProcess_Bloom;	
		m_flags			|= Render_PostProcess_SSAO;	
		m_flags			|= Render_PostProcess_MotionBlur;
		m_flags			|= Render_PostProcess_TAA;
		m_flags			|= Render_PostProcess_Sharpening;	
		m_flags			|= Render_PostProcess_SSR;
		//m_flags		|= Render_PostProcess_Dithering;			// Diasbled by default: It's only needed in very dark scenes to fix smooth color gradients
		//m_flags		|= Render_PostProcess_ChromaticAberration;	// Disabled by default: It doesn't improve the image quality, it's more of a stylistic effect		
		//m_flags		|= Render_PostProcess_FXAA;					// Disabled by default: TAA is superior
		
		// Create RHI device
		m_rhi_device = make_shared<RHI_Device>();
		if (!m_rhi_device->IsInitialized())
		{
			LOG_ERROR("Failed to create device");
			return;
		}

		// Create pipeline
		m_rhi_pipeline = make_shared<RHI_Pipeline>(m_context, m_rhi_device);

		// Create command list
		m_cmd_list = make_shared<RHI_CommandList>(m_rhi_device.get(), m_context->GetSubsystem<Profiler>().get());

		// Create swap chain
		{
			m_swap_chain = make_shared<RHI_SwapChain>
			(
				Settings::Get().GetWindowHandle(),
				m_rhi_device,
				static_cast<unsigned int>(m_resolution.x),
				static_cast<unsigned int>(m_resolution.y),
				m_rhi_device->GetBackBufferFormat(),
				Swap_Flip_Discard,
				SwapChain_Allow_Tearing | SwapChain_Allow_Mode_Switch,
				2
			);

			if (!m_swap_chain->IsInitialized())
			{
				LOG_ERROR("Failed to create swap chain");
				return;
			}
		}

		// Log on-screen as the renderer is ready
		LOG_TO_FILE(false);
		m_initialized = true;

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
		// Create/Get required systems		
		g_resource_cache	= m_context->GetSubsystem<ResourceCache>().get();
		m_profiler			= m_context->GetSubsystem<Profiler>().get();

		// Editor specific
		m_gizmo_grid		= make_unique<Grid>(m_rhi_device);
		m_gizmo_transform	= make_unique<Transform_Gizmo>(m_context);

		// Create a constant buffer that will be used for most shaders
		m_buffer_global = make_shared<RHI_ConstantBuffer>(m_rhi_device, static_cast<unsigned int>(sizeof(ConstantBufferGlobal)));

		// Line buffer
		m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device);

		CreateDepthStencilStates();
		CreateRasterizerStates();
		CreateBlendStates();
		CreateRenderTextures();
		CreateFonts();
		CreateShaders();
		CreateSamplers();
		CreateTextures();
		SetDefaultPipelineState();

		return true;
	}

	void Renderer::CreateDepthStencilStates()
	{
		m_depth_stencil_enabled		= make_shared<RHI_DepthStencilState>(m_rhi_device, true);
		m_depth_stencil_disabled	= make_shared<RHI_DepthStencilState>(m_rhi_device, false);
	}

	void Renderer::CreateRasterizerStates()
	{
		m_rasterizer_cull_back_solid		= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Back,	Fill_Solid,			true, false, false, false);
		m_rasterizer_cull_front_solid		= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Front, Fill_Solid,		true, false, false, false);
		m_rasterizer_cull_none_solid		= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_None,	Fill_Solid,			true, false, false, false);
		m_rasterizer_cull_back_wireframe	= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Back,	Fill_Wireframe,		true, false, false, true);
		m_rasterizer_cull_front_wireframe	= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_Front, Fill_Wireframe,	true, false, false, true);
		m_rasterizer_cull_none_wireframe	= make_shared<RHI_RasterizerState>(m_rhi_device, Cull_None,	Fill_Wireframe,		true, false, false, true);
	}

	void Renderer::CreateBlendStates()
	{
		m_blend_enabled		= make_shared<RHI_BlendState>(m_rhi_device, true);
		m_blend_disabled	= make_shared<RHI_BlendState>(m_rhi_device, false);
	}

	void Renderer::CreateFonts()
	{
		// Get standard font directory
		const auto dir_font = g_resource_cache->GetDataDirectory(Asset_Fonts);

		// Load a font (used for performance metrics)
		m_font = make_unique<Font>(m_context, dir_font + "CalibriBold.ttf", 12, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	}

	void Renderer::CreateTextures()
	{
		// Get standard texture directory
		const auto dir_texture = g_resource_cache->GetDataDirectory(Asset_Textures);

		// Noise texture (used by SSAO shader)
		m_tex_noise_normal = make_shared<RHI_Texture>(m_context);
		m_tex_noise_normal->LoadFromFile(dir_texture + "noise.jpg");

		m_tex_white = make_shared<RHI_Texture>(m_context);
		m_tex_white->SetNeedsMipChain(false);
		m_tex_white->LoadFromFile(dir_texture + "white.png");

		m_tex_black = make_shared<RHI_Texture>(m_context);
		m_tex_black->SetNeedsMipChain(false);
		m_tex_black->LoadFromFile(dir_texture + "black.png");

		m_tex_lut_ibl = make_shared<RHI_Texture>(m_context);
		m_tex_lut_ibl->SetNeedsMipChain(false);
		m_tex_lut_ibl->LoadFromFile(dir_texture + "ibl_brdf_lut.png");

		// Gizmo icons
		m_gizmo_tex_light_directional = make_shared<RHI_Texture>(m_context);
		m_gizmo_tex_light_directional->LoadFromFile(dir_texture + "sun.png");

		m_gizmo_tex_light_point = make_shared<RHI_Texture>(m_context);
		m_gizmo_tex_light_point->LoadFromFile(dir_texture + "light_bulb.png");

		m_gizmo_tex_light_spot = make_shared<RHI_Texture>(m_context);
		m_gizmo_tex_light_spot->LoadFromFile(dir_texture + "flashlight.png");
	}

	void Renderer::CreateRenderTextures()
	{
		auto width	= static_cast<unsigned int>(m_resolution.x);
		auto height	= static_cast<unsigned int>(m_resolution.y);

		if ((width / 4) == 0 || (height / 4) == 0)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Full-screen quad
		m_quad = Rectangle(0, 0, m_resolution.x, m_resolution.y);
		m_quad.CreateBuffers(this);

		// G-Buffer
		m_g_buffer_albedo	= make_shared<RHI_RenderTexture>(m_rhi_device, width, height, Format_R8G8B8A8_UNORM, false);
		m_g_buffer_normal	= make_shared<RHI_RenderTexture>(m_rhi_device, width, height, Format_R16G16B16A16_FLOAT, false); // At Texture_Format_R8G8B8A8_UNORM, normals have noticeable banding
		m_g_buffer_material = make_shared<RHI_RenderTexture>(m_rhi_device, width, height, Format_R8G8B8A8_UNORM, false);
		m_g_buffer_velocity = make_shared<RHI_RenderTexture>(m_rhi_device, width, height, Format_R16G16_FLOAT, false);
		m_g_buffer_depth	= make_shared<RHI_RenderTexture>(m_rhi_device, width, height, Format_R32G32_FLOAT, true, Format_D32_FLOAT);

		// Full res
		m_render_tex_full_hdr_light		= make_unique<RHI_RenderTexture>(m_rhi_device, width, height, Format_R32G32B32A32_FLOAT);
		m_render_tex_full_hdr_light2	= make_unique<RHI_RenderTexture>(m_rhi_device, width, height, Format_R32G32B32A32_FLOAT);
		m_render_tex_full_taa_current	= make_unique<RHI_RenderTexture>(m_rhi_device, width, height, Format_R16G16B16A16_FLOAT);
		m_render_tex_full_taa_history	= make_unique<RHI_RenderTexture>(m_rhi_device, width, height, Format_R16G16B16A16_FLOAT);

		// Half res
		m_render_tex_half_shadows	= make_unique<RHI_RenderTexture>(m_rhi_device, width / 2, height / 2, Format_R8_UNORM);
		m_render_tex_half_ssao		= make_unique<RHI_RenderTexture>(m_rhi_device, width / 2, height / 2, Format_R8_UNORM);
		m_render_tex_half_spare		= make_unique<RHI_RenderTexture>(m_rhi_device, width / 2, height / 2, Format_R8_UNORM);

		// Quarter res
		m_render_tex_quarter_blur1 = make_unique<RHI_RenderTexture>(m_rhi_device, width / 4, height / 4, Format_R16G16B16A16_FLOAT);
		m_render_tex_quarter_blur2 = make_unique<RHI_RenderTexture>(m_rhi_device, width / 4, height / 4, Format_R16G16B16A16_FLOAT);
	}

	void Renderer::CreateShaders()
	{
		// Get standard shader directory
		const auto dir_shaders = g_resource_cache->GetDataDirectory(Asset_Shaders);

		// Light
		m_vps_light = make_shared<ShaderLight>(m_rhi_device);
		m_vps_light->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "Light.hlsl", Input_PositionTexture);

		// Transparent
		m_vps_transparent = make_shared<ShaderBuffered>(m_rhi_device);
		m_vps_transparent->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "Transparent.hlsl", Input_PositionTextureNormalTangent);
		m_vps_transparent->AddBuffer<Struct_Transparency>();

		// Font
		m_vps_font = make_shared<ShaderBuffered>(m_rhi_device);
		m_vps_font->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "Font.hlsl", Input_PositionTexture);
		m_vps_font->AddBuffer<Struct_Matrix_Vector4>();

		// Transform gizmo
		m_vps_gizmo_transform = make_shared<ShaderBuffered>(m_rhi_device);
		m_vps_gizmo_transform->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "TransformGizmo.hlsl", Input_PositionTextureNormalTangent);
		m_vps_gizmo_transform->AddBuffer<Struct_Matrix_Vector3>();
		m_vps_gizmo_transform->AddBuffer<Struct_Matrix_Vector3>();
		m_vps_gizmo_transform->AddBuffer<Struct_Matrix_Vector3>();

		// SSAO
		m_vps_ssao = make_shared<ShaderBuffered>(m_rhi_device);
		m_vps_ssao->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "SSAO.hlsl", Input_PositionTexture);
		m_vps_ssao->AddBuffer<Struct_Matrix_Matrix>();

		// Shadow mapping
		m_vps_shadow_mapping = make_shared<ShaderBuffered>(m_rhi_device);
		m_vps_shadow_mapping->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "ShadowMapping.hlsl", Input_PositionTexture);
		m_vps_shadow_mapping->AddBuffer<Struct_ShadowMapping>();

		// Color
		m_vps_color = make_shared<ShaderBuffered>(m_rhi_device);
		m_vps_color->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "Color.hlsl", Input_PositionColor);
		m_vps_color->AddBuffer<Struct_Matrix_Matrix>();

		// G-Buffer
		m_vs_gbuffer = make_shared<RHI_Shader>(m_rhi_device);
		m_vs_gbuffer->CompileAsync(m_context, Shader_Vertex, dir_shaders + "GBuffer.hlsl", Input_PositionTextureNormalTangent);

		// Depth
		m_vps_depth = make_shared<RHI_Shader>(m_rhi_device);
		m_vps_depth->CompileAsync(m_context, Shader_VertexPixel, dir_shaders + "ShadowingDepth.hlsl", Input_Position3D);

		// Quad
		m_vs_quad = make_shared<RHI_Shader>(m_rhi_device);
		m_vs_quad->CompileAsync(m_context, Shader_Vertex, dir_shaders + "Quad.hlsl", Input_PositionTexture);

		// Texture
		m_ps_texture = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_texture->AddDefine("PASS_TEXTURE");
		m_ps_texture->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// FXAA
		m_ps_fxaa = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_fxaa->AddDefine("PASS_FXAA");
		m_ps_fxaa->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Luma
		m_ps_luma = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_luma->AddDefine("PASS_LUMA");
		m_ps_luma->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Sharpening
		m_ps_sharpening = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_sharpening->AddDefine("PASS_SHARPENING");
		m_ps_sharpening->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Chromatic aberration
		m_ps_chromatic_aberration = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_chromatic_aberration->AddDefine("PASS_CHROMATIC_ABERRATION");
		m_ps_chromatic_aberration->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Blur Box
		m_ps_blur_box = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_blur_box->AddDefine("PASS_BLUR_BOX");
		m_ps_blur_box->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Blur Gaussian Horizontal
		m_ps_blur_gaussian = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_blur_gaussian->AddDefine("PASS_BLUR_GAUSSIAN");
		m_ps_blur_gaussian->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Blur Bilateral Gaussian Horizontal
		m_ps_blur_gaussian_bilateral = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_blur_gaussian_bilateral->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
		m_ps_blur_gaussian_bilateral->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Bloom - bright
		m_ps_bloom_bright = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_bloom_bright->AddDefine("PASS_BRIGHT");
		m_ps_bloom_bright->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Bloom - blend
		m_ps_bloom_blend = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_bloom_blend->AddDefine("PASS_BLEND_ADDITIVE");
		m_ps_bloom_blend->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Tone-mapping
		m_ps_tone_mapping = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_tone_mapping->AddDefine("PASS_TONEMAPPING");
		m_ps_tone_mapping->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Gamma correction
		m_ps_gamma_correction = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_gamma_correction->AddDefine("PASS_GAMMA_CORRECTION");
		m_ps_gamma_correction->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// TAA
		m_ps_taa = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_taa->AddDefine("PASS_TAA_RESOLVE");
		m_ps_taa->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Motion Blur
		m_ps_motion_blur = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_motion_blur->AddDefine("PASS_MOTION_BLUR");
		m_ps_motion_blur->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Dithering
		m_ps_dithering = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_dithering->AddDefine("PASS_DITHERING");
		m_ps_dithering->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Downsample box
		m_ps_downsample_box = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_downsample_box->AddDefine("PASS_DOWNSAMPLE_BOX");
		m_ps_downsample_box->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Quad.hlsl");

		// Debug Normal
		m_ps_debug_normal_ = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_debug_normal_->AddDefine("DEBUG_NORMAL");
		m_ps_debug_normal_->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Debug.hlsl");

		// Debug velocity
		m_ps_debug_velocity = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_debug_velocity->AddDefine("DEBUG_VELOCITY");
		m_ps_debug_velocity->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Debug.hlsl");

		// Debug depth
		m_ps_debug_depth = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_debug_depth->AddDefine("DEBUG_DEPTH");
		m_ps_debug_depth->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Debug.hlsl");

		// Debug ssao
		m_ps_debug_ssao = make_shared<RHI_Shader>(m_rhi_device);
		m_ps_debug_ssao->AddDefine("DEBUG_SSAO");
		m_ps_debug_ssao->CompileAsync(m_context, Shader_Pixel, dir_shaders + "Debug.hlsl");
	}

	void Renderer::CreateSamplers()
	{
		m_sampler_compare_depth		= make_shared<RHI_Sampler>(m_rhi_device, Texture_Filter_Comparison_Bilinear,	Sampler_Address_Clamp,	Comparison_Greater);
		m_sampler_point_clamp		= make_shared<RHI_Sampler>(m_rhi_device, Texture_Filter_Point,					Sampler_Address_Clamp,	Comparison_Always);
		m_sampler_bilinear_clamp	= make_shared<RHI_Sampler>(m_rhi_device, Texture_Filter_Bilinear,				Sampler_Address_Clamp,	Comparison_Always);
		m_sampler_bilinear_wrap		= make_shared<RHI_Sampler>(m_rhi_device, Texture_Filter_Bilinear,				Sampler_Address_Wrap,	Comparison_Always);
		m_sampler_trilinear_clamp	= make_shared<RHI_Sampler>(m_rhi_device, Texture_Filter_Trilinear,				Sampler_Address_Clamp,	Comparison_Always);
		m_sampler_anisotropic_wrap	= make_shared<RHI_Sampler>(m_rhi_device, Texture_Filter_Anisotropic,			Sampler_Address_Wrap,	Comparison_Always);
	}

	void Renderer::SetDefaultPipelineState() const
	{
		if (!m_rhi_pipeline)
			return;

		m_rhi_pipeline->Clear();
		m_rhi_pipeline->SetViewport(m_viewport);
		m_rhi_pipeline->SetDepthStencilState(m_depth_stencil_disabled);
		m_rhi_pipeline->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_rhi_pipeline->SetBlendState(m_blend_disabled);
		m_rhi_pipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhi_pipeline->Bind();
	}

	shared_ptr<Entity>& Renderer::SnapTransformGizmoTo(shared_ptr<Entity>& entity) const
	{
		return m_gizmo_transform->SetSelectedEntity(entity);
	}

	void* Renderer::GetFrameShaderResource() const
	{
		return m_render_tex_full_hdr_light2 ? m_render_tex_full_hdr_light2->GetShaderResource() : nullptr;
	}

	void Renderer::Tick()
	{
		if (!m_rhi_device || !m_rhi_device->IsInitialized())
			return;

		// If there is no camera, do nothing
		if (!m_camera)
		{
			m_render_tex_full_hdr_light2->Clear(0.0f, 0.0f, 0.0f, 1.0f);
			m_is_rendering = false;
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_entities.empty())
		{
			m_render_tex_full_hdr_light2->Clear(m_camera->GetClearColor());
			m_is_rendering = false;
			return;
		}

		m_is_rendering = true;
		m_frame_num++;
		m_is_odd_frame = (m_frame_num % 2) == 1;
		m_profiler->Reset();
		TIME_BLOCK_START_MULTI(m_profiler);

		// Get camera matrices
		{
			m_near_plane	= m_camera->GetNearPlane();
			m_far_plane		= m_camera->GetFarPlane();
			m_view			= m_camera->GetViewMatrix();
			m_view_base		= m_camera->GetBaseViewMatrix();
			m_projection	= m_camera->GetProjectionMatrix();

			// TAA - Generate jitter
			if (Flags_IsSet(Render_PostProcess_TAA))
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
			m_projection_orthographic		= Matrix::CreateOrthographicLH(m_resolution.x, m_resolution.y, m_near_plane, m_far_plane);
			m_view_projection_orthographic	= m_view_base * m_projection_orthographic;
		}

		Pass_DepthDirectionalLight(GetLightDirectional());
		
		Pass_GBuffer();

		Pass_PreLight(
			m_render_tex_half_spare,	// IN:	
			m_render_tex_half_shadows,	// OUT: Shadows
			m_render_tex_half_ssao		// OUT: DO
		);

		Pass_Light(
			m_render_tex_half_shadows,	// IN:	Shadows
			m_render_tex_half_ssao,		// IN:	SSAO
			m_render_tex_full_hdr_light	// Out: Result
		);

		Pass_Transparent(m_render_tex_full_hdr_light);

		Pass_PostLight(
			m_render_tex_full_hdr_light,	// IN:	Light pass result
			m_render_tex_full_hdr_light2	// OUT: Result
		);
	
		Pass_Lines(m_render_tex_full_hdr_light2);
		Pass_Gizmos(m_render_tex_full_hdr_light2);
		Pass_DebugBuffer(m_render_tex_full_hdr_light2);	
		Pass_PerformanceMetrics(m_render_tex_full_hdr_light2);

		m_is_rendering = false;
		TIME_BLOCK_END(m_profiler);
	}

	void Renderer::SetResolution(unsigned int width, unsigned int height)
	{
		// Return if resolution is invalid
		if (width == 0 || width > m_max_resolution || height == 0 || height > m_max_resolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Return if resolution already set
		if (m_resolution.x == width && m_resolution.y == height)
			return;

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

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

	void Renderer::SetDefaultBuffer(const unsigned int resolution_width, const unsigned int resolution_height, const Matrix& mMVP, const float blur_sigma, const Vector2& blur_direction, bool bind) const
	{
		auto buffer = static_cast<ConstantBufferGlobal*>(m_buffer_global->Map());
		if (!buffer)
		{
			LOGF_ERROR("Failed to map buffer");
			return;
		}

		buffer->m_mvp					= mMVP;
		buffer->m_view					= m_view;
		buffer->m_projection			= m_projection;
		buffer->m_projection_ortho		= m_projection_orthographic;
		buffer->m_view_projection		= m_view_projection;
		buffer->m_view_projection_ortho	= m_view_projection_orthographic;
		buffer->camera_position			= m_camera->GetTransform()->GetPosition();
		buffer->camera_near				= m_camera->GetNearPlane();
		buffer->camera_far				= m_camera->GetFarPlane();
		buffer->resolution				= Vector2(static_cast<float>(resolution_width), static_cast<float>(resolution_height));
		buffer->fxaa_sub_pixel			= m_fxaa_sub_pixel;
		buffer->fxaa_edge_threshold		= m_fxaa_edge_threshold;
		buffer->fxaa_edge_threshold_min	= m_fxaa_edge_threshold_min;
		buffer->blur_direction			= blur_direction;
		buffer->blur_sigma				= blur_sigma;
		buffer->bloom_intensity			= m_bloom_intensity;
		buffer->sharpen_strength		= m_sharpen_strength;
		buffer->sharpen_clamp			= m_sharpen_clamp;
		buffer->taa_jitter_offset		= m_taa_jitter - m_taa_jitter_previous;
		buffer->motion_blur_strength	= m_motion_blur_strength;
		buffer->fps_current				= m_profiler->GetFps();
		buffer->fps_target				= Settings::Get().GetFpsTarget();	
		buffer->tonemapping				= static_cast<float>(m_tonemapping);
		buffer->exposure				= m_exposure;
		buffer->gamma					= m_gamma;

		m_buffer_global->Unmap();
		if (bind)
		{
			m_rhi_pipeline->SetConstantBuffer(m_buffer_global, 0, Buffer_Global);
		}
	}

	void Renderer::RenderablesAcquire(const Variant& entities_variant)
	{
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
				const auto is_transparent = !renderable->MaterialExists() ? false : renderable->MaterialPtr()->GetColorAlbedo().w < 1.0f;
				if (!skybox) // Ignore skybox
				{
					m_entities[is_transparent ? Renderable_ObjectTransparent : Renderable_ObjectOpaque].emplace_back(entity);
				}
			}

			if (light)
			{
				m_entities[Renderable_Light].emplace_back(entity);
			}

			if (skybox)
			{
				m_skybox = skybox;
			}

			if (camera)
			{
				m_entities[Renderable_Camera].emplace_back(entity);
				m_camera = camera;
			}
		}

		RenderablesSort(&m_entities[Renderable_ObjectOpaque]);
		RenderablesSort(&m_entities[Renderable_ObjectTransparent]);

		TIME_BLOCK_END(m_profiler);
	}

	void Renderer::RenderablesSort(vector<Entity*>* renderables)
	{
		if (renderables->size() <= 2)
			return;

		// Sort by depth (front to back)
		if (m_camera)
		{
			sort(renderables->begin(), renderables->end(), [this](Entity* a, Entity* b)
			{
				// Get renderable component
				auto a_renderable = a->GetRenderable_PtrRaw();
				auto b_renderable = b->GetRenderable_PtrRaw();
				if (!a_renderable || !b_renderable)
					return false;

				// Get materials
				const auto a_material = a_renderable->MaterialPtr();
				const auto b_material = b_renderable->MaterialPtr();
				if (!a_material || !b_material)
					return false;

				const auto a_depth = (a_renderable->GeometryAabb().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();
				const auto b_depth = (b_renderable->GeometryAabb().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();

				return a_depth < b_depth;
			});
		}

		// Sort by material
		sort(renderables->begin(), renderables->end(), [](Entity* a, Entity* b)
		{
			// Get renderable component
			const auto a_renderable = a->GetRenderable_PtrRaw();
			const auto b_renderable = b->GetRenderable_PtrRaw();
			if (!a_renderable || !b_renderable)
				return false;

			// Get materials
			const auto a_material = a_renderable->MaterialPtr();
			const auto b_material = b_renderable->MaterialPtr();
			if (!a_material || !b_material)
				return false;

			// Order doesn't matter, as long as they are not mixed
			return a_material->GetResourceId() < b_material->GetResourceId();
		});
	}

	shared_ptr<RHI_RasterizerState>& Renderer::GetRasterizerState(const RHI_Cull_Mode cull_mode, const RHI_Fill_Mode fill_mode)
	{
		if (cull_mode == Cull_Back)		return (fill_mode == Fill_Solid) ? m_rasterizer_cull_back_solid		: m_rasterizer_cull_back_wireframe;
		if (cull_mode == Cull_Front)	return (fill_mode == Fill_Solid) ? m_rasterizer_cull_front_solid	: m_rasterizer_cull_front_wireframe;
		if (cull_mode == Cull_None)		return (fill_mode == Fill_Solid) ? m_rasterizer_cull_none_solid		: m_rasterizer_cull_none_wireframe;

		return m_rasterizer_cull_back_solid;
	}

	Light* Renderer::GetLightDirectional()
	{
		auto entities = m_entities[Renderable_Light];

		for (const auto& entity : entities)
		{
			auto light = entity->GetComponent<Light>().get();
			if (light->GetLightType() == LightType_Directional)
				return light;
		}

		return nullptr;
	}
}
