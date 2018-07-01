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
#include "Material.h"
#include "Mesh.h"
#include "Grid.h"
#include "Font.h"
#include "RI/RI_Shader.h"
#include "RI/RI_Texture.h"
#include "RI/D3D11/D3D11_Device.h"
#include "RI/D3D11/D3D11_RenderTexture.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "../Core/Context.h"
#include "../Core/EventSystem.h"
#include "../Scene/GameObject.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Scene/Components/Skybox.h"
#include "../Scene/Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../Resource/ResourceManager.h"
#include "../Profiling/Profiler.h"
#include "../Scene/TransformationGizmo.h"
#include "ShadowCascades.h"
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

	Renderer::Renderer(Context* context) : Subsystem(context)
	{
		m_skybox					= nullptr;
		m_camera					= nullptr;
		m_texEnvironment			= nullptr;
		m_lineRenderer				= nullptr;
		m_nearPlane					= 0.0f;
		m_farPlane					= 0.0f;
		m_graphics					= nullptr;
		m_flags						= 0;
		m_flags						|= Render_SceneGrid;
		m_flags						|= Render_Light;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVED, EVENT_HANDLER_VARIANT(Renderables_Acquire));
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Get Graphics subsystem
		m_graphics = m_context->GetSubsystem<RenderingDevice>();
		if (!m_graphics->IsInitialized())
		{
			LOG_ERROR("Renderer: Can't initialize, Graphics subsystem uninitialized.");
			return false;
		}

		// Get ResourceManager subsystem
		g_resourceMng	= m_context->GetSubsystem<ResourceManager>();
		g_physics		= m_context->GetSubsystem<Physics>();

		// Create G-Buffer
		m_gbuffer = make_unique<GBuffer>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		// Create fullscreen rectangle
		m_quad = make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT);

		// Get standard resource directories
		string shaderDirectory	= g_resourceMng->GetStandardResourceDirectory(Resource_Shader);
		string textureDirectory = g_resourceMng->GetStandardResourceDirectory(Resource_Texture);

		// Light shader
		m_shaderLight = make_unique<LightShader>();
		m_shaderLight->Load(shaderDirectory + "Light.hlsl", m_graphics);

		// Line shader
		m_shaderLine = make_unique<RI_Shader>(m_context);
		m_shaderLine->Compile(shaderDirectory + "Line.hlsl");
		m_shaderLine->SetInputLaytout(PositionColor);
		m_shaderLine->AddSampler(Texture_Sampler_Linear);
		m_shaderLine->AddBuffer(CB_Matrix_Matrix_Matrix, VertexShader);

		// Depth shader
		m_shaderLightDepth = make_unique<RI_Shader>(m_context);
		m_shaderLightDepth->Compile(shaderDirectory + "ShadowingDepth.hlsl");
		m_shaderLightDepth->SetInputLaytout(Position);
		m_shaderLightDepth->AddBuffer(CB_Matrix_Matrix_Matrix, VertexShader);

		// Grid shader
		m_shaderGrid = make_unique<RI_Shader>(m_context);
		m_shaderGrid->Compile(shaderDirectory + "Grid.hlsl");
		m_shaderGrid->SetInputLaytout(PositionColor);
		m_shaderGrid->AddSampler(Texture_Sampler_Anisotropic);
		m_shaderGrid->AddBuffer(CB_Matrix, VertexShader);

		// Font shader
		m_shaderFont = make_unique<RI_Shader>(m_context);
		m_shaderFont->Compile(shaderDirectory + "Font.hlsl");
		m_shaderFont->SetInputLaytout(PositionTexture);
		m_shaderFont->AddSampler(Texture_Sampler_Point);
		m_shaderFont->AddBuffer(CB_Matrix_Vector4, Global);

		// Texture shader
		m_shaderTexture = make_unique<RI_Shader>(m_context);
		m_shaderTexture->Compile(shaderDirectory + "Texture.hlsl");
		m_shaderTexture->SetInputLaytout(PositionTexture);
		m_shaderTexture->AddSampler(Texture_Sampler_Linear);
		m_shaderTexture->AddBuffer(CB_Matrix, VertexShader);

		// FXAA Shader
		m_shaderFXAA = make_unique<RI_Shader>(m_context);
		m_shaderFXAA->AddDefine("FXAA");
		m_shaderFXAA->Compile(shaderDirectory + "PostProcess.hlsl");
		m_shaderFXAA->SetInputLaytout(PositionTexture);
		m_shaderFXAA->AddSampler(Texture_Sampler_Point);
		m_shaderFXAA->AddSampler(Texture_Sampler_Bilinear);
		m_shaderFXAA->AddBuffer(CB_Matrix_Vector2, Global);

		// Sharpening shader
		m_shaderSharpening = make_unique<RI_Shader>(m_context);
		m_shaderSharpening->AddDefine("SHARPENING");
		m_shaderSharpening->Compile(shaderDirectory + "PostProcess.hlsl");
		m_shaderSharpening->SetInputLaytout(PositionTexture);
		m_shaderSharpening->AddSampler(Texture_Sampler_Point);
		m_shaderSharpening->AddSampler(Texture_Sampler_Bilinear);
		m_shaderSharpening->AddBuffer(CB_Matrix_Vector2, Global);

		// Blur shader
		m_shaderBlur = make_unique<RI_Shader>(m_context);
		m_shaderBlur->AddDefine("BLUR");
		m_shaderBlur->Compile(shaderDirectory + "PostProcess.hlsl");
		m_shaderBlur->SetInputLaytout(PositionTexture);
		m_shaderBlur->AddSampler(Texture_Sampler_Point);
		m_shaderBlur->AddSampler(Texture_Sampler_Bilinear);
		m_shaderBlur->AddBuffer(CB_Matrix_Vector2, Global);

		// Transformation Gizmo shader
		m_shaderTransformationGizmo = make_unique<RI_Shader>(m_context);
		m_shaderTransformationGizmo->Compile(shaderDirectory + "TransformationGizmo.hlsl");
		m_shaderTransformationGizmo->SetInputLaytout(PositionTextureTBN);
		m_shaderTransformationGizmo->AddBuffer(CB_Matrix_Vector3_Vector3, Global);

		// Shadowing shader (Shadow mapping & SSAO)
		m_shaderShadowing = make_unique<RI_Shader>(m_context);
		m_shaderShadowing->Compile(shaderDirectory + "Shadowing.hlsl");
		m_shaderShadowing->SetInputLaytout(PositionTexture);
		m_shaderShadowing->AddSampler(Texture_Sampler_Point, Texture_Address_Clamp); // Shadow mapping
		m_shaderShadowing->AddSampler(Texture_Sampler_Linear); // SSAO
		m_shaderShadowing->AddBuffer(CB_Shadowing, Global);

		// Create render textures (used for post-processing)
		m_renderTexSpare		= make_shared<D3D11_RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false, Texture_Format_R8G8B8A8_UNORM);
		m_renderTexShadowing	= make_shared<D3D11_RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false, Texture_Format_R32G32_FLOAT);
		m_renderTexFinalFrame	= make_shared<D3D11_RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false, Texture_Format_R8G8B8A8_UNORM);

		// Noise texture (used by SSAO shader)
		m_texNoiseMap = make_unique<RI_Texture>(m_context);
		m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
		m_texNoiseMap->SetType(TextureType_Normal);

		// Gizmo icons
		m_gizmoTexLightDirectional = make_unique<RI_Texture>(m_context);
		m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");
		m_gizmoTexLightDirectional->SetType(TextureType_Albedo);
		m_gizmoTexLightPoint = make_unique<RI_Texture>(m_context);
		m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");
		m_gizmoTexLightPoint->SetType(TextureType_Albedo);
		m_gizmoTexLightSpot = make_unique<RI_Texture>(m_context);
		m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
		m_gizmoTexLightSpot->SetType(TextureType_Albedo);
		m_gizmoRectLight = make_unique<Rectangle>(m_context);

		// Performance Metrics
		m_font = make_unique<Font>(m_context);
		string fontDir = g_resourceMng->GetStandardResourceDirectory(Resource_Font);
		m_font->SetSize(12);
		m_font->SetColor(Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		m_font->LoadFromFile(fontDir + "CalibriBold.ttf");

		// Scene grid
		m_grid = make_unique<Grid>(m_context);
		m_grid->BuildGrid();

		return true;
	}

	void Renderer::SetRenderTarget(void* renderTarget, bool clear /*= true*/)
	{
		auto renderTexture = (D3D11_RenderTexture*)renderTarget;
		if (renderTexture)
		{
			renderTexture->SetAsRenderTarget();
			if (clear) renderTexture->Clear(GetClearColor());
			return;
		}

		m_graphics->SetBackBufferAsRenderTarget();
		m_graphics->SetViewport();
		if (clear) m_graphics->Clear(GetClearColor());
	}

	void Renderer::SetRenderTarget(const shared_ptr<D3D11_RenderTexture>& renderTexture)
	{
		SetRenderTarget(renderTexture.get());
	}

	void* Renderer::GetFrame()
	{
		return (void*)m_renderTexFinalFrame->GetShaderResourceView();
	}

	void Renderer::Present()
	{
		m_graphics->Present();
	}

	void Renderer::Render()
	{
		if (!m_graphics || !m_graphics->IsInitialized())
			return;

		PROFILE_FUNCTION_BEGIN();
		Profiler::Get().Reset();

		// If there is a camera, render the scene
		if (m_camera)
		{
			m_mView				= m_camera->GetViewMatrix();
			m_mProjectionPersp	= m_camera->GetProjectionMatrix();
			m_mVP				= m_mView * m_mProjectionPersp;
			m_mProjectionOrtho	= Matrix::CreateOrthographicLH((float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
			m_mViewBase			= m_camera->GetBaseViewMatrix();
			m_nearPlane			= m_camera->GetNearPlane();
			m_farPlane			= m_camera->GetFarPlane();

			// If there is nothing to render clear to camera's color and present
			if (m_renderables.empty())
			{
				m_graphics->Clear(m_camera->GetClearColor());
				m_graphics->Present();
				return;
			}

			Pass_DepthDirectionalLight(m_directionalLight);

			Pass_GBuffer();

			Pass_PreLight(
				m_gbuffer->GetShaderResource(GBuffer_Target_Normal),	// IN:	Texture			- Normal
				m_gbuffer->GetShaderResource(GBuffer_Target_Depth),		// IN:	Texture			- Depth
				m_texNoiseMap->GetShaderResource(),						// IN:	Texture			- Normal noise
				m_renderTexSpare.get(),									// IN:	Render texture		
				m_renderTexShadowing.get()								// OUT: Render texture	- Shadowing (Shadow mapping + SSAO)
			);

			Pass_Light(
				m_renderTexShadowing->GetShaderResourceView(),	// IN:	Texture			- Shadowing (Shadow mapping + SSAO)
				m_renderTexSpare.get()							// OUT: Render texture	- Result
			);

			Pass_PostLight(
				m_renderTexSpare,		// IN:	Render texture - Deferred pass result
				m_renderTexFinalFrame	// OUT: Render texture - Result
			);
		}		
		else // If there is no camera, clear to black
		{
			m_graphics->Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		PROFILE_FUNCTION_END();
	}

	void Renderer::SetBackBufferSize(int width, int height)
	{
		SET_DISPLAY_SIZE(width, height);
		m_graphics->SetResolution(width, height);
		m_graphics->SetViewport((float)width, (float)height);
	}

	const RI_Viewport& Renderer::GetViewportBackBuffer()
	{
		return m_graphics->GetViewport();
	}

	void Renderer::SetResolutionInternal(int width, int height)
	{
		// Return if resolution already set
		if (GET_RESOLUTION.x == width && GET_RESOLUTION.y == height)
			return;

		// Return if resolution is invalid
		if (width <= 0 || height <= 0)
			return;

		SET_RESOLUTION(Vector2((float)width, (float)height));
		
		// Resize everything
		m_gbuffer.reset();
		m_gbuffer = make_unique<GBuffer>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_quad.reset();
		m_quad = make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT);

		m_renderTexSpare.reset();
		m_renderTexSpare = make_unique<D3D11_RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexShadowing.reset();
		m_renderTexShadowing = make_unique<D3D11_RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);

		m_renderTexFinalFrame.reset();
		m_renderTexFinalFrame = make_unique<D3D11_RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
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

	//= RENDERABLES ============================================================================================
	void Renderer::Renderables_Acquire(const Variant& renderables)
	{
		PROFILE_FUNCTION_BEGIN();

		Clear();
		auto renderablesVec = VARIANT_GET_FROM(vector<weak_ptr<GameObject>>, renderables);

		for (const auto& renderable : renderablesVec)
		{
			GameObject* gameObject = renderable.lock().get();
			if (!gameObject)
				continue;

			// Get renderables
			m_renderables.emplace_back(gameObject);

			// Get lights
			if (auto light = gameObject->GetComponent<Light>().lock())
			{
				m_lights.emplace_back(light.get());
				if (light->GetLightType() == LightType_Directional)
				{
					m_directionalLight = light.get();
				}
			}

			// Get skybox
			if (auto skybox = gameObject->GetComponent<Skybox>().lock())
			{
				m_skybox = skybox.get();
				m_lineRenderer = gameObject->GetComponent<LineRenderer>().lock().get(); // Hush hush...
			}

			// Get camera
			if (auto camera = gameObject->GetComponent<Camera>().lock())
			{
				m_camera = camera.get();
			}
		}
		Renderables_Sort(&m_renderables);

		PROFILE_FUNCTION_END();
	}

	void Renderer::Renderables_Sort(vector<GameObject*>* renderables)
	{
		if (renderables->size() <= 1)
			return;

		sort(renderables->begin(), renderables->end(),[](GameObject* a, GameObject* b)
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
	void Renderer::Pass_DepthDirectionalLight(Light* directionalLight)
	{
		if (!directionalLight || !directionalLight->GetCastShadows())
			return;

		PROFILE_FUNCTION_BEGIN();

		m_graphics->EventBegin("Pass_DepthDirectionalLight");
		m_graphics->EnableDepth(true);

		m_shaderLightDepth->Bind();

		auto cascades = directionalLight->GetShadowCascades();

		for (unsigned int i = 0; i < cascades->GetCascadeCount(); i++)
		{
			cascades->SetAsRenderTarget(i);

			for (const auto& gameObj : m_renderables)
			{
				// Get renderable and material
				Renderable* obj_renderable = gameObj->GetRenderable_PtrRaw();
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
					obj_geometry->Geometry_Bind();
					m_currentlyBoundGeometry = obj_geometry->GetResourceID();
				}

				// Skip meshes that don't cast shadows
				if (!obj_renderable->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (obj_material->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustum
				//if (!m_directionalLight->IsInViewFrustrum(obj_renderable))
					//continue;

				m_shaderLightDepth->Bind_Buffer(
					gameObj->GetTransform_PtrRaw()->GetWorldTransform() * directionalLight->ComputeViewMatrix() * cascades->ComputeProjectionMatrix(i)
				);
				m_shaderLightDepth->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());

				Profiler::Get().m_drawCalls++;
			}
		}

		// Reset pipeline state tracking
		m_currentlyBoundGeometry = 0;
		
		m_graphics->EnableDepth(false);
		m_graphics->EventEnd();

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_graphics)
			return;

		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_GBuffer");

		m_gbuffer->SetAsRenderTarget();
		m_gbuffer->Clear();
		
		for (auto gameObj : m_renderables)
		{
			// Get renderable and material
			Renderable* obj_renderable	= gameObj->GetRenderable_PtrRaw();
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
			m_graphics->SetCullMode(obj_material->GetCullMode());

			// Bind geometry
			if (m_currentlyBoundGeometry != obj_geometry->GetResourceID())
			{	
				obj_geometry->Geometry_Bind();
				m_currentlyBoundGeometry = obj_geometry->GetResourceID();
			}

			// Bind shader
			if (m_currentlyBoundShader != obj_shader->GetResourceID())
			{
				obj_shader->Bind();
				obj_shader->Bind_PerFrameBuffer(m_camera);
				m_currentlyBoundShader = obj_shader->GetResourceID();
			}

			// Bind material
			if (m_currentlyBoundMaterial != obj_material->GetResourceID())
			{
				obj_shader->Bind_PerMaterialBuffer(obj_material);
				obj_shader->Bind_Textures(obj_material->GetShaderResources());
				m_currentlyBoundMaterial = obj_material->GetResourceID();
			}

			// UPDATE PER OBJECT BUFFER
			auto mWorld	= gameObj->GetTransform_PtrRaw()->GetWorldTransform();
			obj_shader->Bind_PerObjectBuffer(mWorld, m_mView, m_mProjectionPersp);
		
			// render			
			obj_renderable->Render();

			Profiler::Get().m_drawCalls++;
			Profiler::Get().m_meshesRendered++;

		} // GAMEOBJECT/MESH ITERATION

		// Reset pipeline state tracking
		m_currentlyBoundGeometry	= 0;
		m_currentlyBoundShader		= 0;
		m_currentlyBoundMaterial	= 0;

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PreLight(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, void* inRenderTexure, void* outRenderTextureShadowing)
	{
		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_PreLight");

		m_quad->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		// Shadow mapping + SSAO
		Pass_Shadowing(inTextureNormal, inTextureDepth, inTextureNormalNoise, m_directionalLight, inRenderTexure);
		// Blur the shadows and the SSAO
		Pass_Blur(((D3D11_RenderTexture*)inRenderTexure)->GetShaderResourceView(), outRenderTextureShadowing, GET_RESOLUTION);

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_Light(void* inTextureShadowing, void* outRenderTexture)
	{
		if (!m_shaderLight->IsCompiled())
			return;

		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_Light");

		m_graphics->EnableDepth(false);

		// Set the deferred shader
		m_shaderLight->Set();

		// Set render target
		SetRenderTarget(outRenderTexture, false);

		// Update buffers
		m_shaderLight->UpdateMatrixBuffer(Matrix::Identity, m_mView, m_mViewBase, m_mProjectionPersp, m_mProjectionOrtho);
		m_shaderLight->UpdateMiscBuffer(m_lights, m_camera);

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Albedo));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Normal));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Specular));
		m_texArray.emplace_back(inTextureShadowing);
		m_texArray.emplace_back(nullptr); // previous frame for SSR
		m_texArray.emplace_back(m_skybox ? m_skybox->GetShaderResource() : nullptr);

		m_shaderLight->UpdateTextures(m_texArray);
		//=============================================================================

		m_shaderLight->Render(m_quad->GetIndexCount());

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PostLight(shared_ptr<D3D11_RenderTexture>& inRenderTextureFrame, shared_ptr<D3D11_RenderTexture>& outRenderTexture)
	{
		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_PostLight");

		m_quad->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		// FXAA
		Pass_FXAA(inRenderTextureFrame->GetShaderResourceView(), outRenderTexture.get());

		// Swap the render textures instead of swapping render targets (cheaper)
		outRenderTexture.swap(inRenderTextureFrame);	

		// SHARPENING
		Pass_Sharpening(inRenderTextureFrame->GetShaderResourceView(), outRenderTexture.get());
	
		Pass_DebugGBuffer();
		Pass_Debug();

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();
	}

	bool Renderer::Pass_DebugGBuffer()
	{
		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_DebugGBuffer");

		bool albedo		= RenderMode_IsSet(Render_Albedo);
		bool normal		= RenderMode_IsSet(Render_Normal);
		bool specular	= RenderMode_IsSet(Render_Specular);
		bool depth		= RenderMode_IsSet(Render_Depth);

		if (!albedo && !normal && !specular && !depth)
		{
			m_graphics->EventEnd();
			return false;
		}

		GBuffer_Texture_Type texType = GBuffer_Target_Unknown;
		if (albedo)
		{
			texType = GBuffer_Target_Albedo;
		}
		else if (normal)
		{
			texType = GBuffer_Target_Normal;
		}
		else if (specular)
		{
			texType = GBuffer_Target_Specular;			
		}
		else if (depth)
		{
			texType = GBuffer_Target_Depth;
		}

		// TEXTURE
		m_shaderTexture->Bind();
		m_shaderTexture->Bind_Buffer(Matrix::Identity * m_mViewBase * m_mProjectionOrtho, 0);
		m_shaderTexture->SetTexture(m_gbuffer->GetShaderResource(texType), 0);
		m_shaderTexture->DrawIndexed(m_quad->GetIndexCount());

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();

		return true;
	}

	void Renderer::Pass_Debug()
	{
		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_Debug");

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
				m_graphics->EventBegin("Lines");

				// Render
				m_lineRenderer->SetBuffer();
				m_shaderLine->Bind();
				m_shaderLine->Bind_Buffer(Matrix::Identity, m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix(), 0);
				m_shaderLine->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth), 0); // depth
				m_shaderLine->Draw(m_lineRenderer->GetVertexCount());

				m_graphics->EventEnd();
			}			
		}
		//============================================================================================================

		m_graphics->EnableAlphaBlending(true);

		// Grid
		if (m_flags & Render_SceneGrid)
		{
			m_graphics->EventBegin("Grid");

			m_grid->SetBuffer();
			m_shaderGrid->Bind();
			m_shaderGrid->Bind_Buffer(m_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix(), 0);
			m_shaderGrid->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth), 0);
			m_shaderGrid->DrawIndexed(m_grid->GetIndexCount());

			m_graphics->EventEnd();
		}

		// Light gizmo
		m_graphics->EventBegin("Gizmos");
		{
			if (m_flags & Render_Light)
			{
				m_graphics->EventBegin("Lights");
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

					RI_Texture* lightTex = nullptr;
					LightType type = light->GetGameObject_PtrRaw()->GetComponent<Light>().lock()->GetLightType();
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

					m_gizmoRectLight->SetBuffer();
					m_shaderTexture->Bind();
					m_shaderTexture->Bind_Buffer(Matrix::Identity * m_mViewBase * m_mProjectionOrtho, 0);
					m_shaderTexture->SetTexture(lightTex->GetShaderResource(), 0);
					m_shaderTexture->DrawIndexed(m_gizmoRectLight->GetIndexCount());
				}
				m_graphics->EventEnd();
			}

			// Transformation Gizmo
			/*
			m_graphics->EventBegin("Transformation");
			{
				TransformationGizmo* gizmo = m_camera->GetTransformationGizmo();
				gizmo->SetBuffers();
				m_shaderTransformationGizmo->Set();

				// X - Axis
				m_shaderTransformationGizmo->SetBuffer(gizmo->GetTransformationX() * m_mView * m_mProjectionPersp, Vector3::Right, Vector3::Zero, 0);
				m_shaderTransformationGizmo->DrawIndexed(gizmo->GetIndexCount());
				// Y - Axis
				m_shaderTransformationGizmo->SetBuffer(gizmo->GetTransformationY() * m_mView * m_mProjectionPersp, Vector3::Up, Vector3::Zero, 0);
				m_shaderTransformationGizmo->DrawIndexed(gizmo->GetIndexCount());
				// Z - Axis
				m_shaderTransformationGizmo->SetBuffer(gizmo->GetTransformationZ() * m_mView * m_mProjectionPersp, Vector3::Forward, Vector3::Zero, 0);
				m_shaderTransformationGizmo->DrawIndexed(gizmo->GetIndexCount());
			}
			m_graphics->EventEnd();
			*/
		}
		m_graphics->EventEnd();

		// Performance metrics
		if (m_flags & Render_PerformanceMetrics)
		{
			m_font->SetText(Profiler::Get().GetMetrics(), Vector2(-RESOLUTION_WIDTH * 0.5f + 1.0f, RESOLUTION_HEIGHT * 0.5f));
			m_font->SetBuffers();
			m_font->SetInputLayout();

			m_shaderFont->Bind();
			m_shaderFont->Bind_Buffer(Matrix::Identity * m_mViewBase * m_mProjectionOrtho, m_font->GetColor(), 0);
			m_shaderFont->SetTexture(m_font->GetShaderResource(), 0);
			m_shaderFont->DrawIndexed(m_font->GetIndexCount());
		}

		m_graphics->EnableAlphaBlending(false);

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_FXAA(void* texture, void* renderTarget)
	{
		m_graphics->EventBegin("Pass_FXAA");

		SetRenderTarget(renderTarget, false);
		m_shaderFXAA->Bind();
		m_shaderFXAA->Bind_Buffer(Matrix::Identity * m_mViewBase * m_mProjectionOrtho, GET_RESOLUTION, 0);
		m_shaderFXAA->SetTexture(texture, 0);
		m_shaderFXAA->DrawIndexed(m_quad->GetIndexCount());

		m_graphics->EventEnd();
	}

	void Renderer::Pass_Sharpening(void* texture, void* renderTarget)
	{
		m_graphics->EventBegin("Pass_Sharpening");

		SetRenderTarget(renderTarget, false);
		m_shaderSharpening->Bind();
		m_shaderSharpening->Bind_Buffer(Matrix::Identity * m_mViewBase * m_mProjectionOrtho, GET_RESOLUTION, 0);
		m_shaderSharpening->SetTexture(texture, 0);
		m_shaderSharpening->DrawIndexed(m_quad->GetIndexCount());

		m_graphics->EventEnd();
	}

	void Renderer::Pass_Blur(void* texture, void* renderTarget, const Vector2& blurScale)
	{
		m_graphics->EventBegin("Pass_Blur");

		SetRenderTarget(renderTarget, false);
		m_shaderBlur->Bind();
		m_shaderBlur->Bind_Buffer(Matrix::Identity * m_mViewBase * m_mProjectionOrtho, blurScale, 0);
		m_shaderBlur->SetTexture(texture, 0); // Shadows are alpha
		m_shaderBlur->DrawIndexed(m_quad->GetIndexCount());

		m_graphics->EventEnd();
	}

	void Renderer::Pass_Shadowing(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, Light* inDirectionalLight, void* outRenderTexture)
	{
		PROFILE_FUNCTION_BEGIN();
		m_graphics->EventBegin("Pass_Shadowing");

		// SHADOWING (Shadow mapping + SSAO)
		SetRenderTarget(outRenderTexture, false);

		// TEXTURES
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.emplace_back(inTextureNormal);
		m_texArray.emplace_back(inTextureDepth);
		m_texArray.emplace_back(inTextureNormalNoise);
		if (inDirectionalLight)
		{
			m_texArray.emplace_back(inDirectionalLight->GetShadowCascades()->GetShaderResource(0));
			m_texArray.emplace_back(inDirectionalLight->GetShadowCascades()->GetShaderResource(1));
			m_texArray.emplace_back(inDirectionalLight->GetShadowCascades()->GetShaderResource(2));
		}

		// BUFFER
		Matrix mvp_ortho		= Matrix::Identity * m_mViewBase * m_mProjectionOrtho;
		Matrix mvp_persp_inv	= (Matrix::Identity * m_mView * m_mProjectionPersp).Inverted();

		m_shaderShadowing->Bind();
		m_shaderShadowing->Bind_Buffer(
			mvp_ortho, 
			mvp_persp_inv, 
			m_mView, 
			m_mProjectionPersp,		
			GET_RESOLUTION, 
			inDirectionalLight,
			m_camera,
			0
		);
		m_shaderShadowing->SetTextures(m_texArray);
		m_shaderShadowing->DrawIndexed(m_quad->GetIndexCount());

		m_graphics->EventEnd();
		PROFILE_FUNCTION_END();
	}
	//=============================================================================================================

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4::Zero;
	}
}
