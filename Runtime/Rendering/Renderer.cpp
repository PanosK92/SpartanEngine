/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ================================
#include "Renderer.h"
#include "Rectangle.h"
#include "Grid.h"
#include "Font.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommonBuffers.h"
#include "../RHI/RHI_Sampler.h"
#include "../Scene/Actor.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Scene/Components/Skybox.h"
#include "../Scene/Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../RHI/RHI_PipelineState.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	static Physics* g_physics				= nullptr;
	static ResourceManager* g_resourceMng	= nullptr;
	unsigned long Renderer::m_flags;

	Renderer::Renderer(Context* context, void* drawHandle) : Subsystem(context)
	{
		m_skybox					= nullptr;
		m_camera					= nullptr;
		m_texEnvironment			= nullptr;
		m_lineRenderer				= nullptr;
		m_nearPlane					= 0.0f;
		m_farPlane					= 0.0f;
		m_rhiDevice					= nullptr;
		m_flags						= 0;
		m_flags						|= Render_SceneGrid;
		m_flags						|= Render_Light;
		m_flags						|= Render_Bloom;
		m_flags						|= Render_FXAA;
		m_flags						|= Render_Sharpening;
		m_flags						|= Render_ChromaticAberration;
		m_flags						|= Render_Correction;

		// Create RHI device
		m_rhiDevice			= make_shared<RHI_Device>(drawHandle);
		m_rhiPipelineState	= make_shared<RHI_PipelineState>(m_rhiDevice);

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVED, EVENT_HANDLER_VARIANT(Renderables_Acquire));
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Create/Get required systems
		
		g_resourceMng		= m_context->GetSubsystem<ResourceManager>();
		g_physics			= m_context->GetSubsystem<Physics>();

		// Get standard resource directories
		string fontDir			= g_resourceMng->GetStandardResourceDirectory(Resource_Font);
		string shaderDirectory	= g_resourceMng->GetStandardResourceDirectory(Resource_Shader);
		string textureDirectory = g_resourceMng->GetStandardResourceDirectory(Resource_Texture);

		// Load a font (used for performance metrics)
		m_font = make_unique<Font>(m_context, fontDir + "CalibriBold.ttf", 12, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		// Make a grid (used in editor)
		m_grid = make_unique<Grid>(m_context);
		// Light gizmo icon rectangle
		m_gizmoRectLight = make_unique<Rectangle>(m_context);

		RenderTargets_Create(Settings::Get().GetResolutionWidth(), Settings::Get().GetResolutionHeight());

		// SAMPLERS
		{
			m_samplerPointClampAlways		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Point,			Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerPointClampGreater		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Point,			Texture_Address_Clamp,	Texture_Comparison_GreaterEqual);
			m_samplerLinearClampGreater		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Linear,			Texture_Address_Clamp,	Texture_Comparison_GreaterEqual);
			m_samplerLinearClampAlways		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Linear,			Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerBilinearClampAlways	= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Bilinear,		Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerAnisotropicWrapAlways	= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Anisotropic,	Texture_Address_Wrap,	Texture_Comparison_Always);
		}

		// SHADERS
		{
			// Light
			m_shaderLight = make_shared<LightShader>();
			m_shaderLight->Compile(shaderDirectory + "Light.hlsl", m_rhiDevice);

			// Line
			m_shaderLine = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderLine->Compile(shaderDirectory + "Line.hlsl", Input_PositionColor);
			m_shaderLine->AddBuffer<Struct_Matrix_Matrix_Matrix>(Buffer_VertexShader, 0);

			// Depth
			m_shaderLightDepth = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderLightDepth->Compile(shaderDirectory + "ShadowingDepth.hlsl", Input_Position);
			m_shaderLightDepth->AddBuffer<Struct_Matrix>(Buffer_VertexShader, 0);

			// Grid
			m_shaderGrid = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderGrid->Compile(shaderDirectory + "Grid.hlsl", Input_PositionColor);
			m_shaderGrid->AddBuffer<Struct_Matrix>(Buffer_VertexShader, 0);

			// Font
			m_shaderFont = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderFont->Compile(shaderDirectory + "Font.hlsl", Input_PositionTexture);
			m_shaderFont->AddBuffer<Struct_Matrix_Vector4>(Buffer_Global, 0);

			// Texture
			m_shaderTexture = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTexture->Compile(shaderDirectory + "Texture.hlsl", Input_PositionTexture);
			m_shaderTexture->AddBuffer<Struct_Matrix>(Buffer_VertexShader, 0);

			// FXAA
			m_shaderFXAA = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderFXAA->AddDefine("PASS_FXAA");
			m_shaderFXAA->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderFXAA->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Sharpening
			m_shaderSharpening = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderSharpening->AddDefine("PASS_SHARPENING");
			m_shaderSharpening->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderSharpening->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Sharpening
			m_shaderChromaticAberration = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderChromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
			m_shaderChromaticAberration->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderChromaticAberration->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Blur Box
			m_shaderBlurBox = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurBox->AddDefine("PASS_BLUR_BOX");
			m_shaderBlurBox->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderBlurBox->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Blur Gaussian Horizontal
			m_shaderBlurGaussianH = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurGaussianH->AddDefine("PASS_BLUR_GAUSSIAN_H");
			m_shaderBlurGaussianH->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);		
			m_shaderBlurGaussianH->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Blur Gaussian Vertical
			m_shaderBlurGaussianV = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurGaussianV->AddDefine("PASS_BLUR_GAUSSIAN_V", "1");
			m_shaderBlurGaussianV->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBlurGaussianV->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Bloom - bright
			m_shaderBloom_Bright = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBloom_Bright->AddDefine("PASS_BRIGHT", "1");
			m_shaderBloom_Bright->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBloom_Bright->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Bloom - blend
			m_shaderBloom_BlurBlend = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBloom_BlurBlend->AddDefine("PASS_BLEND_ADDITIVE");
			m_shaderBloom_BlurBlend->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBloom_BlurBlend->AddBuffer<Struct_Matrix>(Buffer_VertexShader, 0);

			// Tone-mapping
			m_shaderCorrection = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderCorrection->AddDefine("PASS_CORRECTION");
			m_shaderCorrection->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);		
			m_shaderCorrection->AddBuffer<Struct_Matrix_Vector2>(Buffer_Global, 0);

			// Transformation gizmo
			m_shaderTransformationGizmo = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTransformationGizmo->Compile(shaderDirectory + "TransformationGizmo.hlsl", Input_PositionTextureTBN);
			m_shaderTransformationGizmo->AddBuffer<Struct_Matrix_Vector3_Vector3>(Buffer_Global, 0);

			// Shadowing (shadow mapping & SSAO)
			m_shaderShadowing = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderShadowing->Compile(shaderDirectory + "Shadowing.hlsl", Input_PositionTexture);
			m_shaderShadowing->AddBuffer<Struct_Shadowing>(Buffer_Global, 0);
		}

		// TEXTURES
		{
			// Noise texture (used by SSAO shader)
			m_texNoiseMap = make_shared<RHI_Texture>(m_context);
			m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
			m_texNoiseMap->SetType(TextureType_Normal);

			// Gizmo icons
			m_gizmoTexLightDirectional = make_unique<RHI_Texture>(m_context);
			m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");
			m_gizmoTexLightDirectional->SetType(TextureType_Albedo);

			m_gizmoTexLightPoint = make_unique<RHI_Texture>(m_context);
			m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");
			m_gizmoTexLightPoint->SetType(TextureType_Albedo);

			m_gizmoTexLightSpot = make_unique<RHI_Texture>(m_context);
			m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
			m_gizmoTexLightSpot->SetType(TextureType_Albedo);
		}

		return true;
	}

	void Renderer::SetRenderTarget(shared_ptr<RHI_RenderTexture>& renderTarget, bool clear /*= true*/)
	{
		renderTarget->SetAsRenderTarget();
		if (clear) renderTarget->Clear(GetClearColor());
	}

	void Renderer::SetRenderTarget(bool clear /*= true*/)
	{
		m_rhiDevice->Bind_BackBufferAsRenderTarget();
		m_rhiPipelineState->SetViewport((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight());
		m_rhiPipelineState->Bind();
		if (clear) m_rhiDevice->Clear(GetClearColor());
	}

	void* Renderer::GetFrame()
	{
		return m_renderTexPong ? m_renderTexPong->GetShaderResourceView() : nullptr;
	}

	void Renderer::Present()
	{
		m_rhiDevice->Present();
	}

	void Renderer::Render()
	{
		if (!m_rhiDevice || !m_rhiDevice->IsInitialized())
			return;

		PROFILE_FUNCTION_BEGIN();
		Profiler::Get().Reset();

		// If there is a camera, render the scene
		if (m_camera)
		{
			m_mV					= m_camera->GetViewMatrix();
			m_mV_base				= m_camera->GetBaseViewMatrix();
			m_mP_perspective		= m_camera->GetProjectionMatrix();
			m_mP_orthographic		= Matrix::CreateOrthographicLH((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight(), m_nearPlane, m_farPlane);		
			m_wvp_perspective		= m_mV * m_mP_perspective;
			m_wvp_baseOrthographic	= m_mV_base * m_mP_orthographic;
			m_nearPlane				= m_camera->GetNearPlane();
			m_farPlane				= m_camera->GetFarPlane();

			// If there is nothing to render clear to camera's color and present
			if (m_renderables.empty())
			{
				m_rhiDevice->Clear(m_camera->GetClearColor());
				m_rhiDevice->Present();
				return;
			}

			Pass_DepthDirectionalLight(m_directionalLight);
		
			Pass_GBuffer();
			
			Pass_PreLight(
				m_gbuffer->GetShaderResource(GBuffer_Target_Normal),	// IN:	Texture			- Normal
				m_gbuffer->GetShaderResource(GBuffer_Target_Depth),		// IN:	Texture			- Depth
				m_texNoiseMap,											// IN:	Texture			- Normal noise
				m_renderTexPing,										// IN:	Render texture		
				m_renderTexShadowing									// OUT: Render texture	- Shadowing (Shadow mapping + SSAO)
			);

			Pass_Light(
				m_renderTexShadowing,	// IN:	Texture			- Shadowing (Shadow mapping + SSAO)
				m_renderTexPing			// OUT: Render texture	- Result
			);

			Pass_PostLight(
				m_renderTexPing,	// IN:	Render texture - Deferred pass result
				m_renderTexPing2,	// IN:	Render texture - A spare one
				m_renderTexPong		// OUT: Render texture - Result
			);
		}		
		else // If there is no camera, clear to black
		{
			m_rhiDevice->Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		PROFILE_FUNCTION_END();
	}

	void Renderer::SetBackBufferSize(int width, int height)
	{
		Settings::Get().SetViewport(width, height);
		m_rhiDevice->SetResolution(width, height);
		m_rhiPipelineState->SetViewport((float)width, (float)height);
		m_rhiPipelineState->Bind();
	}

	const RHI_Viewport& Renderer::GetViewportBackBuffer()
	{
		return m_rhiDevice->GetViewport();
	}

	void Renderer::SetResolution(int width, int height)
	{
		// Return if resolution already set
		if (Settings::Get().GetResolution().x == width && Settings::Get().GetResolution().y == height)
			return;

		// Return if resolution is invalid
		if (width <= 0 || height <= 0)
		{
			LOG_WARNING("Renderer::SetResolutionInternal: Invalid resolution");
			return;
		}

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

		Settings::Get().SetResolution(Vector2((float)width, (float)height));
		RenderTargets_Create(width, height);
		LOGF_INFO("Renderer::SetResolution:: Resolution was set to %dx%d", width, height);
	}

	const Vector2& Renderer::GetViewportInternal()
	{
		// The internal (frame) viewport equals the resolution
		return Settings::Get().GetResolution();
	}

	void Renderer::Clear()
	{
		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lights.clear();
		m_lights.shrink_to_fit();

		m_directionalLight	= nullptr;
		m_skybox			= nullptr;
		m_lineRenderer		= nullptr;
		m_camera			= nullptr;
	}

	void Renderer::RenderTargets_Create(int width, int height)
	{
		// Resize everything
		m_gbuffer.reset();
		m_gbuffer = make_unique<GBuffer>(m_rhiDevice, width, height);

		m_quad.reset();
		m_quad = make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)width, (float)height);

		m_renderTexPing.reset();
		m_renderTexPing = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexPing2.reset();
		m_renderTexPing2 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexPong.reset();
		m_renderTexPong = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexShadowing.reset();
		m_renderTexShadowing = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.5f), int(height * 0.5f), false, Texture_Format_R32G32_FLOAT);
	}

	//= RENDERABLES ============================================================================================
	void Renderer::Renderables_Acquire(const Variant& renderables)
	{
		PROFILE_FUNCTION_BEGIN();

		Clear();
		auto renderablesVec = renderables.Get<vector<weak_ptr<Actor>>>();

		for (const auto& renderable : renderablesVec)
		{
			Actor* actor = renderable.lock().get();
			if (!actor)
				continue;

			// Get renderables
			m_renderables.emplace_back(actor);

			// Get lights
			if (auto light = actor->GetComponent<Light>().lock())
			{
				m_lights.emplace_back(light.get());
				if (light->GetLightType() == LightType_Directional)
				{
					m_directionalLight = light.get();
				}
			}

			// Get skybox
			if (auto skybox = actor->GetComponent<Skybox>().lock())
			{
				m_skybox = skybox.get();
				m_lineRenderer = actor->GetComponent<LineRenderer>().lock().get(); // Hush hush...
			}

			// Get camera
			if (auto camera = actor->GetComponent<Camera>().lock())
			{
				m_camera = camera.get();
			}
		}
		Renderables_Sort(&m_renderables);

		PROFILE_FUNCTION_END();
	}

	void Renderer::Renderables_Sort(vector<Actor*>* renderables)
	{
		if (renderables->size() <= 1)
			return;

		sort(renderables->begin(), renderables->end(),[](Actor* a, Actor* b)
		{
			// Get renderable component
			auto a_renderable = a->GetRenderable_PtrRaw();
			auto b_renderable = b->GetRenderable_PtrRaw();

			// Validate renderable components
			if (!a_renderable || !b_renderable)
				return false;

			// Get geometry parents
			auto a_geometryModel = a_renderable->Geometry_Model();
			auto b_geometryModel = b_renderable->Geometry_Model();

			// Validate geometry parents
			if (!a_geometryModel || !b_geometryModel)
				return false;

			// Get materials
			auto a_material = a_renderable->Material_Ref();
			auto b_material = b_renderable->Material_Ref();

			if (!a_material || !b_material)
				return false;

			// Get key for models
			auto a_keyModel = a_geometryModel->GetResourceID();
			auto b_keyModel = b_geometryModel->GetResourceID();

			// Get key for shaders
			auto a_keyShader = a_material->GetShader().lock()->GetResourceID();
			auto b_keyShader = b_material->GetShader().lock()->GetResourceID();

			// Get key for materials
			auto a_keyMaterial = a_material->GetResourceID();
			auto b_keyMaterial = b_material->GetResourceID();

			auto a_key = 
				(((unsigned long long)a_keyModel)		<< 48u)	| 
				(((unsigned long long)a_keyShader)		<< 32u)	|
				(((unsigned long long)a_keyMaterial)	<< 16u);

			auto b_key = 
				(((unsigned long long)b_keyModel)		<< 48u)	|
				(((unsigned long long)b_keyShader)		<< 32u)	|
				(((unsigned long long)b_keyMaterial)	<< 16u);
	
			return a_key < b_key;
		});
	}
	//==========================================================================================================

	//= PASSES =================================================================================================
	void Renderer::Pass_DepthDirectionalLight(Light* light)
	{
		if (!light || !light->GetCastShadows())
			return;

		PROFILE_FUNCTION_BEGIN();

		m_rhiDevice->EventBegin("Pass_DepthDirectionalLight");
		m_rhiDevice->EnableDepth(true);
		m_rhiPipelineState->SetShader(m_shaderLightDepth);

		for (unsigned int i = 0; i < light->ShadowMap_GetCount(); i++)
		{
			light->ShadowMap_SetRenderTarget(i);
			m_rhiDevice->EventBegin("Pass_ShadowMap_" + to_string(i));
			for (const auto& actor : m_renderables)
			{
				// Get renderable and material
				Renderable* obj_renderable = actor->GetRenderable_PtrRaw();
				Material* obj_material = obj_renderable ? obj_renderable->Material_Ref() : nullptr;

				if (!obj_renderable || !obj_material)
					continue;

				// Get geometry
				Model* obj_geometry = obj_renderable->Geometry_Model();
				if (!obj_geometry)
					continue;

				// Bind geometry
				if (m_currentlyBoundGeometry != obj_geometry->GetResourceID())
				{
					m_rhiPipelineState->SetIndexBuffer(obj_geometry->GetIndexBuffer());
					m_rhiPipelineState->SetVertexBuffer(obj_geometry->GetVertexBuffer());
					m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
					m_currentlyBoundGeometry = obj_geometry->GetResourceID();
				}

				// Skip meshes that don't cast shadows
				if (!obj_renderable->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (obj_material->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustum
				//if (!m_directionalLight->IsInViewFrustrum(obj_renderable, i))
					//continue;

				m_shaderLightDepth->BindBuffer(&Struct_Matrix(actor->GetTransform_PtrRaw()->GetWorldTransform() * light->GetViewMatrix() * light->ShadowMap_GetProjectionMatrix(i)), 0);
				m_rhiPipelineState->Bind();

				m_rhiDevice->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());

				Profiler::Get().m_drawCalls++;
			}
			m_rhiDevice->EventEnd();
		}

		// Reset pipeline state tracking
		m_currentlyBoundGeometry = 0;
		
		m_rhiDevice->EnableDepth(false);
		m_rhiDevice->EventEnd();

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhiDevice)
			return;

		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_GBuffer");

		m_gbuffer->SetAsRenderTarget();
		m_gbuffer->Clear();

		// Bind sampler 
		m_rhiPipelineState->SetSampler(m_samplerAnisotropicWrapAlways);
		m_rhiPipelineState->SetViewport((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight());

		for (auto actor : m_renderables)
		{
			// Get renderable and material
			Renderable* obj_renderable	= actor->GetRenderable_PtrRaw();
			Material* obj_material		= obj_renderable ? obj_renderable->Material_Ref() : nullptr;

			if (!obj_renderable || !obj_material)
				continue;

			// Get geometry and shader
			Model* obj_geometry			= obj_renderable->Geometry_Model();
			ShaderVariation* obj_shader	= obj_material->GetShader().lock().get();

			if (!obj_geometry || !obj_shader)
				continue;

			// Skip transparent objects (for now)
			if (obj_material->GetOpacity() < 1.0f)
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(obj_renderable))
				continue;

			// set face culling (changes only if required)
			m_rhiPipelineState->SetCullMode(obj_material->GetCullMode());

			// Bind geometry
			if (m_currentlyBoundGeometry != obj_geometry->GetResourceID())
			{	
				m_rhiPipelineState->SetIndexBuffer(obj_geometry->GetIndexBuffer());
				m_rhiPipelineState->SetVertexBuffer(obj_geometry->GetVertexBuffer());
				m_currentlyBoundGeometry = obj_geometry->GetResourceID();
			}

			// Bind shader
			if (m_currentlyBoundShader != obj_shader->GetResourceID())
			{
				m_rhiPipelineState->SetShader(obj_shader->GetShader());
				obj_shader->UpdatePerFrameBuffer(m_camera);
				m_currentlyBoundShader = obj_shader->GetResourceID();
			}

			// Bind material
			if (m_currentlyBoundMaterial != obj_material->GetResourceID())
			{
				obj_shader->UpdatePerMaterialBuffer(obj_material);
				auto getSRV = [&obj_material](TextureType type)
				{
					auto texture = obj_material->GetTextureByType(type).lock();
					return texture ? texture->GetShaderResource() : nullptr;
				};
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Albedo));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Roughness));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Metallic));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Normal));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Height));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Occlusion));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Emission));
				m_rhiPipelineState->SetTexture(getSRV(TextureType_Mask));
				m_currentlyBoundMaterial = obj_material->GetResourceID();
			}

			// UPDATE PER OBJECT BUFFER
			obj_shader->UpdatePerObjectBuffer(
				actor->GetTransform_PtrRaw()->GetWorldTransform(),
				m_mV, 
				m_mP_perspective
			);
		
			m_rhiPipelineState->SetConstantBuffer(obj_shader->GetPerFrameBuffer(), 0, Buffer_PixelShader);		
			m_rhiPipelineState->SetConstantBuffer(obj_shader->GetMaterialBuffer(), 1, Buffer_PixelShader);
			m_rhiPipelineState->SetConstantBuffer(obj_shader->GetPerObjectBuffer(), 2, Buffer_VertexShader);

			m_rhiPipelineState->Bind();

			// Render	
			m_rhiDevice->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());
			Profiler::Get().m_meshesRendered++;

		} // Actor/MESH ITERATION

		// Reset pipeline state tracking
		m_currentlyBoundGeometry	= 0;
		m_currentlyBoundShader		= 0;
		m_currentlyBoundMaterial	= 0;

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PreLight(
		void* inTextureNormal,
		void* inTextureDepth,
		shared_ptr<RHI_Texture>& inTextureNormalNoise,
		shared_ptr<RHI_RenderTexture>& inRenderTexure,
		shared_ptr<RHI_RenderTexture>& outRenderTextureShadowing
	)
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_PreLight");

		m_rhiPipelineState->SetIndexBuffer(m_quad->GetIndexBuffer());
		m_rhiPipelineState->SetVertexBuffer(m_quad->GetVertexBuffer());

		// Shadow mapping + SSAO
		Pass_Shadowing(inTextureNormal, inTextureDepth, inTextureNormalNoise, m_directionalLight, inRenderTexure);

		// Blur the shadows and the SSAO
		Pass_Blur(inRenderTexure, outRenderTextureShadowing, Settings::Get().GetResolution());

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_Light(shared_ptr<RHI_RenderTexture>& inTextureShadowing, shared_ptr<RHI_RenderTexture>& outRenderTexture)
	{
		if (!m_shaderLight->IsCompiled())
			return;

		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_Light");

		m_rhiDevice->EnableDepth(false);

		// Set render target
		SetRenderTarget(outRenderTexture, false);
	
		// Update buffers
		m_shaderLight->UpdateMatrixBuffer(Matrix::Identity, m_mV, m_mV_base, m_mP_perspective, m_mP_orthographic);
		m_shaderLight->UpdateMiscBuffer(m_lights, m_camera);

		m_rhiPipelineState->SetShader(m_shaderLight->GetShader());
		m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Albedo));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Normal));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Specular));
		m_rhiPipelineState->SetTexture(inTextureShadowing->GetShaderResourceView());
		m_rhiPipelineState->SetTexture(m_renderTexPong->GetShaderResourceView()); // previous frame for SSR
		m_rhiPipelineState->SetTexture(m_skybox ? m_skybox->GetShaderResource() : nullptr);
		m_rhiPipelineState->SetSampler(m_samplerAnisotropicWrapAlways);	
		m_rhiPipelineState->SetConstantBuffer(m_shaderLight->GetMatrixBuffer(), 0, Buffer_Global);
		m_rhiPipelineState->SetConstantBuffer(m_shaderLight->GetMiscBuffer(), 1, Buffer_Global);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PostLight(shared_ptr<RHI_RenderTexture>& inRenderTexture1, shared_ptr<RHI_RenderTexture>& inRenderTexture2, shared_ptr<RHI_RenderTexture>& outRenderTexture)
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_PostLight");

		m_rhiPipelineState->SetVertexBuffer(m_quad->GetVertexBuffer());
		m_rhiPipelineState->SetIndexBuffer(m_quad->GetIndexBuffer());

		// Keep track of render target swapping
		bool swaped = false;
		auto SwapTargets = [&swaped, &inRenderTexture1, &outRenderTexture]()
		{
			outRenderTexture.swap(inRenderTexture1);
			swaped = !swaped;
		};

		// BLOOM
		if (RenderFlags_IsSet(Render_Bloom))
		{
			Pass_Bloom(inRenderTexture1, inRenderTexture2, outRenderTexture);
			SwapTargets();
		}

		// CORRECTION
		if (RenderFlags_IsSet(Render_Correction))
		{
			Pass_Correction(inRenderTexture1, outRenderTexture);
			SwapTargets();
		}

		// FXAA
		if (RenderFlags_IsSet(Render_FXAA))
		{
			Pass_FXAA(inRenderTexture1, outRenderTexture);
			SwapTargets();
		}

		// CHROMATIC ABERRATION
		if (RenderFlags_IsSet(Render_ChromaticAberration))
		{
			Pass_ChromaticAberration(inRenderTexture1, outRenderTexture);
			SwapTargets();
		}

		// SHARPENING
		if (RenderFlags_IsSet(Render_Sharpening))
		{
			Pass_Sharpening(inRenderTexture1, outRenderTexture);
		}

		// DEBUG - Rendering continues on last bound target
		Pass_DebugGBuffer();
		Pass_Debug();

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_Correction(shared_ptr<RHI_RenderTexture>& inTexture, shared_ptr<RHI_RenderTexture>& outTexture)
	{
		m_rhiDevice->EventBegin("Pass_Correction");

		SetRenderTarget(outTexture, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetShader(m_shaderCorrection);
		m_rhiPipelineState->SetTexture(inTexture->GetShaderResourceView());
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_shaderCorrection->BindBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, Settings::Get().GetResolution()), 0);	
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_RenderTexture>& inTexture, shared_ptr<RHI_RenderTexture>& outTexture)
	{
		m_rhiDevice->EventBegin("Pass_FXAA");

		SetRenderTarget(outTexture, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetShader(m_shaderFXAA);	
		m_rhiPipelineState->SetTexture(inTexture->GetShaderResourceView());
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_shaderFXAA->BindBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, Settings::Get().GetResolution()), 0);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_RenderTexture>& inTexture, shared_ptr<RHI_RenderTexture>& outTexture)
	{
		m_rhiDevice->EventBegin("Pass_Sharpening");

		SetRenderTarget(outTexture, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetShader(m_shaderSharpening);
		m_rhiPipelineState->SetTexture(inTexture->GetShaderResourceView());
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_shaderSharpening->BindBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, Settings::Get().GetResolution()), 0);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_RenderTexture>& inTexture, shared_ptr<RHI_RenderTexture>& outTexture)
	{
		m_rhiDevice->EventBegin("Pass_ChromaticAberration");

		SetRenderTarget(outTexture, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetShader(m_shaderChromaticAberration);
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_rhiPipelineState->SetTexture(inTexture->GetShaderResourceView());
		m_shaderChromaticAberration->BindBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, Settings::Get().GetResolution()), 0);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_RenderTexture>& inSourceTexture, shared_ptr<RHI_RenderTexture>& inTextureSpare, shared_ptr<RHI_RenderTexture>& outTexture)
	{
		m_rhiDevice->EventBegin("Pass_Bloom");
	
		SetRenderTarget(inTextureSpare, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_rhiPipelineState->SetShader(m_shaderBloom_Bright);
		m_rhiPipelineState->SetTexture(inSourceTexture->GetShaderResourceView());		
		auto buffer = Struct_Matrix_Vector2(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_shaderBloom_Bright->BindBuffer(&buffer, 0);
		m_rhiPipelineState->Bind();
		// Bright pass
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
	
		SetRenderTarget(outTexture, false);

		m_rhiPipelineState->SetShader(m_shaderBlurGaussianH);
		m_rhiPipelineState->SetTexture(inTextureSpare->GetShaderResourceView());
		m_shaderBlurGaussianH->BindBuffer(&buffer, 0);
		m_rhiPipelineState->Bind();
		// Horizontal Gaussian blur
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		SetRenderTarget(inTextureSpare, false);

		m_rhiPipelineState->SetShader(m_shaderBlurGaussianV);
		m_rhiPipelineState->SetTexture(outTexture->GetShaderResourceView());
		m_shaderBlurGaussianV->BindBuffer(&buffer, 0);
		m_rhiPipelineState->Bind();
		// Vertical Gaussian blur
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
		
		SetRenderTarget(outTexture, false);

		m_rhiPipelineState->SetShader(m_shaderBloom_BlurBlend);
		m_rhiPipelineState->SetTexture(inSourceTexture->GetShaderResourceView());
		m_rhiPipelineState->SetTexture(inTextureSpare->GetShaderResourceView());
		m_shaderBloom_BlurBlend->BindBuffer(&Struct_Matrix(m_wvp_baseOrthographic), 0);	
		m_rhiPipelineState->Bind();
		// Additive blending
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Blur(shared_ptr<RHI_RenderTexture>& texture, shared_ptr<RHI_RenderTexture>& renderTarget, const Vector2& blurScale)
	{
		m_rhiDevice->EventBegin("Pass_Blur");

		SetRenderTarget(renderTarget, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetShader(m_shaderBlurBox);
		m_rhiPipelineState->SetTexture(texture->GetShaderResourceView()); // Shadows are in the alpha channel
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_shaderBlurBox->BindBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, blurScale), 0);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Shadowing(
		void* inTextureNormal_shaderResource,
		void* inTextureDepth_shaderResource,
		shared_ptr<RHI_Texture>& inTextureNormalNoise,
		Light* inDirectionalLight,
		shared_ptr<RHI_RenderTexture>& outRenderTexture)
	{
		if (!inDirectionalLight)
			return;

		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_Shadowing");

		// SHADOWING (Shadow mapping + SSAO)
		SetRenderTarget(outRenderTexture, false);

		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetShader(m_shaderShadowing);
		m_rhiPipelineState->SetTexture(inTextureNormal_shaderResource);
		m_rhiPipelineState->SetTexture(inTextureDepth_shaderResource);
		m_rhiPipelineState->SetTexture(inTextureNormalNoise->GetShaderResource());
		if (inDirectionalLight)
		{
			m_rhiPipelineState->SetTexture(inDirectionalLight->ShadowMap_GetShaderResource(0));
			m_rhiPipelineState->SetTexture(inDirectionalLight->ShadowMap_GetShaderResource(1));
			m_rhiPipelineState->SetTexture(inDirectionalLight->ShadowMap_GetShaderResource(2));
		}
		m_rhiPipelineState->SetSampler(m_samplerPointClampGreater);		// Shadow mapping
		m_rhiPipelineState->SetSampler(m_samplerLinearClampGreater);	// SSAO
		m_shaderShadowing->BindBuffer(&Struct_Shadowing
			(
				m_wvp_baseOrthographic,
				m_wvp_perspective.Inverted(),
				m_mV,
				m_mP_perspective,
				Settings::Get().GetResolution(),
				inDirectionalLight,
				m_camera
			),
			0
		);	
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();
	}
	//=============================================================================================================

	bool Renderer::Pass_DebugGBuffer()
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_DebugGBuffer");

		GBuffer_Texture_Type texType = GBuffer_Target_Unknown;
		texType	= RenderFlags_IsSet(Render_Albedo)		? GBuffer_Target_Albedo		: texType;
		texType = RenderFlags_IsSet(Render_Normal)		? GBuffer_Target_Normal		: texType;
		texType = RenderFlags_IsSet(Render_Specular)	? GBuffer_Target_Specular	: texType;
		texType = RenderFlags_IsSet(Render_Depth)		? GBuffer_Target_Depth		: texType;

		if (texType == GBuffer_Target_Unknown)
		{
			m_rhiDevice->EventEnd();
			return false;
		}

		// TEXTURE
		m_rhiPipelineState->SetShader(m_shaderTexture);
		m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(texType));
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_shaderTexture->BindBuffer(&Struct_Matrix(m_wvp_baseOrthographic), 0);		
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();

		return true;
	}

	void Renderer::Pass_Debug()
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhiDevice->EventBegin("Pass_Debug");

		//= PRIMITIVES ===================================================================================
		// Anything that is a bunch of vertices (doesn't have a vertex and and index buffer) gets rendered here
		// by passing it's vertices (VertexPosCol) to the LineRenderer. Typically used only for debugging.
		if (m_lineRenderer)
		{
			m_lineRenderer->ClearVertices();

			// Physics
			if (m_flags & Render_Physics)
			{
				g_physics->DebugDraw();
				if (g_physics->GetPhysicsDebugDraw()->IsDirty())
				{
					m_lineRenderer->AddLines(g_physics->GetPhysicsDebugDraw()->GetLines());
				}
			}

			// Picking ray
			if (m_flags & Render_PickingRay)
			{
				m_lineRenderer->AddLines(m_camera->GetPickingRay());
			}

			// bounding boxes
			if (m_flags & Render_AABB)
			{
				for (const auto& renderableWeak : m_renderables)
				{
					if (auto renderable = renderableWeak->GetRenderable_PtrRaw())
					{
						m_lineRenderer->AddBoundigBox(renderable->Geometry_BB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}

			if (m_lineRenderer->GetVertexCount() != 0)
			{
				m_rhiDevice->EventBegin("Lines");

				// Render
				m_lineRenderer->Update();

				m_rhiPipelineState->SetShader(m_shaderLine);
				m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
				m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
				m_rhiPipelineState->SetVertexBuffer(m_lineRenderer->GetVertexBuffer());
				m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_LineList);
				m_shaderLine->BindBuffer(&Struct_Matrix_Matrix_Matrix(Matrix::Identity, m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix()), 0);	
				m_rhiPipelineState->Bind();

				m_rhiDevice->Draw(m_lineRenderer->GetVertexCount());

				m_rhiDevice->EventEnd();
			}			
		}
		//============================================================================================================

		m_rhiDevice->EnableAlphaBlending(true);

		// Grid
		if (m_flags & Render_SceneGrid)
		{
			m_rhiDevice->EventBegin("Grid");

			m_rhiPipelineState->SetShader(m_shaderGrid);
			m_rhiPipelineState->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
			m_rhiPipelineState->SetSampler(m_samplerAnisotropicWrapAlways);
			m_rhiPipelineState->SetIndexBuffer(m_grid->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(m_grid->GetVertexBuffer());
			m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_LineList);
			m_shaderGrid->BindBuffer(&Struct_Matrix(m_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix()), 0);
			m_rhiPipelineState->Bind();

			m_rhiDevice->DrawIndexed(m_grid->GetIndexCount(), 0, 0);

			m_rhiDevice->EventEnd();
		}

		// Gizmos
		m_rhiDevice->EventBegin("Gizmos");
		{
			if (m_flags & Render_Light)
			{
				m_rhiDevice->EventBegin("Lights");
				for (auto* light : m_lights)
				{
					Vector3 lightWorldPos = light->GetTransform()->GetPosition();
					Vector3 cameraWorldPos = m_camera->GetTransform()->GetPosition();

					// Compute light screen space position and scale (based on distance from the camera)
					Vector2 lightScreenPos	= m_camera->WorldToScreenPoint(lightWorldPos);
					float distance			= Vector3::Length(lightWorldPos, cameraWorldPos);
					float scale				= GIZMO_MAX_SIZE / distance;
					scale					= Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

					// Skip if the light is not in front of the camera
					if (!m_camera->IsInViewFrustrum(lightWorldPos, Vector3(1.0f)))
						continue;

					// Skip if the light if it's too small
					if (scale == GIZMO_MIN_SIZE)
						continue;

					RHI_Texture* lightTex = nullptr;
					LightType type = light->Getactor_PtrRaw()->GetComponent<Light>().lock()->GetLightType();
					if (type == LightType_Directional)
					{
						lightTex = m_gizmoTexLightDirectional.get();
					}
					else if (type == LightType_Point)
					{
						lightTex = m_gizmoTexLightPoint.get();
					}
					else if (type == LightType_Spot)
					{
						lightTex = m_gizmoTexLightSpot.get();
					}

					// Construct appropriate rectangle
					float texWidth = lightTex->GetWidth() * scale;
					float texHeight = lightTex->GetHeight() * scale;
					m_gizmoRectLight->Create(
						lightScreenPos.x - texWidth * 0.5f,
						lightScreenPos.y - texHeight * 0.5f,
						texWidth,
						texHeight
					);

					m_rhiPipelineState->SetShader(m_shaderTexture);
					m_rhiPipelineState->SetTexture(lightTex->GetShaderResource());
					m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
					m_rhiPipelineState->SetIndexBuffer(m_gizmoRectLight->GetIndexBuffer());
					m_rhiPipelineState->SetVertexBuffer(m_gizmoRectLight->GetVertexBuffer());
					m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
					m_shaderTexture->BindBuffer(&Struct_Matrix(m_wvp_baseOrthographic), 0);
					m_rhiPipelineState->Bind();

					m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
				}
				m_rhiDevice->EventEnd();
			}

			// Transformation Gizmo		
			//m_rhi->EventBegin("Transformation");
			//{
			//	TransformationGizmo* gizmo = m_camera->GetTransformationGizmo();
			//	gizmo->SetBuffers();
			//	m_shaderTransformationGizmo->Bind();

			//	// X - Axis
			//	m_shaderTransformationGizmo->Bind_Buffer(gizmo->GetTransformationX() * m_mV * m_mP_perspective, Vector3::Right, Vector3::Zero, 0);
			//	m_rhi->DrawIndexed(gizmo->GetIndexCount(), 0, 0);
			//	// Y - Axis
			//	m_shaderTransformationGizmo->Bind_Buffer(gizmo->GetTransformationY() * m_mV * m_mP_perspective, Vector3::Up, Vector3::Zero, 0);
			//	m_rhi->DrawIndexed(gizmo->GetIndexCount(), 0, 0);
			//	// Z - Axis
			//	m_shaderTransformationGizmo->Bind_Buffer(gizmo->GetTransformationZ() * m_mV * m_mP_perspective, Vector3::Forward, Vector3::Zero, 0);
			//	m_rhi->DrawIndexed(gizmo->GetIndexCount(), 0, 0);
			//}
			//m_rhi->EventEnd();
			
		}
		m_rhiDevice->EventEnd();

		// Performance metrics
		if (m_flags & Render_PerformanceMetrics)
		{
			m_font->SetText(Profiler::Get().GetMetrics(), Vector2(-Settings::Get().GetResolutionWidth() * 0.5f + 1.0f, Settings::Get().GetResolutionHeight() * 0.5f));

			m_rhiPipelineState->SetShader(m_shaderFont);
			m_rhiPipelineState->SetTexture(m_font->GetShaderResource());
			m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
			m_rhiPipelineState->SetIndexBuffer(m_font->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(m_font->GetVertexBuffer());
			m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
			m_shaderFont->BindBuffer(&Struct_Matrix_Vector4(m_wvp_baseOrthographic, m_font->GetColor()), 0);
			m_rhiPipelineState->Bind();

			m_rhiDevice->DrawIndexed(m_font->GetIndexCount(), 0, 0);
		}

		m_rhiDevice->EnableAlphaBlending(false);

		m_rhiDevice->EventEnd();
		PROFILE_FUNCTION_END();
	}

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4::Zero;
	}
}
