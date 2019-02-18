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
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "Utilities/Sampling.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommonBuffers.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_BlendState.h"
#include "../RHI/RHI_SwapChain.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Skybox.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

namespace Directus
{
	static ResourceCache* g_resourceCache	= nullptr;
	bool Renderer::m_isRendering			= false;

	Renderer::Renderer(Context* context) : ISubsystem(context)
	{	
		m_nearPlane		= 0.0f;
		m_farPlane		= 0.0f;
		m_camera		= nullptr;
		m_rhiDevice		= nullptr;
		m_frameNum		= 0;
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
		m_flags			|= Render_PostProcess_Dithering;
		m_flags			|= Render_PostProcess_SSR;
		//m_flags		|= Render_PostProcess_ChromaticAberration;	// Disabled by default: It doesn't improve the image quality, it's more of a stylistic effect		
		//m_flags		|= Render_PostProcess_FXAA;					// Disabled by default: TAA is superior
		
		// Create RHI device
		RHI_Format backBufferFormat = Format_R8G8B8A8_UNORM;
		m_rhiDevice		= make_shared<RHI_Device>(Settings::Get().GetWindowHandle());
		m_rhiDevice->DetectPrimaryAdapter(backBufferFormat);
		m_rhiPipeline	= make_shared<RHI_Pipeline>(m_context, m_rhiDevice);
		m_swapChain		= make_unique<RHI_SwapChain>
		(
			Settings::Get().GetWindowHandle(),
			m_rhiDevice,
			(unsigned int)m_resolution.x,
			(unsigned int)m_resolution.y,
			backBufferFormat,
			Swap_Flip_Discard,
			SwapChain_Allow_Tearing | SwapChain_Allow_Mode_Switch,
			2
		);
		
		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_World_Submit, EVENT_HANDLER_VARIANT(Renderables_Acquire));
	}

	Renderer::~Renderer()
	{
		// Unsubscribe from events
		UNSUBSCRIBE_FROM_EVENT(Event_World_Submit, EVENT_HANDLER_VARIANT(Renderables_Acquire));

		m_entities.clear();
		m_camera = nullptr;
	}

	bool Renderer::Initialize()
	{
		// Create/Get required systems		
		g_resourceCache	= m_context->GetSubsystem<ResourceCache>().get();
		m_profiler		= m_context->GetSubsystem<Profiler>().get();

		// Editor specific
		m_grid				= make_unique<Grid>(m_rhiDevice);
		m_transformGizmo	= make_unique<Transform_Gizmo>(m_context);

		// Create a constant buffer that will be used for most shaders
		m_bufferGlobal = make_shared<RHI_ConstantBuffer>(m_rhiDevice, static_cast<unsigned int>(sizeof(ConstantBuffer_Global)));

		// Line buffer
		m_vertexBufferLines = make_shared<RHI_VertexBuffer>(m_rhiDevice);
	
		CreateDepthStencilStates();
		CreateRasterizerStates();
		CreateBlendStates();
		CreateRenderTextures();
		CreateFonts();
		CreateShaders();
		CreateSamplers();
		CreateTextures();
		SetDefault_Pipeline_State();

		return true;
	}

	void Renderer::CreateDepthStencilStates()
	{
		m_depthStencil_enabled	= make_shared<RHI_DepthStencilState>(m_rhiDevice, true);
		m_depthStencil_disabled	= make_shared<RHI_DepthStencilState>(m_rhiDevice, false);
	}

	void Renderer::CreateRasterizerStates()
	{
		m_rasterizer_cullBack_solid			= make_shared<RHI_RasterizerState>(m_rhiDevice, Cull_Back,	Fill_Solid,		true, false, false, false);
		m_rasterizer_cullFront_solid		= make_shared<RHI_RasterizerState>(m_rhiDevice, Cull_Front, Fill_Solid,		true, false, false, false);
		m_rasterizer_cullNone_solid			= make_shared<RHI_RasterizerState>(m_rhiDevice, Cull_None,	Fill_Solid,		true, false, false, false);
		m_rasterizer_cullBack_wireframe		= make_shared<RHI_RasterizerState>(m_rhiDevice, Cull_Back,	Fill_Wireframe, true, false, false, true);
		m_rasterizer_cullFront_wireframe	= make_shared<RHI_RasterizerState>(m_rhiDevice, Cull_Front, Fill_Wireframe, true, false, false, true);
		m_rasterizer_cullNone_wireframe		= make_shared<RHI_RasterizerState>(m_rhiDevice, Cull_None,	Fill_Wireframe,	true, false, false, true);
	}

	void Renderer::CreateBlendStates()
	{
		m_blend_enabled		= make_shared<RHI_BlendState>(m_rhiDevice, true);
		m_blend_disabled	= make_shared<RHI_BlendState>(m_rhiDevice, false);
	}

	void Renderer::CreateFonts()
	{
		// Get standard font directory
		string fontDir = g_resourceCache->GetStandardResourceDirectory(Resource_Font);

		// Load a font (used for performance metrics)
		m_font = make_unique<Font>(m_context, fontDir + "CalibriBold.ttf", 12, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	}

	void Renderer::CreateTextures()
	{
		// Get standard texture directory
		string textureDirectory = g_resourceCache->GetStandardResourceDirectory(Resource_Texture);

		// Noise texture (used by SSAO shader)
		m_texNoiseNormal = make_shared<RHI_Texture>(m_context);
		m_texNoiseNormal->LoadFromFile(textureDirectory + "noise.jpg");

		m_texWhite = make_shared<RHI_Texture>(m_context);
		m_texWhite->SetNeedsMipChain(false);
		m_texWhite->LoadFromFile(textureDirectory + "white.png");

		m_texBlack = make_shared<RHI_Texture>(m_context);
		m_texBlack->SetNeedsMipChain(false);
		m_texBlack->LoadFromFile(textureDirectory + "black.png");

		m_texLUT_IBL = make_shared<RHI_Texture>(m_context);
		m_texLUT_IBL->SetNeedsMipChain(false);
		m_texLUT_IBL->LoadFromFile(textureDirectory + "ibl_brdf_lut.png");

		// Gizmo icons
		m_gizmoTexLightDirectional = make_shared<RHI_Texture>(m_context);
		m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");

		m_gizmoTexLightPoint = make_shared<RHI_Texture>(m_context);
		m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");

		m_gizmoTexLightSpot = make_shared<RHI_Texture>(m_context);
		m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
	}

	void Renderer::CreateRenderTextures()
	{
		auto width	= (unsigned int)m_resolution.x;
		auto height	= (unsigned int)m_resolution.y;

		if ((width / 4) == 0 || (height / 4) == 0)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Resize everything
		m_gbuffer = make_unique<GBuffer>(m_rhiDevice, width, height);
		m_quad = Rectangle(0, 0, m_resolution.x, m_resolution.y);
		m_quad.CreateBuffers(this);

		// Full res
		m_renderTexFull_HDR_Light	= make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Format_R32G32B32A32_FLOAT);
		m_renderTexFull_HDR_Light2	= make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Format_R32G32B32A32_FLOAT);
		m_renderTexFull_TAA_Current = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Format_R16G16B16A16_FLOAT);
		m_renderTexFull_TAA_History = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Format_R16G16B16A16_FLOAT);

		// Half res
		m_renderTexHalf_Shadows = make_unique<RHI_RenderTexture>(m_rhiDevice, width / 2, height / 2, Format_R8_UNORM);
		m_renderTexHalf_SSAO	= make_unique<RHI_RenderTexture>(m_rhiDevice, width / 2, height / 2, Format_R8_UNORM);
		m_renderTexHalf_Spare	= make_unique<RHI_RenderTexture>(m_rhiDevice, width / 2, height / 2, Format_R8_UNORM);

		// Quarter res
		m_renderTexQuarter_Blur1 = make_unique<RHI_RenderTexture>(m_rhiDevice, width / 4, height / 4, Format_R16G16B16A16_FLOAT);
		m_renderTexQuarter_Blur2 = make_unique<RHI_RenderTexture>(m_rhiDevice, width / 4, height / 4, Format_R16G16B16A16_FLOAT);
	}

	void Renderer::CreateShaders()
	{
		// Get standard shader directory
		string shaderDirectory = g_resourceCache->GetStandardResourceDirectory(Resource_Shader);

		// G-Buffer
		m_vs_gbuffer = make_shared<RHI_Shader>(m_rhiDevice);
		m_vs_gbuffer->CompileVertex(shaderDirectory + "GBuffer.hlsl", Input_PositionTextureNormalTangent);

		// Light
		m_vps_light = make_shared<LightShader>(m_rhiDevice);
		m_vps_light->CompileVertexPixel(shaderDirectory + "Light.hlsl", Input_PositionTexture);

		// Transparent
		m_vps_transparent = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_transparent->CompileVertexPixel(shaderDirectory + "Transparent.hlsl", Input_PositionTextureNormalTangent);
		m_vps_transparent->AddBuffer<Struct_Transparency>();

		// Depth
		m_vps_depth = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_depth->CompileVertexPixel(shaderDirectory + "ShadowingDepth.hlsl", Input_Position3D);

		// Font
		m_vps_font = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_font->CompileVertexPixel(shaderDirectory + "Font.hlsl", Input_PositionTexture);
		m_vps_font->AddBuffer<Struct_Matrix_Vector4>();

		// Transform gizmo
		m_vps_gizmoTransform = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_gizmoTransform->CompileVertexPixel(shaderDirectory + "TransformGizmo.hlsl", Input_PositionTextureNormalTangent);
		m_vps_gizmoTransform->AddBuffer<Struct_Matrix_Vector3>();

		// SSAO
		m_vps_ssao = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_ssao->CompileVertexPixel(shaderDirectory + "SSAO.hlsl", Input_PositionTexture);
		m_vps_ssao->AddBuffer<Struct_Matrix_Matrix>();

		// Shadow mapping
		m_vps_shadowMapping = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_shadowMapping->CompileVertexPixel(shaderDirectory + "ShadowMapping.hlsl", Input_PositionTexture);
		m_vps_shadowMapping->AddBuffer<Struct_ShadowMapping>();

		// Color
		m_vps_color = make_shared<RHI_Shader>(m_rhiDevice);
		m_vps_color->CompileVertexPixel(shaderDirectory + "Color.hlsl", Input_PositionColor);
		m_vps_color->AddBuffer<Struct_Matrix_Matrix>();

		// Quad
		m_vs_quad = make_shared<RHI_Shader>(m_rhiDevice);
		m_vs_quad->CompileVertex(shaderDirectory + "Quad.hlsl", Input_PositionTexture);

		// Texture
		m_ps_texture = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_texture->AddDefine("PASS_TEXTURE");
		m_ps_texture->CompilePixel(shaderDirectory + "Quad.hlsl");

		// FXAA
		m_ps_fxaa = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_fxaa->AddDefine("PASS_FXAA");
		m_ps_fxaa->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Luma
		m_ps_luma = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_luma->AddDefine("PASS_LUMA");
		m_ps_luma->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Sharpening
		m_ps_sharpening = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_sharpening->AddDefine("PASS_SHARPENING");
		m_ps_sharpening->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Chromatic aberration
		m_ps_chromaticAberration = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_chromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
		m_ps_chromaticAberration->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Blur Box
		m_ps_blurBox = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_blurBox->AddDefine("PASS_BLUR_BOX");
		m_ps_blurBox->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Blur Gaussian Horizontal
		m_ps_blurGaussian = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_blurGaussian->AddDefine("PASS_BLUR_GAUSSIAN");
		m_ps_blurGaussian->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Blur Bilateral Gaussian Horizontal
		m_ps_blurGaussianBilateral = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_blurGaussianBilateral->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
		m_ps_blurGaussianBilateral->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Bloom - bright
		m_ps_bloomBright = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_bloomBright->AddDefine("PASS_BRIGHT");
		m_ps_bloomBright->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Bloom - blend
		m_ps_bloomBlend = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_bloomBlend->AddDefine("PASS_BLEND_ADDITIVE");
		m_ps_bloomBlend->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Tone-mapping
		m_ps_toneMapping = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_toneMapping->AddDefine("PASS_TONEMAPPING");
		m_ps_toneMapping->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Gamma correction
		m_ps_gammaCorrection = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_gammaCorrection->AddDefine("PASS_GAMMA_CORRECTION");
		m_ps_gammaCorrection->CompilePixel(shaderDirectory + "Quad.hlsl");

		// TAA
		m_ps_taa = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_taa->AddDefine("PASS_TAA_RESOLVE");
		m_ps_taa->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Motion Blur
		m_ps_motionBlur = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_motionBlur->AddDefine("PASS_MOTION_BLUR");
		m_ps_motionBlur->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Dithering
		m_ps_dithering = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_dithering->AddDefine("PASS_DITHERING");
		m_ps_dithering->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Debug Normal
		m_ps_debugNormal = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_debugNormal->AddDefine("DEBUG_NORMAL");
		m_ps_debugNormal->CompilePixel(shaderDirectory + "Debug.hlsl");

		// Debug velocity
		m_ps_debugVelocity = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_debugVelocity->AddDefine("DEBUG_VELOCITY");
		m_ps_debugVelocity->CompilePixel(shaderDirectory + "Debug.hlsl");

		// Debug depth
		m_ps_debugDepth = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_debugDepth->AddDefine("DEBUG_DEPTH");
		m_ps_debugDepth->CompilePixel(shaderDirectory + "Debug.hlsl");

		// Debug ssao
		m_ps_debugSSAO = make_shared<RHI_Shader>(m_rhiDevice);
		m_ps_debugSSAO->AddDefine("DEBUG_SSAO");
		m_ps_debugSSAO->CompilePixel(shaderDirectory + "Debug.hlsl");
	}

	void Renderer::CreateSamplers()
	{
		m_samplerCompareDepth		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Filter_Comparison_Bilinear,	Texture_Address_Clamp,	Comparison_Greater);
		m_samplerPointClamp			= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Filter_Point,				Texture_Address_Clamp,	Comparison_Always);
		m_samplerBilinearClamp		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Filter_Bilinear,			Texture_Address_Clamp,	Comparison_Always);
		m_samplerBilinearWrap		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Filter_Bilinear,			Texture_Address_Wrap,	Comparison_Always);
		m_samplerTrilinearClamp		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Filter_Trilinear,			Texture_Address_Clamp,	Comparison_Always);
		m_samplerAnisotropicWrap	= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Filter_Anisotropic,			Texture_Address_Wrap,	Comparison_Always);
	}

	void Renderer::SetDefault_Pipeline_State()
	{
		m_rhiPipeline->Clear();
		m_rhiPipeline->SetViewport(m_viewport);
		m_rhiPipeline->SetDepthStencilState(m_depthStencil_disabled);
		m_rhiPipeline->SetRasterizerState(m_rasterizer_cullBack_solid);
		m_rhiPipeline->SetBlendState(m_blend_disabled);
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->Bind();
	}

	void* Renderer::GetFrameShaderResource()
	{
		return m_renderTexFull_HDR_Light2 ? m_renderTexFull_HDR_Light2->GetShaderResource() : nullptr;
	}

	bool Renderer::SwapChain_Present()
	{
		if (!m_swapChain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_swapChain->Present(Present_Off);
		return true;
	}

	bool Renderer::SwapChain_SetAsRenderTarget()
	{
		if (!m_swapChain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_swapChain->SetAsRenderTarget();		
		return true;
	}

	bool Renderer::SwapChain_Clear(const Math::Vector4& color)
	{
		if (!m_swapChain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_swapChain->Clear(color);
		return true;
	}

	bool Renderer::SwapChain_Resize(unsigned int width, unsigned int height)
	{
		if (!m_swapChain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Return if resolution is invalid
		if (width == 0 || width > m_maxResolution || height == 0 || height > m_maxResolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return false;
		}

		return m_swapChain->Resize(width, height);
	}

	void Renderer::Tick()
	{
		if (!m_rhiDevice || !m_rhiDevice->IsInitialized())
			return;

		// If there is no camera, do nothing
		if (!m_camera)
		{
			SwapChain_Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_entities.empty())
		{
			SwapChain_Clear(m_camera->GetClearColor());
			SwapChain_Present();
			m_isRendering = false;
			return;
		}

		TIME_BLOCK_START_MULTI(m_profiler);
		m_profiler->Reset();
		m_isRendering = true;
		m_frameNum++;
		m_isOddFrame = (m_frameNum % 2) == 1;

		// Get camera matrices
		{
			m_nearPlane		= m_camera->GetNearPlane();
			m_farPlane		= m_camera->GetFarPlane();
			m_view			= m_camera->GetViewMatrix();
			m_viewBase		= m_camera->GetBaseViewMatrix();
			m_projection	= m_camera->GetProjectionMatrix();

			// TAA - Generate jitter
			if (Flags_IsSet(Render_PostProcess_TAA))
			{
				m_taa_jitterPrevious = m_taa_jitter;

				// Halton(2, 3) * 16 seems to work nice
				uint64_t samples	= 16;
				uint64_t index		= m_frameNum % samples;
				m_taa_jitter		= Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f;
				m_taa_jitter.x		= m_taa_jitter.x / m_resolution.x;
				m_taa_jitter.y		= m_taa_jitter.y / m_resolution.y;
				m_projection		*= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
			}
			else
			{
				m_taa_jitter			= Vector2::Zero;
				m_taa_jitterPrevious	= Vector2::Zero;		
			}

			m_viewProjection				= m_view * m_projection;
			m_projectionOrthographic		= Matrix::CreateOrthographicLH(m_resolution.x, m_resolution.y, m_nearPlane, m_farPlane);
			m_viewProjection_Orthographic	= m_viewBase * m_projectionOrthographic;
		}

		Pass_DepthDirectionalLight(GetLightDirectional());
		
		Pass_GBuffer();

		Pass_PreLight(
			m_renderTexHalf_Spare,		// IN:	
			m_renderTexHalf_Shadows,	// OUT: Shadows
			m_renderTexHalf_SSAO		// OUT: DO
		);

		Pass_Light(
			m_renderTexHalf_Shadows,	// IN:	Shadows
			m_renderTexHalf_SSAO,		// IN:	SSAO
			m_renderTexFull_HDR_Light	// Out: Result
		);

		Pass_Transparent(m_renderTexFull_HDR_Light);

		Pass_PostLight(
			m_renderTexFull_HDR_Light,	// IN:	Light pass result
			m_renderTexFull_HDR_Light2	// OUT: Result
		);
	
		Pass_Lines(m_renderTexFull_HDR_Light2);
		Pass_Gizmos(m_renderTexFull_HDR_Light2);
		Pass_DebugBuffer(m_renderTexFull_HDR_Light2);	
		Pass_PerformanceMetrics(m_renderTexFull_HDR_Light2);

		m_isRendering = false;
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::SetResolution(unsigned int width, unsigned int height)
	{
		// Return if resolution is invalid
		if (width == 0 || width > m_maxResolution || height == 0 || height > m_maxResolution)
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
		m_resolution.x = (float)width;
		m_resolution.y = (float)height;

		// Re-create render textures
		CreateRenderTextures();

		// Log
		LOGF_INFO("Resolution set to %dx%d", width, height);
	}

	void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Vector4& color_from, const Vector4& color_to, bool depth /*= true*/)
	{
		if (depth)
		{
			m_linesList_depthEnabled.emplace_back(from, color_from);
			m_linesList_depthEnabled.emplace_back(to, color_to);
		}
		else
		{
			m_linesList_depthDisabled.emplace_back(from, color_from);
			m_linesList_depthDisabled.emplace_back(to, color_to);
		}
	}

	void Renderer::DrawBox(const BoundingBox& box, const Vector4& color, bool depth /*= true*/)
	{
		Vector3 min = box.GetMin();
		Vector3 max = box.GetMax();
	
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

	void Renderer::SetDefault_Buffer(unsigned int resolutionWidth, unsigned int resolutionHeight, const Matrix& mMVP, float blur_sigma, const Math::Vector2& blur_direction)
	{
		auto buffer = (ConstantBuffer_Global*)m_bufferGlobal->Map();

		buffer->mMVP					= mMVP;
		buffer->mView					= m_view;
		buffer->mProjection				= m_projection;
		buffer->mProjectionOrtho		= m_projectionOrthographic;
		buffer->mViewProjection			= m_viewProjection;
		buffer->mViewProjectionOrtho	= m_viewProjection_Orthographic;
		buffer->camera_position			= m_camera->GetTransform()->GetPosition();
		buffer->camera_near				= m_camera->GetNearPlane();
		buffer->camera_far				= m_camera->GetFarPlane();
		buffer->resolution				= Vector2((float)resolutionWidth, (float)resolutionHeight);
		buffer->fxaa_subPixel			= m_fxaaSubPixel;
		buffer->fxaa_edgeThreshold		= m_fxaaEdgeThreshold;
		buffer->fxaa_edgeThresholdMin	= m_fxaaEdgeThresholdMin;
		buffer->blur_direction			= blur_direction;
		buffer->blur_sigma				= blur_sigma;
		buffer->bloom_intensity			= m_bloomIntensity;
		buffer->sharpen_strength		= m_sharpenStrength;
		buffer->sharpen_clamp			= m_sharpenClamp;
		buffer->taa_jitterOffset		= m_taa_jitter - m_taa_jitterPrevious;
		buffer->motionBlur_strength		= m_motionBlurStrength;
		buffer->fps_current				= m_profiler->GetFPS();
		buffer->fps_target				= Settings::Get().FPS_GetTarget();
		buffer->gamma					= m_gamma;
		buffer->tonemapping				= (float)m_tonemapping;

		m_bufferGlobal->Unmap();
		m_rhiPipeline->SetConstantBuffer(m_bufferGlobal, 0, Buffer_Global);
	}

	void Renderer::Renderables_Acquire(const Variant& entitiesVariant)
	{
		TIME_BLOCK_START_CPU(m_profiler);

		// Clear previous state
		m_entities.clear();
		m_camera = nullptr;
		
		auto entitiesVec = entitiesVariant.Get<vector<shared_ptr<Entity>>>();
		for (const auto& entitieshared : entitiesVec)
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
				bool isTransparent = !renderable->Material_Exists() ? false : renderable->Material_Ptr()->GetColorAlbedo().w < 1.0f;
				if (!skybox) // Ignore skybox
				{
					m_entities[isTransparent ? Renderable_ObjectTransparent : Renderable_ObjectOpaque].emplace_back(entity);
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

		Renderables_Sort(&m_entities[Renderable_ObjectOpaque]);
		Renderables_Sort(&m_entities[Renderable_ObjectTransparent]);

		TIME_BLOCK_END_CPU(m_profiler);
	}

	void Renderer::Renderables_Sort(vector<Entity*>* renderables)
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
				auto a_material = a_renderable->Material_Ptr();
				auto b_material = b_renderable->Material_Ptr();
				if (!a_material || !b_material)
					return false;

				float a_depth = (a_renderable->Geometry_AABB().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();
				float b_depth = (b_renderable->Geometry_AABB().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();

				return a_depth < b_depth;
			});
		}

		// Sort by material
		sort(renderables->begin(), renderables->end(), [](Entity* a, Entity* b)
		{
			// Get renderable component
			auto a_renderable = a->GetRenderable_PtrRaw();
			auto b_renderable = b->GetRenderable_PtrRaw();
			if (!a_renderable || !b_renderable)
				return false;

			// Get materials
			auto a_material = a_renderable->Material_Ptr();
			auto b_material = b_renderable->Material_Ptr();
			if (!a_material || !b_material)
				return false;

			// Order doesn't matter, as long as they are not mixed
			return a_material->Resource_GetID() < b_material->Resource_GetID();
		});
	}

	shared_ptr<RHI_RasterizerState>& Renderer::GetRasterizerState(RHI_Cull_Mode cullMode, RHI_Fill_Mode fillMode)
	{
		if		(cullMode == Cull_Back)		return (fillMode == Fill_Solid) ? m_rasterizer_cullBack_solid	: m_rasterizer_cullBack_wireframe;
		else if (cullMode == Cull_Front)	return (fillMode == Fill_Solid) ? m_rasterizer_cullFront_solid : m_rasterizer_cullFront_wireframe;
		else if (cullMode == Cull_None)		return (fillMode == Fill_Solid) ? m_rasterizer_cullNone_solid	: m_rasterizer_cullNone_wireframe;

		return m_rasterizer_cullBack_solid;
	}

	Light* Renderer::GetLightDirectional()
	{
		auto entities = m_entities[Renderable_Light];

		for (const auto& entity : entities)
		{
			Light* light = entity->GetComponent<Light>().get();
			if (light->GetLightType() == LightType_Directional)
				return light;
		}

		return nullptr;
	}
}
