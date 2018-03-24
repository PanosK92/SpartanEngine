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
#include "GBuffer.h"
#include "Rectangle.h"
#include "Material.h"
#include "Mesh.h"
#include "Grid.h"
#include "Shader.h"
#include "../Core/Context.h"
#include "../Core/EventSystem.h"
#include "D3D11/D3D11RenderTexture.h"
#include "DeferredShaders/ShaderVariation.h"
#include "DeferredShaders/DeferredShader.h"
#include "../Scene/GameObject.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Scene/Components/Skybox.h"
#include "../Scene/Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../Resource/ResourceManager.h"
#include "../Font/Font.h"
#include "../Profiling/Profiler.h"
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
		m_renderedMeshesCount		= 0;
		m_renderedMeshesPerFrame	= 0;
		m_flags						= 0;
		m_flags						|= Render_SceneGrid;
		m_flags						|= Render_Light;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVED, EVENT_HANDLER_VARIANT(Pass_RenderableAcquisition));
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Get Graphics subsystem
		m_graphics = m_context->GetSubsystem<Graphics>();
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

		// Deferred shader
		m_shaderDeferred = make_unique<DeferredShader>();
		m_shaderDeferred->Load(shaderDirectory + "Deferred.hlsl", m_graphics);

		// Line shader
		m_shaderLine = make_unique<Shader>(m_context);
		m_shaderLine->Compile(shaderDirectory + "Line.hlsl");
		m_shaderLine->SetInputLaytout(PositionColor);
		m_shaderLine->AddSampler(Texture_Sampler_Linear);
		m_shaderLine->AddBuffer(CB_W_V_P, VertexShader);

		// Depth shader
		m_shaderDepth = make_unique<Shader>(m_context);
		m_shaderDepth->Compile(shaderDirectory + "Depth.hlsl");
		m_shaderDepth->SetInputLaytout(Position);
		m_shaderDepth->AddBuffer(CB_WVP, VertexShader);

		// Grid shader
		m_shaderGrid = make_unique<Shader>(m_context);
		m_shaderGrid->Compile(shaderDirectory + "Grid.hlsl");
		m_shaderGrid->SetInputLaytout(PositionColor);
		m_shaderGrid->AddSampler(Texture_Sampler_Anisotropic);
		m_shaderGrid->AddBuffer(CB_WVP, VertexShader);

		// Font shader
		m_shaderFont = make_unique<Shader>(m_context);
		m_shaderFont->Compile(shaderDirectory + "Font.hlsl");
		m_shaderFont->SetInputLaytout(PositionTexture);
		m_shaderFont->AddSampler(Texture_Sampler_Point);
		m_shaderFont->AddBuffer(CB_WVP_Color, Global);

		// Texture shader
		m_shaderTexture = make_unique<Shader>(m_context);
		m_shaderTexture->Compile(shaderDirectory + "Texture.hlsl");
		m_shaderTexture->SetInputLaytout(PositionTexture);
		m_shaderTexture->AddSampler(Texture_Sampler_Linear);
		m_shaderTexture->AddBuffer(CB_WVP, VertexShader);

		// FXAA Shader
		m_shaderFXAA = make_unique<Shader>(m_context);
		m_shaderFXAA->AddDefine("FXAA");
		m_shaderFXAA->Compile(shaderDirectory + "PostProcess.hlsl");
		m_shaderFXAA->SetInputLaytout(PositionTexture);
		m_shaderFXAA->AddSampler(Texture_Sampler_Point);
		m_shaderFXAA->AddSampler(Texture_Sampler_Bilinear);
		m_shaderFXAA->AddBuffer(CB_WVP_Resolution, Global);

		// Sharpening shader
		m_shaderSharpening = make_unique<Shader>(m_context);
		m_shaderSharpening->AddDefine("SHARPENING");
		m_shaderSharpening->Compile(shaderDirectory + "PostProcess.hlsl");
		m_shaderSharpening->SetInputLaytout(PositionTexture);
		m_shaderSharpening->AddSampler(Texture_Sampler_Point);
		m_shaderSharpening->AddSampler(Texture_Sampler_Bilinear);
		m_shaderSharpening->AddBuffer(CB_WVP_Resolution, Global);

		// Blur shader
		m_shaderBlur = make_unique<Shader>(m_context);
		m_shaderBlur->AddDefine("BLUR");
		m_shaderBlur->Compile(shaderDirectory + "PostProcess.hlsl");
		m_shaderBlur->SetInputLaytout(PositionTexture);
		m_shaderBlur->AddSampler(Texture_Sampler_Point);
		m_shaderBlur->AddSampler(Texture_Sampler_Bilinear);
		m_shaderBlur->AddBuffer(CB_WVP_Resolution, Global);

		// Shadowing shader (Shadow mapping & SSAO)
		m_shaderShadowing = make_unique<Shader>(m_context);
		m_shaderShadowing->Compile(shaderDirectory + "Shadowing.hlsl");
		m_shaderShadowing->SetInputLaytout(PositionTexture);
		m_shaderShadowing->AddSampler(Texture_Sampler_Point, Texture_Address_Clamp); // Shadow mapping
		m_shaderShadowing->AddSampler(Texture_Sampler_Linear); // SSAO
		m_shaderShadowing->AddBuffer(CB_Shadowing, Global);

		// Create render textures (used for post-processing)
		m_renderTexSpare = make_shared<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
		m_renderTexShadowing = make_shared<D3D11RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);
		m_renderTexFinalFrame = make_shared<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		// Noise texture (used by SSAO shader)
		m_texNoiseMap = make_unique<Texture>(m_context);
		m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
		m_texNoiseMap->SetType(TextureType_Normal);

		// Gizmo icons
		m_gizmoTexLightDirectional = make_unique<Texture>(m_context);
		m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");
		m_gizmoTexLightDirectional->SetType(TextureType_Albedo);
		m_gizmoTexLightPoint = make_unique<Texture>(m_context);
		m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");
		m_gizmoTexLightPoint->SetType(TextureType_Albedo);
		m_gizmoTexLightSpot = make_unique<Texture>(m_context);
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
		D3D11RenderTexture* renderTexture = (D3D11RenderTexture*)renderTarget;
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

	void Renderer::SetRenderTarget(const shared_ptr<D3D11RenderTexture>& renderTexture)
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
		if (!m_graphics)
			return;

		if (!m_graphics->IsInitialized())
			return;

		//= METRICS ==============
		PROFILE_FUNCTION_BEGIN();
		m_renderedMeshesCount = 0;
		//========================

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

			Pass_PreDeferred(
				m_gbuffer->GetShaderResource(GBuffer_Target_Normal),	// IN:	Texture			- Normal
				m_gbuffer->GetShaderResource(GBuffer_Target_Depth),		// IN:	Texture			- Depth
				m_texNoiseMap->GetShaderResource(),						// IN:	Texture			- Normal noise
				m_renderTexSpare.get(),									// IN:	Render texture		
				m_renderTexShadowing.get()								// OUT: Render texture	- Shadowing (Shadow mapping + SSAO)
			);

			Pass_Deferred(
				m_renderTexShadowing->GetShaderResourceView(),	// IN:	Texture			- Shadowing (Shadow mapping + SSAO)
				m_renderTexSpare.get()							// OUT: Render texture	- Result
			);

			Pass_PostDeferred(
				m_renderTexSpare,		// IN:	Render texture - Deferred pass result
				m_renderTexFinalFrame	// OUT: Render texture - Result
			);
		}		
		else // If there is no camera, clear to black
		{
			m_graphics->Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		//= METRICS =====================================
		PROFILE_FUNCTION_END();
		m_renderedMeshesPerFrame = m_renderedMeshesCount;
		//===============================================
	}

	void Renderer::SetResolutionBackBuffer(int width, int height)
	{
		m_graphics->SetResolution(width, height);
	}

	void Renderer::SetViewportBackBuffer(float width, float height)
	{
		m_graphics->SetViewport(width, height);
	}

	Vector4 Renderer::GetViewportBackBuffer()
	{
		D3D11_VIEWPORT* viewport = (D3D11_VIEWPORT*)m_graphics->GetViewport();
		return Vector4(viewport->TopLeftX, viewport->TopLeftY, viewport->Width, viewport->Height);
	}

	void Renderer::SetResolution(int width, int height)
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
		m_renderTexSpare = make_unique<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexShadowing.reset();
		m_renderTexShadowing = make_unique<D3D11RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);

		m_renderTexFinalFrame.reset();
		m_renderTexFinalFrame = make_unique<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
	}

	void Renderer::SetViewport(int width, int height)
	{
		SET_VIEWPORT(Vector4(GET_VIEWPORT.x, GET_VIEWPORT.y, (float)width, (float)height));
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

	//= PASSES =================================================================================================
	void Renderer::Pass_RenderableAcquisition(const Variant& renderables)
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
			m_renderables.push_back(gameObject);

			// Get lights
			if (auto light = gameObject->GetComponent<Light>().lock())
			{
				m_lights.push_back(light.get());
				if (light->GetLightType() == LightType_Directional)
				{
					m_directionalLight = light.get();
				}
			}

			// Get skybox
			if (auto skybox = gameObject->GetComponent<Skybox>().lock())
			{
				m_skybox		= skybox.get();
				m_lineRenderer	= gameObject->GetComponent<LineRenderer>().lock().get(); // Hush hush...
			}

			// Get camera
			if (auto camera = gameObject->GetComponent<Camera>().lock())
			{
				m_camera = camera.get();
			}
		}

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_DepthDirectionalLight(Light* directionalLight)
	{
		if (!directionalLight || !directionalLight->GetCastShadows())
			return;

		PROFILE_FUNCTION_BEGIN();

		m_graphics->EnableDepth(true);
		m_shaderDepth->Set();

		for (int cascadeIndex = 0; cascadeIndex < directionalLight->GetShadowCascadeCount(); cascadeIndex++)
		{
			// Set appropriate shadow map as render target
			directionalLight->SetShadowCascadeAsRenderTarget(cascadeIndex);

			Matrix mViewLight		= directionalLight->GetViewMatrix();
			Matrix mProjectionLight = directionalLight->GetOrthographicProjectionMatrix(cascadeIndex);

			for (const auto& gameObj : m_renderables)
			{
				Renderable* renderable	= gameObj->GetRenderableRef();
				Material* material		= renderable	? renderable->GetMaterial_Ref()	: nullptr;
				Mesh* mesh				= renderable	? renderable->GetMesh_Ref()		: nullptr;

				// Make sure we have everything
				if (!mesh || !renderable || !material)
					continue;

				// Skip meshes that don't cast shadows
				if (!renderable->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustrum
				//if (!m_directionalLight->IsInViewFrustrum(meshFilter))
					//continue;

				if (renderable->SetBuffers())
				{
					m_shaderDepth->SetBuffer(gameObj->GetTransformRef()->GetWorldTransform(), mViewLight, mProjectionLight, 0);
					m_shaderDepth->DrawIndexed(mesh->GetIndexCount());
				}
			}
		}

		m_graphics->EnableDepth(false);

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_graphics)
			return;

		PROFILE_FUNCTION_BEGIN();

		m_gbuffer->SetAsRenderTarget();
		m_gbuffer->Clear();

		vector<weak_ptr<IResource>> materials	= g_resourceMng->GetResourcesByType(Resource_Material);
		vector<weak_ptr<IResource>> shaders		= g_resourceMng->GetResourcesByType(Resource_Shader);

		for (const auto& shaderIt : shaders) // SHADER ITERATION
		{
			ShaderVariation* shader = (ShaderVariation*)shaderIt.lock().get();
			if (!shader)
				continue;

			// Set the shader and update frame buffer
			shader->Set();
			shader->UpdatePerFrameBuffer(m_camera);

			for (const auto& materialIt : materials) // MATERIAL ITERATION
			{
				Material* material = (Material*)materialIt.lock().get();
				if (!material)
					continue;

				// Continue only if the material at hand happens to use the already set shader
				if (!material->GetShader().expired() && (material->GetShader().lock()->GetResourceID() != shader->GetResourceID()))
					continue;

				// UPDATE PER MATERIAL BUFFER
				shader->UpdatePerMaterialBuffer(material);

				// Order the textures they way the shader expects them
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Albedo));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Roughness));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Metallic));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Normal));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Height));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Occlusion));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Emission));
				m_textures.emplace_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Mask));

				// UPDATE TEXTURE BUFFER
				shader->UpdateTextures(m_textures);
				//==================================================================================

				for (const auto& gameObj : m_renderables) // GAMEOBJECT/MESH ITERATION
				{
					//= Get all that we need =========================================================
					Renderable* renderable	= gameObj->GetRenderableRef();
					Mesh* objMesh			= renderable	? renderable->GetMesh_Ref()		: nullptr;
					Material* objMaterial	= renderable	? renderable->GetMaterial_Ref()	: nullptr;
					auto mWorld				= gameObj->GetTransformRef()->GetWorldTransform();
					//================================================================================

					// skip objects that are missing required components
					if (!objMesh || !renderable || !objMaterial)
						continue;

					// skip objects that use a different material
					if (material->GetResourceID() != objMaterial->GetResourceID())
						continue;

					// skip transparent objects (for now)
					if (objMaterial->GetOpacity() < 1.0f)
						continue;

					// skip objects outside of the view frustrum
					if (!m_camera->IsInViewFrustrum(renderable))
						continue;

					// UPDATE PER OBJECT BUFFER
					shader->UpdatePerObjectBuffer(mWorld, m_mView, m_mProjectionPersp);

					// Set mesh buffer
					if (renderable->HasMesh() && renderable->SetBuffers())
					{
						// Set face culling (changes only if required)
						m_graphics->SetCullMode(objMaterial->GetCullMode());

						// Render the mesh, finally!				
						renderable->Render(objMesh->GetIndexCount());

						m_renderedMeshesCount++;
					}
				} // GAMEOBJECT/MESH ITERATION

				m_textures.clear();

			} // MATERIAL ITERATION
		} // SHADER ITERATION

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PreDeferred(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, void* inRenderTexure, void* outRenderTextureShadowing)
	{
		PROFILE_FUNCTION_BEGIN();

		m_quad->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		// Shadow mapping + SSAO
		Pass_Shadowing(inTextureNormal, inTextureDepth, inTextureNormalNoise, m_directionalLight, inRenderTexure);
		// Blur the shadows and the ssao
		Pass_Blur(((D3D11RenderTexture*)inRenderTexure)->GetShaderResourceView(), outRenderTextureShadowing, GET_RESOLUTION);

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_Deferred(void* inTextureShadowing, void* outRenderTexture)
	{
		if (!m_shaderDeferred->IsCompiled())
			return;

		PROFILE_FUNCTION_BEGIN();

		// Set the deferred shader
		m_shaderDeferred->Set();

		// Set render target
		SetRenderTarget(outRenderTexture, false);

		// Update buffers
		m_shaderDeferred->UpdateMatrixBuffer(Matrix::Identity, m_mView, m_mViewBase, m_mProjectionPersp, m_mProjectionOrtho);
		m_shaderDeferred->UpdateMiscBuffer(m_lights, m_camera);

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Albedo));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Normal));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Specular));
		m_texArray.emplace_back(inTextureShadowing);
		m_texArray.emplace_back(nullptr); // previous frame for SSR
		m_texArray.emplace_back(m_skybox ? m_skybox->GetShaderResource() : nullptr);

		m_shaderDeferred->UpdateTextures(m_texArray);
		//=============================================================================

		m_shaderDeferred->Render(m_quad->GetIndexCount());

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PostDeferred(shared_ptr<D3D11RenderTexture>& inRenderTextureFrame, shared_ptr<D3D11RenderTexture>& outRenderTexture)
	{
		PROFILE_FUNCTION_BEGIN();

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

		PROFILE_FUNCTION_END();
	}

	bool Renderer::Pass_DebugGBuffer()
	{
		PROFILE_FUNCTION_BEGIN();

		bool albedo		= RenderMode_IsSet(Render_Albedo);
		bool normal		= RenderMode_IsSet(Render_Normal);
		bool specular	= RenderMode_IsSet(Render_Specular);
		bool depth		= RenderMode_IsSet(Render_Depth);

		if (!albedo && !normal && !specular && !depth)
			return false;

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
		m_shaderTexture->Set();
		m_shaderTexture->SetBuffer(Matrix::Identity, m_mViewBase, m_mProjectionOrtho, 0);
		m_shaderTexture->SetTexture(m_gbuffer->GetShaderResource(texType), 0);
		m_shaderTexture->DrawIndexed(m_quad->GetIndexCount());

		PROFILE_FUNCTION_END();

		return true;
	}

	void Renderer::Pass_Debug()
	{
		PROFILE_FUNCTION_BEGIN();

		//= PRIMITIVES ===================================================================================
		// Anything that is a bunch of vertices (doesn't have a vertex and and index buffer) get's rendered here
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
				for (const auto& gameObject : m_renderables)
				{
					if (auto renderable = gameObject->GetRenderableRef())
					{
						m_lineRenderer->AddBoundigBox(renderable->GetBoundingBoxTransformed(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}

			if (m_lineRenderer->GetVertexCount() != 0)
			{
				// Render
				m_lineRenderer->SetBuffer();
				m_shaderLine->Set();
				m_shaderLine->SetBuffer(Matrix::Identity, m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix(), 0);
				m_shaderLine->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth), 0); // depth
				m_shaderLine->Draw(m_lineRenderer->GetVertexCount());
			}
		}
		//============================================================================================================

		m_graphics->EnableAlphaBlending(true);

		// Grid
		if (m_flags & Render_SceneGrid)
		{
			m_grid->SetBuffer();
			m_shaderGrid->Set();
			m_shaderGrid->SetBuffer(m_grid->ComputeWorldMatrix(m_camera->GetTransform()), m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix(), 0);
			m_shaderGrid->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth), 0);
			m_shaderGrid->DrawIndexed(m_grid->GetIndexCount());
		}

		// Light gizmo
		if (m_flags & Render_Light)
		{
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

				Texture* lightTex = nullptr;
				LightType type = light->GetGameObject_Ref()->GetComponent<Light>().lock()->GetLightType();
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
				m_shaderTexture->Set();
				m_shaderTexture->SetBuffer(Matrix::Identity, m_mViewBase, m_mProjectionOrtho, 0);
				m_shaderTexture->SetTexture((ID3D11ShaderResourceView*)lightTex->GetShaderResource(), 0);
				m_shaderTexture->DrawIndexed(m_gizmoRectLight->GetIndexCount());
			}
		}

		// Performance metrics
		if (m_flags & Render_PerformanceMetrics)
		{
			m_font->SetText(Profiler::Get().GetMetrics(), Vector2(-RESOLUTION_WIDTH * 0.5f + 1.0f, RESOLUTION_HEIGHT * 0.5f));
			m_font->SetBuffer();

			m_shaderFont->Set();
			m_shaderFont->SetBuffer(Matrix::Identity, m_mViewBase, m_mProjectionOrtho, m_font->GetColor(), 0);
			m_shaderFont->SetTexture((ID3D11ShaderResourceView*)m_font->GetShaderResource(), 0);
			m_shaderFont->DrawIndexed(m_font->GetIndexCount());
		}

		m_graphics->EnableAlphaBlending(false);

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_FXAA(void* texture, void* renderTarget)
	{
		SetRenderTarget(renderTarget, false);
		m_shaderFXAA->Set();
		m_shaderFXAA->SetBuffer(Matrix::Identity, m_mViewBase, m_mProjectionOrtho, GET_RESOLUTION, 0);
		m_shaderFXAA->SetTexture(texture, 0);
		m_shaderFXAA->DrawIndexed(m_quad->GetIndexCount());
	}

	void Renderer::Pass_Sharpening(void* texture, void* renderTarget)
	{
		SetRenderTarget(renderTarget, false);
		m_shaderSharpening->Set();
		m_shaderSharpening->SetBuffer(Matrix::Identity, m_mViewBase, m_mProjectionOrtho, GET_RESOLUTION, 0);
		m_shaderSharpening->SetTexture(texture, 0);
		m_shaderSharpening->DrawIndexed(m_quad->GetIndexCount());
	}

	void Renderer::Pass_Blur(void* texture, void* renderTarget, const Vector2& blurScale)
	{
		SetRenderTarget(renderTarget, false);
		m_shaderBlur->Set();
		m_shaderBlur->SetBuffer(Matrix::Identity, m_mViewBase, m_mProjectionOrtho, blurScale, 0);
		m_shaderBlur->SetTexture(texture, 0); // Shadows are alpha
		m_shaderBlur->DrawIndexed(m_quad->GetIndexCount());
	}

	void Renderer::Pass_Shadowing(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, Light* inDirectionalLight, void* outRenderTexture)
	{
		if (!inDirectionalLight)
			return;

		PROFILE_FUNCTION_BEGIN();

		// SHADOWING (Shadow mapping + SSAO)
		SetRenderTarget(outRenderTexture, false);

		// TEXTURES
		vector<void*> textures;
		textures.push_back(inTextureNormal);
		textures.push_back(inTextureDepth);
		textures.push_back(inTextureNormalNoise);
		if (inDirectionalLight)
		{
			for (int i = 0; i < inDirectionalLight->GetShadowCascadeCount(); i++)
			{
				Cascade* shadowMap = inDirectionalLight->GetShadowCascade(i).lock().get();
				textures.push_back(shadowMap ? shadowMap->GetShaderResource() : nullptr);
			}
		}

		// BUFFER
		Matrix mvp_ortho		= Matrix::Identity * m_mViewBase * m_mProjectionOrtho;
		Matrix mvp_persp_inv	= (Matrix::Identity * m_mView * m_mProjectionPersp).Inverted();

		m_shaderShadowing->Set();
		m_shaderShadowing->SetBuffer(
			mvp_ortho, 
			mvp_persp_inv, 
			m_mView, 
			m_mProjectionPersp,		
			GET_RESOLUTION, 
			inDirectionalLight,
			m_camera,
			0
		);
		m_shaderShadowing->SetTextures(textures);

		m_shaderShadowing->DrawIndexed(m_quad->GetIndexCount());

		PROFILE_FUNCTION_END();
	}
	//=============================================================================================================

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4::Zero;
	}
}
