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
#include "Rectangle.h"
#include "Gizmos/Grid.h"
#include "Gizmos/TransformGizmo.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "Utilities/Sampling.h"
#include "Font/Font.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommonBuffers.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_Pipeline.h"
#include "../RHI/RHI_RenderTexture.h"
#include "../RHI/RHI_Shader.h"
#include "../World/Actor.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Skybox.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Profiling/Profiler.h"
#include "../Core/Context.h"
#include "../Math/BoundingBox.h"
#include "../RHI/RHI_ConstantBuffer.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	static ResourceCache* g_resourceCache	= nullptr;
	bool Renderer::m_isRendering			= false;
	unsigned int Renderer::m_maxResolution	= 16384;

	Renderer::Renderer(Context* context, void* drawHandle) : Subsystem(context)
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
		m_flags			|= Render_PostProcess_ToneMapping;
		m_flags			|= Render_PostProcess_Bloom;	
		m_flags			|= Render_PostProcess_SSAO;	
		m_flags			|= Render_PostProcess_MotionBlur;
		m_flags			|= Render_PostProcess_TAA;
		m_flags			|= Render_PostProcess_Sharpening;
		//m_flags		|= Render_PostProcess_ChromaticAberration;
		//m_flags		|= Render_PostProcess_SSR;	// Disabled by default: Only plays nice if it has environmental probes for fallback
		//m_flags		|= Render_PostProcess_FXAA; // Disabled by default: TAA is superior
		

		// Create RHI device
		m_rhiDevice		= make_shared<RHI_Device>(drawHandle);
		m_rhiPipeline	= make_shared<RHI_Pipeline>(m_rhiDevice);

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_WORLD_SUBMIT, EVENT_HANDLER_VARIANT(Renderables_Acquire));
	}

	Renderer::~Renderer()
	{
		m_actors.clear();
		m_camera = nullptr;
	}

	bool Renderer::Initialize()
	{
		// Create/Get required systems		
		g_resourceCache	= m_context->GetSubsystem<ResourceCache>();
		m_viewport		= make_shared<RHI_Viewport>();

		// Editor specific
		m_grid				= make_unique<Grid>(m_rhiDevice);
		m_transformGizmo	= make_unique<TransformGizmo>(m_context);
		m_gizmoRectLight	= make_unique<Rectangle>(m_context);

		// Create a constant buffer that will be used for most shaders
		m_bufferGlobal = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_bufferGlobal->Create(sizeof(ConstantBuffer_Global));
	
		CreateRenderTextures(Settings::Get().Resolution_GetWidth(), Settings::Get().Resolution_GetHeight());
		CreateFonts();
		CreateShaders();
		CreateSamplers();
		CreateTextures();

		// PIPELINE STATES - EXPERIMENTAL
		m_pipelineLine.primitiveTopology	= PrimitiveTopology_LineList;
		m_pipelineLine.cullMode				= Cull_Back;
		m_pipelineLine.fillMode				= Fill_Solid;
		m_pipelineLine.vertexShader			= m_shaderLine;
		m_pipelineLine.pixelShader			= m_shaderLine;
		m_pipelineLine.constantBuffer		= m_shaderLine->GetConstantBuffer();
		m_pipelineLine.sampler				= m_samplerPointClamp;

		return true;
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

		m_tex_lutIBL = make_shared<RHI_Texture>(m_context);
		m_tex_lutIBL->SetNeedsMipChain(false);
		m_tex_lutIBL->LoadFromFile(textureDirectory + "ibl_brdf_lut.png");

		// Gizmo icons
		m_gizmoTexLightDirectional = make_shared<RHI_Texture>(m_context);
		m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");

		m_gizmoTexLightPoint = make_shared<RHI_Texture>(m_context);
		m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");

		m_gizmoTexLightSpot = make_shared<RHI_Texture>(m_context);
		m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
	}

	void Renderer::CreateRenderTextures(unsigned int width, unsigned int height)
	{
		if ((width / 4) == 0 || (height / 4) == 0)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Resize everything
		m_gbuffer	= make_unique<GBuffer>(m_rhiDevice, width, height);
		m_quad		= make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)width, (float)height);

		// Full res
		m_renderTexFull_HDR_Light	= make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R32G32B32A32_FLOAT);
		m_renderTexFull_HDR_Light2	= make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R32G32B32A32_FLOAT);
		m_renderTexFull_TAA_Current = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R16G16B16A16_FLOAT);
		m_renderTexFull_TAA_History = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R16G16B16A16_FLOAT);

		// Half res
		m_renderTexHalf_Shadows = make_unique<RHI_RenderTexture>(m_rhiDevice, width / 2, height / 2, Texture_Format_R8_UNORM);
		m_renderTexHalf_SSAO	= make_unique<RHI_RenderTexture>(m_rhiDevice, width / 2, height / 2, Texture_Format_R8_UNORM);
		m_renderTexHalf_Spare	= make_unique<RHI_RenderTexture>(m_rhiDevice, width / 2, height / 2, Texture_Format_R8_UNORM);

		// Quarter res
		m_renderTexQuarter_Blur1 = make_unique<RHI_RenderTexture>(m_rhiDevice, width / 4, height / 4, Texture_Format_R16G16B16A16_FLOAT);
		m_renderTexQuarter_Blur2 = make_unique<RHI_RenderTexture>(m_rhiDevice, width / 4, height / 4, Texture_Format_R16G16B16A16_FLOAT);
	}

	void Renderer::CreateShaders()
	{
		// Get standard shader directory
		string shaderDirectory = g_resourceCache->GetStandardResourceDirectory(Resource_Shader);

		// G-Buffer
		m_shaderGBuffer = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderGBuffer->CompileVertex(shaderDirectory + "GBuffer.hlsl", Input_PositionTextureNormalTangent);

		// Light
		m_shaderLight = make_shared<LightShader>(m_rhiDevice);
		m_shaderLight->CompileVertexPixel(shaderDirectory + "Light.hlsl", Input_PositionTexture);

		// Transparent
		m_shaderTransparent = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderTransparent->CompileVertexPixel(shaderDirectory + "Transparent.hlsl", Input_PositionTextureNormalTangent);
		m_shaderTransparent->AddBuffer<Struct_Transparency>();

		// Depth
		m_shaderLightDepth = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderLightDepth->CompileVertexPixel(shaderDirectory + "ShadowingDepth.hlsl", Input_Position);

		// Font
		m_shaderFont = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderFont->CompileVertexPixel(shaderDirectory + "Font.hlsl", Input_PositionTexture);
		m_shaderFont->AddBuffer<Struct_Matrix_Vector4>();

		// Transform gizmo
		m_shaderTransformGizmo = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderTransformGizmo->CompileVertexPixel(shaderDirectory + "TransformGizmo.hlsl", Input_PositionTextureNormalTangent);
		m_shaderTransformGizmo->AddBuffer<Struct_Matrix_Vector3>();

		// SSAO
		m_shaderSSAO = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderSSAO->CompileVertexPixel(shaderDirectory + "SSAO.hlsl", Input_PositionTexture);
		m_shaderSSAO->AddBuffer<Struct_Matrix_Matrix>();

		// Shadow mapping
		m_shaderShadowMapping = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderShadowMapping->CompileVertexPixel(shaderDirectory + "ShadowMapping.hlsl", Input_PositionTexture);
		m_shaderShadowMapping->AddBuffer<Struct_ShadowMapping>();

		// Line
		m_shaderLine = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderLine->CompileVertexPixel(shaderDirectory + "Line.hlsl", Input_PositionColor);
		m_shaderLine->AddBuffer<Struct_Matrix_Matrix>();

		// Quad
		m_shaderQuad = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad->CompileVertex(shaderDirectory + "Quad.hlsl", Input_PositionTexture);

		// Texture
		m_shaderQuad_texture = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_texture->AddDefine("PASS_TEXTURE");
		m_shaderQuad_texture->CompilePixel(shaderDirectory + "Quad.hlsl");

		// FXAA
		m_shaderQuad_fxaa = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_fxaa->AddDefine("PASS_FXAA");
		m_shaderQuad_fxaa->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Luma
		m_shaderQuad_luma = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_luma->AddDefine("PASS_LUMA");
		m_shaderQuad_luma->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Sharpening
		m_shaderQuad_sharpening = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_sharpening->AddDefine("PASS_SHARPENING");
		m_shaderQuad_sharpening->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Chromatic aberration
		m_shaderQuad_chromaticAberration = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_chromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
		m_shaderQuad_chromaticAberration->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Blur Box
		m_shaderQuad_blur_box = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_blur_box->AddDefine("PASS_BLUR_BOX");
		m_shaderQuad_blur_box->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Blur Gaussian Horizontal
		m_shaderQuad_blur_gaussian = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_blur_gaussian->AddDefine("PASS_BLUR_GAUSSIAN");
		m_shaderQuad_blur_gaussian->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Blur Bilateral Gaussian Horizontal
		m_shaderQuad_blur_gaussianBilateral = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_blur_gaussianBilateral->AddDefine("PASS_BLUR_BILATERAL_GAUSSIAN");
		m_shaderQuad_blur_gaussianBilateral->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Bloom - bright
		m_shaderQuad_bloomBright = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_bloomBright->AddDefine("PASS_BRIGHT");
		m_shaderQuad_bloomBright->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Bloom - blend
		m_shaderQuad_bloomBLend = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_bloomBLend->AddDefine("PASS_BLEND_ADDITIVE");
		m_shaderQuad_bloomBLend->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Tone-mapping
		m_shaderQuad_toneMapping = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_toneMapping->AddDefine("PASS_TONEMAPPING");
		m_shaderQuad_toneMapping->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Gamma correction
		m_shaderQuad_gammaCorrection = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_gammaCorrection->AddDefine("PASS_GAMMA_CORRECTION");
		m_shaderQuad_gammaCorrection->CompilePixel(shaderDirectory + "Quad.hlsl");

		// TAA
		m_shaderQuad_taa = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_taa->AddDefine("PASS_TAA_RESOLVE");
		m_shaderQuad_taa->CompilePixel(shaderDirectory + "Quad.hlsl");

		// Motion Blur
		m_shaderQuad_motionBlur = make_shared<RHI_Shader>(m_rhiDevice);
		m_shaderQuad_motionBlur->AddDefine("PASS_MOTION_BLUR");
		m_shaderQuad_motionBlur->CompilePixel(shaderDirectory + "Quad.hlsl");
	}

	void Renderer::CreateSamplers()
	{
		m_samplerCompareDepth		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Comparison_Bilinear,	Texture_Address_Clamp,	Texture_Comparison_Greater);
		m_samplerPointClamp			= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Point,					Texture_Address_Clamp,	Texture_Comparison_Always);
		m_samplerBilinearClamp		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Bilinear,				Texture_Address_Clamp,	Texture_Comparison_Always);
		m_samplerBilinearWrap		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Bilinear,				Texture_Address_Wrap,	Texture_Comparison_Always);
		m_samplerTrilinearClamp		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Trilinear,				Texture_Address_Clamp,	Texture_Comparison_Always);
		m_samplerAnisotropicWrap	= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Anisotropic,			Texture_Address_Wrap,	Texture_Comparison_Always);
	}

	void Renderer::SetBackBufferAsRenderTarget(bool clear /*= true*/)
	{
		m_rhiDevice->Set_BackBufferAsRenderTarget();
		m_viewport->SetWidth((float)Settings::Get().Resolution_GetWidth());
		m_viewport->SetHeight((float)Settings::Get().Resolution_GetHeight());
		m_rhiPipeline->SetViewport(m_viewport);
		if (clear) m_rhiDevice->ClearBackBuffer(m_camera ? m_camera->GetClearColor() : Vector4(0, 0, 0, 1));
	}

	void* Renderer::GetFrameShaderResource()
	{
		return m_renderTexFull_HDR_Light2 ? m_renderTexFull_HDR_Light2->GetShaderResource() : nullptr;
	}

	void Renderer::Present()
	{
		m_rhiDevice->Present();
	}

	void Renderer::Render()
	{
		if (!m_rhiDevice || !m_rhiDevice->IsInitialized())
			return;

		// If there is no camera, do nothing
		if (!m_camera)
		{
			m_rhiDevice->ClearBackBuffer(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_actors.empty())
		{
			m_rhiDevice->ClearBackBuffer(m_camera->GetClearColor());
			m_rhiDevice->Present();
			m_isRendering = false;
			return;
		}

		TIME_BLOCK_START_MULTI();
		Profiler::Get().Reset();
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
				m_taa_jitter.x		= m_taa_jitter.x / (float)Settings::Get().Resolution_GetWidth();
				m_taa_jitter.y		= m_taa_jitter.y / (float)Settings::Get().Resolution_GetHeight();
				m_projection		*= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
			}
			else
			{
				m_taa_jitter			= Vector2::Zero;
				m_taa_jitterPrevious	= Vector2::Zero;		
			}

			m_viewProjection				= m_view * m_projection;
			m_projectionOrthographic		= Matrix::CreateOrthographicLH((float)Settings::Get().Resolution_GetWidth(), (float)Settings::Get().Resolution_GetHeight(), m_nearPlane, m_farPlane);		
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
		
		Pass_Lines(m_renderTexFull_HDR_Light);

		Pass_PostLight(
			m_renderTexFull_HDR_Light,	// IN:	Light pass result
			m_renderTexFull_HDR_Light2	// OUT: Result
		);
	
		Pass_GBufferVisualize(m_renderTexFull_HDR_Light2);
		Pass_Gizmos(m_renderTexFull_HDR_Light2);
		Pass_PerformanceMetrics(m_renderTexFull_HDR_Light2);

		m_isRendering = false;
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::SetBackBufferSize(unsigned int width, unsigned int height)
	{
		// Return if resolution is invalid
		if (width == 0 || width > m_maxResolution || height == 0 || height > m_maxResolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		m_rhiDevice->Set_Resolution(width, height);
		m_viewport->SetWidth((float)width);
		m_viewport->SetHeight((float)height);
		m_rhiPipeline->SetViewport(m_viewport);
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
		if (Settings::Get().Resolution_Get().x == width && Settings::Get().Resolution_Get().y == height)
			return;

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

		Settings::Get().Resolution_Set(Vector2((float)width, (float)height));
		CreateRenderTextures(width, height);
		LOGF_INFO("Resolution set to %dx%d", width, height);
	}

	void Renderer::AddBoundigBox(const BoundingBox& box, const Vector4& color)
	{
		// Compute points from min and max
		Vector3 boundPoint1 = box.GetMin();
		Vector3 boundPoint2 = box.GetMax();
		Vector3 boundPoint3 = Vector3(boundPoint1.x, boundPoint1.y, boundPoint2.z);
		Vector3 boundPoint4 = Vector3(boundPoint1.x, boundPoint2.y, boundPoint1.z);
		Vector3 boundPoint5 = Vector3(boundPoint2.x, boundPoint1.y, boundPoint1.z);
		Vector3 boundPoint6 = Vector3(boundPoint1.x, boundPoint2.y, boundPoint2.z);
		Vector3 boundPoint7 = Vector3(boundPoint2.x, boundPoint1.y, boundPoint2.z);
		Vector3 boundPoint8 = Vector3(boundPoint2.x, boundPoint2.y, boundPoint1.z);

		// top of rectangular cuboid (6-2-8-4)
		AddLine(boundPoint6, boundPoint2, color);
		AddLine(boundPoint2, boundPoint8, color);
		AddLine(boundPoint8, boundPoint4, color);
		AddLine(boundPoint4, boundPoint6, color);

		// bottom of rectangular cuboid (3-7-5-1)
		AddLine(boundPoint3, boundPoint7, color);
		AddLine(boundPoint7, boundPoint5, color);
		AddLine(boundPoint5, boundPoint1, color);
		AddLine(boundPoint1, boundPoint3, color);

		// legs (6-3, 2-7, 8-5, 4-1)
		AddLine(boundPoint6, boundPoint3, color);
		AddLine(boundPoint2, boundPoint7, color);
		AddLine(boundPoint8, boundPoint5, color);
		AddLine(boundPoint4, boundPoint1, color);
	}

	void Renderer::AddLine(const Vector3& from, const Vector3& to, const Vector4& colorFrom, const Vector4& colorTo)
	{
		m_lineVertices.emplace_back(from, colorFrom);
		m_lineVertices.emplace_back(to, colorTo);
	}

	void Renderer::SetGlobalBuffer(const Matrix& mMVP, unsigned int resolutionWidth, unsigned int resolutionHeight, float blur_sigma, const Math::Vector2& blur_direction)
	{
		auto buffer = (ConstantBuffer_Global*)m_bufferGlobal->Map();

		buffer->mMVP					= mMVP;
		buffer->mView					= m_view;
		buffer->mProjection				= m_projection;
		buffer->mProjectionOrtho		= m_projectionOrthographic;
		buffer->mViewProjection			= m_viewProjection;
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
		buffer->fps_current				= Profiler::Get().GetFPS();
		buffer->fps_target				= Settings::Get().FPS_GetTarget();
		buffer->gamma					= m_gamma;

		m_bufferGlobal->Unmap();
		m_rhiPipeline->SetConstantBuffer(m_bufferGlobal, 0, Buffer_Global);
	}

	//= RENDERABLES ============================================================================================
	void Renderer::Renderables_Acquire(const Variant& actorsVariant)
	{
		TIME_BLOCK_START_CPU();

		// Clear previous state
		m_actors.clear();
		m_camera = nullptr;
		
		auto actorsVec = actorsVariant.Get<vector<shared_ptr<Actor>>>();
		for (const auto& actorShared : actorsVec)
		{
			auto actor = actorShared.get();
			if (!actor)
				continue;

			// Get all the components we are interested in
			auto renderable = actor->GetComponent<Renderable>();
			auto light		= actor->GetComponent<Light>();
			auto skybox		= actor->GetComponent<Skybox>();
			auto camera		= actor->GetComponent<Camera>();

			if (renderable)
			{
				bool isTransparent = !renderable->Material_Exists() ? false : renderable->Material_Ptr()->GetColorAlbedo().w < 1.0f;
				m_actors[isTransparent ? Renderable_ObjectTransparent : Renderable_ObjectOpaque].emplace_back(actor);
			}

			if (light)
			{
				m_actors[Renderable_Light].emplace_back(actor);
			}

			if (skybox)
			{
				m_skybox = skybox.get();
			}

			if (camera)
			{
				m_actors[Renderable_Camera].emplace_back(actor);
				m_camera = camera.get();
			}
		}

		Renderables_Sort(&m_actors[Renderable_ObjectOpaque]);
		Renderables_Sort(&m_actors[Renderable_ObjectTransparent]);

		TIME_BLOCK_END_CPU();
	}

	void Renderer::Renderables_Sort(vector<Actor*>* renderables)
	{
		if (renderables->size() <= 2)
			return;

		// Sort by depth (front to back)
		if (m_camera)
		{
			sort(renderables->begin(), renderables->end(), [this](Actor* a, Actor* b)
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
		sort(renderables->begin(), renderables->end(), [](Actor* a, Actor* b)
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
	//==========================================================================================================

	//= PASSES =================================================================================================
	void Renderer::Pass_DepthDirectionalLight(Light* light)
	{
		// Validate light
		if (!light || !light->GetCastShadows())
			return;

		// Validate light's shadow map
		auto& shadowMap = light->GetShadowMap();
		if (!shadowMap)
			return;

		// Validate actors
		auto& actors = m_actors[Renderable_ObjectOpaque];
		if (actors.empty())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_DepthDirectionalLight");

		// Set common states	
		m_rhiPipeline->SetShader(m_shaderLightDepth);
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetViewport(shadowMap->GetViewport());
		
		// Variables that help reduce state changes
		unsigned int currentlyBoundGeometry = 0;
		for (unsigned int i = 0; i < light->GetShadowMap()->GetArraySize(); i++)
		{
			m_rhiDevice->EventBegin(("Pass_DepthDirectionalLight " + to_string(i)).c_str());
			m_rhiPipeline->SetRenderTarget(shadowMap->GetRenderTargetView(i), shadowMap->GetDepthStencilView(), true);		

			for (const auto& actor : actors)
			{
				// Acquire renderable component
				auto renderable = actor->GetRenderable_PtrRaw();
				if (!renderable)
					continue;

				// Acquire material
				auto material = renderable->Material_Ptr();
				if (!material)
					continue;

				// Acquire geometry
				auto geometry = renderable->Geometry_Model();
				if (!geometry || !geometry->GetVertexBuffer() || !geometry->GetIndexBuffer())
					continue;

				// Skip meshes that don't cast shadows
				if (!renderable->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material->GetColorAlbedo().w < 1.0f)
					continue;

				// Bind geometry
				if (currentlyBoundGeometry != geometry->Resource_GetID())
				{
					m_rhiPipeline->SetIndexBuffer(geometry->GetIndexBuffer());
					m_rhiPipeline->SetVertexBuffer(geometry->GetVertexBuffer());
					currentlyBoundGeometry = geometry->Resource_GetID();
				}

				SetGlobalBuffer(actor->GetTransform_PtrRaw()->GetMatrix() * light->GetViewMatrix() * light->ShadowMap_GetProjectionMatrix(i));
				m_rhiPipeline->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
			}
			m_rhiDevice->EventEnd();
		}

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhiDevice)
			return;

		if (m_actors[Renderable_ObjectOpaque].empty())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_GBuffer");

		// Set common states
		m_gbuffer->SetAsRenderTarget(m_rhiPipeline);
		m_rhiPipeline->SetSampler(m_samplerAnisotropicWrap);
		m_rhiPipeline->SetFillMode(Fill_Solid);
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetVertexShader(m_shaderGBuffer);
		SetGlobalBuffer();

		// Variables that help reduce state changes
		unsigned int currentlyBoundGeometry = 0;
		unsigned int currentlyBoundShader	= 0;
		unsigned int currentlyBoundMaterial = 0;

		for (auto actor : m_actors[Renderable_ObjectOpaque])
		{
			// Get renderable and material
			Renderable* renderable	= actor->GetRenderable_PtrRaw();
			Material* material		= renderable ? renderable->Material_Ptr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get shader and geometry
			auto shader	= material->GetShader();
			auto model	= renderable->Geometry_Model();

			// Validate shader
			if (!shader || shader->GetState() != Shader_Built)
				continue;

			// Validate geometry
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// set face culling (changes only if required)
			m_rhiPipeline->SetCullMode(material->GetCullMode());

			// Bind geometry
			if (currentlyBoundGeometry != model->Resource_GetID())
			{	
				m_rhiPipeline->SetIndexBuffer(model->GetIndexBuffer());
				m_rhiPipeline->SetVertexBuffer(model->GetVertexBuffer());
				currentlyBoundGeometry = model->Resource_GetID();
			}

			// Bind shader
			if (currentlyBoundShader != shader->RHI_GetID())
			{
				m_rhiPipeline->SetPixelShader(shared_ptr<RHI_Shader>(shader));
				currentlyBoundShader = shader->RHI_GetID();
			}

			// Bind textures
			if (currentlyBoundMaterial != material->Resource_GetID())
			{
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Albedo).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Roughness).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Metallic).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Normal).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Height).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Occlusion).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Emission).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Mask).ptr);

				currentlyBoundMaterial = material->Resource_GetID();
			}

			// UPDATE PER OBJECT BUFFER
			shader->UpdatePerObjectBuffer(actor->GetTransform_PtrRaw(), material, m_view, m_projection);			
			m_rhiPipeline->SetConstantBuffer(shader->GetPerObjectBuffer(), 1, Buffer_Global);

			// Render	
			m_rhiPipeline->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
			Profiler::Get().m_rendererMeshesRendered++;

		} // Actor/MESH ITERATION

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_PreLight(shared_ptr<RHI_RenderTexture>& texIn_Spare, shared_ptr<RHI_RenderTexture>& texOut_Shadows, shared_ptr<RHI_RenderTexture>& texOut_SSAO)
	{
		m_rhiDevice->EventBegin("Pass_PreLight");

		m_rhiPipeline->SetIndexBuffer(m_quad->GetIndexBuffer());
		m_rhiPipeline->SetVertexBuffer(m_quad->GetVertexBuffer());
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);

		// Shadow mapping + Blur
		if (auto lightDir = GetLightDirectional())
		{
			if (lightDir->GetCastShadows())
			{
				Pass_ShadowMapping(texIn_Spare, GetLightDirectional());
				float sigma			= 1.0f;
				float pixelStride	= 1.0f;
				Pass_BlurBilateralGaussian(texIn_Spare, texOut_Shadows, sigma, pixelStride);
			}
			else
			{
				texOut_Shadows->Clear(1, 1, 1, 1);
			}
		}

		// SSAO + Blur
		if (m_flags & Render_PostProcess_SSAO)
		{
			Pass_SSAO(texIn_Spare);
			float sigma			= 2.0f;
			float pixelStride	= 2.0f;
			Pass_BlurBilateralGaussian(texIn_Spare, texOut_SSAO, sigma, pixelStride);
		}

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Light(shared_ptr<RHI_RenderTexture>& texShadows, shared_ptr<RHI_RenderTexture>& texSSAO, shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (m_shaderLight->GetState() != Shader_Built)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Light");

		// Update constant buffer
		m_shaderLight->UpdateConstantBuffer
		(
			m_viewProjection_Orthographic,
			m_view,
			m_projection,
			m_actors[Renderable_Light],
			Flags_IsSet(Render_PostProcess_SSR)
		);

		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetShader(shared_ptr<RHI_Shader>(m_shaderLight));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Albedo));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Material));
		m_rhiPipeline->SetTexture(texShadows);
		if (Flags_IsSet(Render_PostProcess_SSAO)) { m_rhiPipeline->SetTexture(texSSAO); } else { m_rhiPipeline->SetTexture(m_texWhite); }
		m_rhiPipeline->SetTexture(m_renderTexFull_HDR_Light2); // SSR
		m_rhiPipeline->SetTexture(m_skybox ? m_skybox->GetTexture() : m_texWhite);
		m_rhiPipeline->SetTexture(m_tex_lutIBL);
		m_rhiPipeline->SetSampler(m_samplerTrilinearClamp);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetConstantBuffer(m_shaderLight->GetConstantBuffer(), 1, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Transparent(shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (!GetLightDirectional())
			return;

		auto& actors_transparent = m_actors[Renderable_ObjectTransparent];
		if (actors_transparent.empty())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Transparent");

		m_rhiPipeline->SetAlphaBlending(true);
		m_rhiPipeline->SetShader(m_shaderTransparent);
		m_rhiPipeline->SetRenderTarget(texOut, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView());
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_skybox ? m_skybox->GetTexture() : nullptr);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);

		for (auto& actor : actors_transparent)
		{
			// Get renderable and material
			Renderable* renderable = actor->GetRenderable_PtrRaw();
			Material* material = renderable ? renderable->Material_Ptr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get geometry
			auto model = renderable->Geometry_Model();
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set the following per object
			m_rhiPipeline->SetCullMode(material->GetCullMode());
			m_rhiPipeline->SetIndexBuffer(model->GetIndexBuffer());
			m_rhiPipeline->SetVertexBuffer(model->GetVertexBuffer());

			// Constant buffer
			auto buffer = Struct_Transparency(
				actor->GetTransform_PtrRaw()->GetMatrix(),
				m_view,
				m_projection,
				material->GetColorAlbedo(),
				m_camera->GetTransform()->GetPosition(),
				GetLightDirectional()->GetDirection(),
				material->GetRoughnessMultiplier()
			);
			m_shaderTransparent->UpdateBuffer(&buffer);
			m_rhiPipeline->SetConstantBuffer(m_shaderTransparent->GetConstantBuffer(), 1, Buffer_Global);
			m_rhiPipeline->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());

			Profiler::Get().m_rendererMeshesRendered++;

		} // Actor/MESH ITERATION

		m_rhiPipeline->SetAlphaBlending(false);
		m_rhiPipeline->ClearPendingStates();

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_PostLight(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_PostLight");

		// All post-process passes share the following, so set them once here
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetVertexBuffer(m_quad->GetVertexBuffer());
		m_rhiPipeline->SetIndexBuffer(m_quad->GetIndexBuffer());
		m_rhiPipeline->SetVertexShader(m_shaderQuad);
		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight());

		// Render target swapping
		auto SwapTargets = [&texIn, &texOut]() { texOut.swap(texIn); };

		// TAA	
		if (Flags_IsSet(Render_PostProcess_TAA))
		{
			Pass_TAA(texIn, texOut);
			SwapTargets();
		}

		// Bloom
		if (Flags_IsSet(Render_PostProcess_Bloom))
		{
			Pass_Bloom(texIn, texOut);
			SwapTargets();
		}

		// Motion Blur
		if (Flags_IsSet(Render_PostProcess_MotionBlur))
		{
			Pass_MotionBlur(texIn, texOut);
			SwapTargets();
		}

		// Tone-Mapping
		if (Flags_IsSet(Render_PostProcess_ToneMapping))
		{
			Pass_ToneMapping(texIn, texOut);
			SwapTargets();
		}

		// FXAA
		if (Flags_IsSet(Render_PostProcess_FXAA))
		{
			Pass_FXAA(texIn, texOut);
			SwapTargets();
		}

		// Sharpening
		if (Flags_IsSet(Render_PostProcess_Sharpening))
		{
			Pass_Sharpening(texIn, texOut);
			SwapTargets();
		}

		// Chromatic aberration
		if (Flags_IsSet(Render_PostProcess_ChromaticAberration))
		{
			Pass_ChromaticAberration(texIn, texOut);
			SwapTargets();
		}

		// Gamma correction
		Pass_GammaCorrection(texIn, texOut);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_ShadowMapping(shared_ptr<RHI_RenderTexture>& texOut, Light* inDirectionalLight)
	{
		if (!inDirectionalLight)
			return;

		if (!inDirectionalLight->GetCastShadows())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Shadowing");

		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetShader(m_shaderShadowMapping);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(inDirectionalLight->GetShadowMap()); // Texture2DArray
		m_rhiPipeline->SetSampler(m_samplerCompareDepth);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		SetGlobalBuffer(m_viewProjection_Orthographic, texOut->GetWidth(), texOut->GetHeight());
		auto buffer = Struct_ShadowMapping((m_viewProjection).Inverted(), inDirectionalLight, m_camera);
		m_shaderShadowMapping->UpdateBuffer(&buffer);
		m_rhiPipeline->SetConstantBuffer(m_shaderShadowMapping->GetConstantBuffer(), 1, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_SSAO(shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_SSAO");

		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetShader(m_shaderSSAO);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_texNoiseNormal);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);	// SSAO (clamp)
		m_rhiPipeline->SetSampler(m_samplerBilinearWrap);	// SSAO noise texture (wrap)
		auto buffer = Struct_Matrix_Matrix
		(
			m_viewProjection_Orthographic,
			(m_viewProjection).Inverted()
		);
		m_shaderSSAO->UpdateBuffer(&buffer);
		m_rhiPipeline->SetConstantBuffer(m_shaderSSAO->GetConstantBuffer(), 1, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_BlurBox(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut, float sigma)
	{
		m_rhiDevice->EventBegin("Pass_Blur");

		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight(), sigma);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetVertexShader(m_shaderQuad);
		m_rhiPipeline->SetPixelShader(m_shaderQuad_blur_box);
		m_rhiPipeline->SetTexture(texIn); // Shadows are in the alpha channel
		m_rhiPipeline->SetSampler(m_samplerTrilinearClamp);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_BlurGaussian(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut, float sigma)
	{
		if (texIn->GetWidth() != texOut->GetWidth() ||
			texIn->GetHeight() != texOut->GetHeight() ||
			texIn->GetFormat() != texOut->GetFormat())
		{
			LOG_ERROR("Renderer::Pass_BlurGaussian: Invalid parameters, textures must match because they will get swapped");
			return;
		}

		m_rhiDevice->EventBegin("Pass_BlurGaussian");

		// Set common states
		m_rhiPipeline->SetViewport(texIn->GetViewport());

		// Horizontal Gaussian blur
		auto direction = Vector2(1.0f, 0.0f);
		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight(), sigma, direction);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetVertexShader(m_shaderQuad);
		m_rhiPipeline->SetPixelShader(m_shaderQuad_blur_gaussian);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Vertical Gaussian blur	
		direction = Vector2(0.0f, 1.0f);
		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight(), sigma, direction);
		m_rhiPipeline->SetRenderTarget(texIn);
		m_rhiPipeline->SetPixelShader(m_shaderQuad_blur_gaussian);
		m_rhiPipeline->SetTexture(texOut);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Swap textures
		texIn.swap(texOut);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_BlurBilateralGaussian(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut, float sigma, float pixelStride)
	{
		if (texIn->GetWidth() != texOut->GetWidth() ||
			texIn->GetHeight() != texOut->GetHeight() ||
			texIn->GetFormat() != texOut->GetFormat())
		{
			LOG_ERROR("Renderer::Pass_BlurBilateralGaussian: Invalid parameters, textures must match because they will get swapped");
			return;
		}

		m_rhiDevice->EventBegin("Pass_BlurBilateralGaussian");

		// Set common states
		m_rhiPipeline->SetViewport(texIn->GetViewport());

		// Horizontal Gaussian blur
		auto direction = Vector2(pixelStride, 0.0f);
		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight(), sigma, direction);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetVertexShader(m_shaderQuad);
		m_rhiPipeline->SetPixelShader(m_shaderQuad_blur_gaussianBilateral);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		direction = Vector2(0.0f, pixelStride);
		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight(), sigma, direction);
		m_rhiPipeline->SetRenderTarget(texIn);
		m_rhiPipeline->SetTexture(texOut);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		texIn.swap(texOut);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_TAA(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_TAA");

		// Resolve
		SetGlobalBuffer(m_viewProjection_Orthographic, m_renderTexFull_TAA_Current->GetWidth(), m_renderTexFull_TAA_Current->GetHeight());
		m_rhiPipeline->SetRenderTarget(m_renderTexFull_TAA_Current);
		m_rhiPipeline->SetViewport(m_renderTexFull_TAA_Current->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_taa);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetTexture(m_renderTexFull_TAA_History);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Velocity));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Output to texOut
		SetGlobalBuffer(m_viewProjection_Orthographic, texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_texture);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetTexture(m_renderTexFull_TAA_Current);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Swap textures so current becomes history
		m_renderTexFull_TAA_Current.swap(m_renderTexFull_TAA_History);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Bloom");

		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		SetGlobalBuffer(m_viewProjection_Orthographic, texOut->GetWidth(), texOut->GetHeight());

		// Bright pass
		m_rhiPipeline->SetRenderTarget(m_renderTexQuarter_Blur1);
		m_rhiPipeline->SetViewport(m_renderTexQuarter_Blur1->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_bloomBright);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		float sigma = 2.0f;
		Pass_BlurGaussian(m_renderTexQuarter_Blur1, m_renderTexQuarter_Blur2, sigma);

		// Additive blending
		SetGlobalBuffer(m_viewProjection_Orthographic, texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_bloomBLend);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_renderTexQuarter_Blur2);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_ToneMapping(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_ToneMapping");

		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_toneMapping);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_GammaCorrection(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_GammaCorrection");

		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_gammaCorrection);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_FXAA");

		// Common states
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		SetGlobalBuffer(m_viewProjection_Orthographic, texIn->GetWidth(), texIn->GetHeight());

		// Luma
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_luma);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// FXAA
		m_rhiPipeline->SetRenderTarget(texIn);
		m_rhiPipeline->SetViewport(texIn->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_fxaa);	
		m_rhiPipeline->SetTexture(texOut);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Swap the textures
		texIn.swap(texOut);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_ChromaticAberration");

		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_chromaticAberration);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_MotionBlur(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_MotionBlur");

		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetPixelShader(m_shaderQuad_motionBlur);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Velocity));
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Sharpening");

		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_shaderQuad_sharpening);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Lines(shared_ptr<RHI_RenderTexture>& texOut)
	{
		bool drawPhysics	= m_flags & Render_Gizmo_Physics;
		bool drawPickingRay = m_flags & Render_Gizmo_PickingRay;
		bool drawAABBs		= m_flags & Render_Gizmo_AABB;
		bool drawGrid		= m_flags & Render_Gizmo_Grid;
		bool draw			= drawPhysics | drawPickingRay | drawAABBs | drawGrid;
		if (!draw)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Lines");

		m_rhiPipeline->SetState(m_pipelineLine);
		m_rhiPipeline->SetAlphaBlending(true);
		m_rhiPipeline->SetRenderTarget(texOut, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView());
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		{
			// Picking ray
			if (drawPickingRay)
			{
				const Ray& ray = m_camera->GetPickingRay();
				AddLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
			}

			// bounding boxes
			if (drawAABBs)
			{
				for (const auto& actor : m_actors[Renderable_ObjectOpaque])
				{
					if (auto renderable = actor->GetRenderable_PtrRaw())
					{
						AddBoundigBox(renderable->Geometry_AABB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& actor : m_actors[Renderable_ObjectTransparent])
				{
					if (auto renderable = actor->GetRenderable_PtrRaw())
					{
						AddBoundigBox(renderable->Geometry_AABB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}

			auto lineVertexBufferSize = (unsigned int)m_lineVertices.size();
			if (lineVertexBufferSize != 0)
			{
				if (lineVertexBufferSize > m_lineVertexCount)
				{
					m_lineVertexBuffer = make_shared<RHI_VertexBuffer>(m_rhiDevice);
					m_lineVertexBuffer->CreateDynamic(sizeof(RHI_Vertex_PosCol), lineVertexBufferSize);
					m_lineVertexCount = lineVertexBufferSize;
				}

				// Update line vertex buffer
				void* data = m_lineVertexBuffer->Map();
				memcpy(data, &m_lineVertices[0], sizeof(RHI_Vertex_PosCol) * lineVertexBufferSize);
				m_lineVertexBuffer->Unmap();

				// Set pipeline state
				m_rhiPipeline->SetVertexBuffer(m_lineVertexBuffer);
				auto buffer = Struct_Matrix_Matrix(m_view, m_projection);
				m_shaderLine->UpdateBuffer(&buffer);
				m_rhiPipeline->Draw(lineVertexBufferSize);

				m_lineVertices.clear();
			}
		}
		
		// Grid
		if (drawGrid)
		{
			m_rhiPipeline->SetIndexBuffer(m_grid->GetIndexBuffer());
			m_rhiPipeline->SetVertexBuffer(m_grid->GetVertexBuffer());
			auto buffer = Struct_Matrix_Matrix(m_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_view, m_projection);
			m_shaderLine->UpdateBuffer(&buffer);
			m_rhiPipeline->DrawIndexed(m_grid->GetIndexCount(), 0, 0);
		}

		m_rhiPipeline->SetAlphaBlending(false);
		m_rhiPipeline->ClearPendingStates();

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Gizmos(shared_ptr<RHI_RenderTexture>& texOut)
	{
		bool render_lights		= m_flags & Render_Gizmo_Lights;
		bool render_transform	= m_flags & Render_Gizmo_Transform;
		bool render				= render_lights || render_transform;
		if (!render)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Gizmos");

		// Set shared states
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetFillMode(Fill_Solid);

		auto& lights = m_actors[Renderable_Light];
		if (lights.size() != 0)
		{
			m_rhiDevice->EventBegin("Gizmo_Lights");
			m_rhiPipeline->SetShader(m_shaderQuad_texture);
			m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
			m_rhiPipeline->SetAlphaBlending(true);

			for (const auto& actor : lights)
			{
				Vector3 position_light_world		= actor->GetTransform_PtrRaw()->GetPosition();
				Vector3 position_camera_world		= m_camera->GetTransform()->GetPosition();
				Vector3 direction_camera_to_light	= (position_light_world - position_camera_world).Normalized();
				float VdL							= Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);

				// Don't bother drawing if out of view
				if (VdL <= 0.5f)
					continue;

				// Compute light screen space position and scale (based on distance from the camera)
				Vector2 position_light_screen	= m_camera->WorldToScreenPoint(position_light_world);
				float distance					= (position_camera_world - position_light_world).Length() + M_EPSILON;
				float scale						= GIZMO_MAX_SIZE / distance;
				scale							= Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

				// Choose texture based on light type
				shared_ptr<RHI_Texture> lightTex = nullptr;
				LightType type = actor->GetComponent<Light>()->GetLightType();
				if (type == LightType_Directional)	lightTex = m_gizmoTexLightDirectional;
				else if (type == LightType_Point)	lightTex = m_gizmoTexLightPoint;
				else if (type == LightType_Spot)	lightTex = m_gizmoTexLightSpot;

				// Construct appropriate rectangle
				float texWidth	= lightTex->GetWidth() * scale;
				float texHeight = lightTex->GetHeight() * scale;
				m_gizmoRectLight->Create(position_light_screen.x - texWidth * 0.5f, position_light_screen.y - texHeight * 0.5f, texWidth, texHeight);
			
				SetGlobalBuffer(m_viewProjection_Orthographic, texOut->GetWidth(), texOut->GetHeight());
				m_rhiPipeline->SetTexture(lightTex);
				m_rhiPipeline->SetIndexBuffer(m_gizmoRectLight->GetIndexBuffer());
				m_rhiPipeline->SetVertexBuffer(m_gizmoRectLight->GetVertexBuffer());		
				m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
				
			}
			m_rhiPipeline->SetAlphaBlending(false);		
			m_rhiDevice->EventEnd();
		}

		// Transform
		if (render_transform)
		{
			m_transformGizmo->Update(m_camera->GetPickedActor().lock(), m_camera);
			if (m_transformGizmo->IsInspecting())
			{
				m_rhiDevice->EventBegin("Gizmo_Transform");

				m_rhiPipeline->SetShader(m_shaderTransformGizmo);
				m_rhiPipeline->SetIndexBuffer(m_transformGizmo->GetIndexBuffer());
				m_rhiPipeline->SetVertexBuffer(m_transformGizmo->GetVertexBuffer());
				SetGlobalBuffer();

				// X - Axis		
				auto buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle_Pos_X().GetTransform(), m_transformGizmo->GetHandle_Pos_X().GetColor());
				m_shaderTransformGizmo->UpdateBuffer(&buffer);
				m_rhiPipeline->SetConstantBuffer(m_shaderTransformGizmo->GetConstantBuffer(), 1, Buffer_Global);
				m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);

				// Y - Axis		
				buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle_Pos_Y().GetTransform(), m_transformGizmo->GetHandle_Pos_Y().GetColor());
				m_shaderTransformGizmo->UpdateBuffer(&buffer);
				m_rhiPipeline->SetConstantBuffer(m_shaderTransformGizmo->GetConstantBuffer(), 1, Buffer_Global);
				m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);

				// Z - Axis		
				buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle_Pos_Z().GetTransform(), m_transformGizmo->GetHandle_Pos_Z().GetColor());
				m_shaderTransformGizmo->UpdateBuffer(&buffer);
				m_rhiPipeline->SetConstantBuffer(m_shaderTransformGizmo->GetConstantBuffer(), 1, Buffer_Global);
				m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);

				m_rhiDevice->EventEnd();
			}
		}

		m_rhiPipeline->ClearPendingStates();

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_PerformanceMetrics(shared_ptr<RHI_RenderTexture>& texOut)
	{
		bool draw = m_flags & Render_Gizmo_PerformanceMetrics;
		if (!draw)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_PerformanceMetrics");

		Vector2 textPos = Vector2(-(int)Settings::Get().Viewport_GetWidth() * 0.5f + 1.0f, (int)Settings::Get().Viewport_GetHeight() * 0.5f);
		m_font->SetText(Profiler::Get().GetMetrics(), textPos);

		m_rhiPipeline->SetAlphaBlending(true);
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetCullMode(Cull_Back);
		m_rhiPipeline->SetFillMode(Fill_Solid);
		m_rhiPipeline->SetIndexBuffer(m_font->GetIndexBuffer());
		m_rhiPipeline->SetVertexBuffer(m_font->GetVertexBuffer());
		m_rhiPipeline->SetRenderTarget(texOut);	
		m_rhiPipeline->SetTexture(m_font->GetTexture());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetShader(m_shaderFont);
		auto buffer = Struct_Matrix_Vector4(m_viewProjection_Orthographic, m_font->GetColor());
		m_shaderFont->UpdateBuffer(&buffer);
		m_rhiPipeline->SetConstantBuffer(m_shaderFont->GetConstantBuffer(), 0, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_font->GetIndexCount(), 0, 0);
		m_rhiPipeline->SetAlphaBlending(false);
		m_rhiPipeline->ClearPendingStates();

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	bool Renderer::Pass_GBufferVisualize(shared_ptr<RHI_RenderTexture>& texOut)
	{
		GBuffer_Texture_Type texType = GBuffer_Target_Unknown;
		texType	= Flags_IsSet(Render_GBuffer_Albedo)	? GBuffer_Target_Albedo		: texType;
		texType = Flags_IsSet(Render_GBuffer_Normal)	? GBuffer_Target_Normal		: texType;
		texType = Flags_IsSet(Render_GBuffer_Material)	? GBuffer_Target_Material	: texType;
		texType = Flags_IsSet(Render_GBuffer_Velocity)	? GBuffer_Target_Velocity	: texType;
		texType = Flags_IsSet(Render_GBuffer_Depth)		? GBuffer_Target_Depth		: texType;

		if (texType != GBuffer_Target_Unknown)
		{
			TIME_BLOCK_START_MULTI();
			m_rhiDevice->EventBegin("Pass_GBufferVisualize");

			SetGlobalBuffer(m_viewProjection_Orthographic, texOut->GetWidth(), texOut->GetHeight());
			m_rhiPipeline->SetRenderTarget(texOut);
			m_rhiPipeline->ClearPendingStates();
			m_rhiPipeline->SetVertexBuffer(m_quad->GetVertexBuffer());
			m_rhiPipeline->SetIndexBuffer(m_quad->GetIndexBuffer());
			m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
			m_rhiPipeline->SetFillMode(Fill_Solid);
			m_rhiPipeline->SetCullMode(Cull_Back);
			m_rhiPipeline->SetInputLayout(m_shaderQuad_texture->GetInputLayout());
			m_rhiPipeline->SetShader(m_shaderQuad_texture);
			m_rhiPipeline->SetViewport(m_gbuffer->GetTexture(texType)->GetViewport());
			m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(texType));
			m_rhiPipeline->SetSampler(m_samplerTrilinearClamp);
			m_rhiPipeline->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
			m_rhiPipeline->ClearPendingStates();

			m_rhiDevice->EventEnd();
			TIME_BLOCK_END_MULTI();
		}

		return true;
	}
	//=============================================================================================================

	Light* Renderer::GetLightDirectional()
	{
		auto actors = m_actors[Renderable_Light];

		for (const auto& actor : actors)
		{
			Light* light = actor->GetComponent<Light>().get();
			if (light->GetLightType() == LightType_Directional)
				return light;
		}

		return nullptr;
	}
}
