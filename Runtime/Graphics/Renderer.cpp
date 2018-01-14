/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ===============================
#include "Renderer.h"
#include "Gbuffer.h"
#include "Rectangle.h"
#include "Material.h"
#include "Mesh.h"
#include "Grid.h"
#include "Shader.h"
#include "D3D11/D3D11RenderTexture.h"
#include "DeferredShaders/ShaderVariation.h"
#include "DeferredShaders/DeferredShader.h"
#include "../Scene/GameObject.h"
#include "../Core/Context.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Skybox.h"
#include "../Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventSystem.h"
#include "../Resource/ResourceManager.h"
#include "../Font/Font.h"
#include "../Profiling/PerformanceProfiler.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	Renderer::Renderer(Context* context) : Subsystem(context)
	{
		m_skybox			= nullptr;
		m_camera			= nullptr;
		m_texEnvironment	= nullptr;
		m_lineRenderer		= nullptr;
		m_nearPlane			= 0.0f;
		m_farPlane			= 0.0f;
		m_resourceMng		= nullptr;
		m_graphics			= nullptr;
		m_renderFlags		= 0;
		m_renderFlags		|= Render_Grid;
		m_renderFlags		|= Render_Light;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_UPDATED, EVENT_HANDLER_VARIANT(AcquireRenderables));
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
		m_resourceMng = m_context->GetSubsystem<ResourceManager>();

		// Create G-Buffer
		m_gbuffer = make_unique<GBuffer>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		// Create fullscreen rectangle
		m_quad = make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT);

		// Get standard resource directories
		string shaderDirectory = m_resourceMng->GetStandardResourceDirectory(Resource_Shader);
		string textureDirectory = m_resourceMng->GetStandardResourceDirectory(Resource_Texture);

		// Deferred shader
		m_shaderDeferred = make_unique<DeferredShader>();
		m_shaderDeferred->Load(shaderDirectory + "Deferred.hlsl", m_graphics);

		// Line shader
		m_shaderLine = make_unique<Shader>(m_context);
		m_shaderLine->Load(shaderDirectory + "Line.hlsl");
		m_shaderLine->SetInputLaytout(PositionColor);
		m_shaderLine->AddSampler(Linear_Sampler);
		m_shaderLine->AddBuffer(W_V_P, VertexShader);

		// Depth shader
		m_shaderDepth = make_unique<Shader>(m_context);
		m_shaderDepth->Load(shaderDirectory + "Depth.hlsl");
		m_shaderDepth->SetInputLaytout(Position);
		m_shaderDepth->AddBuffer(WVP, VertexShader);

		// Grid shader
		m_shaderGrid = make_unique<Shader>(m_context);
		m_shaderGrid->Load(shaderDirectory + "Grid.hlsl");
		m_shaderGrid->SetInputLaytout(PositionColor);
		m_shaderGrid->AddSampler(Anisotropic_Sampler);
		m_shaderGrid->AddBuffer(WVP, VertexShader);

		// Font shader
		m_shaderFont = make_unique<Shader>(m_context);
		m_shaderFont->Load(shaderDirectory + "Font.hlsl");
		m_shaderFont->SetInputLaytout(PositionTexture);
		m_shaderFont->AddSampler(Point_Sampler);
		m_shaderFont->AddBuffer(WVP_Color, Global);

		// Texture shader
		m_shaderTexture = make_unique<Shader>(m_context);
		m_shaderTexture->Load(shaderDirectory + "Texture.hlsl");
		m_shaderTexture->SetInputLaytout(PositionTexture);
		m_shaderTexture->AddSampler(Anisotropic_Sampler);
		m_shaderTexture->AddBuffer(WVP, VertexShader);

		// FXAA Shader
		m_shaderFXAA = make_unique<Shader>(m_context);
		m_shaderFXAA->AddDefine("FXAA");
		m_shaderFXAA->Load(shaderDirectory + "PostProcess.hlsl");
		m_shaderFXAA->SetInputLaytout(PositionTexture);
		m_shaderFXAA->AddSampler(Anisotropic_Sampler);
		m_shaderFXAA->AddSampler(Linear_Sampler);
		m_shaderFXAA->AddBuffer(WVP_Resolution, Global);

		// SSAO Shader
		m_shaderSSAO = make_unique<Shader>(m_context);
		m_shaderSSAO->Load(shaderDirectory + "SSAO.hlsl");
		m_shaderSSAO->SetInputLaytout(PositionTexture);
		m_shaderSSAO->AddSampler(Anisotropic_Sampler);
		m_shaderSSAO->AddSampler(Linear_Sampler);
		m_shaderSSAO->AddBuffer(WVP_WVPInverse_Resolution_Planes, Global);

		// Sharpening shader
		m_shaderSharpening = make_unique<Shader>(m_context);
		m_shaderSharpening->AddDefine("SHARPENING");
		m_shaderSharpening->Load(shaderDirectory + "PostProcess.hlsl");
		m_shaderSharpening->SetInputLaytout(PositionTexture);
		m_shaderSharpening->AddSampler(Anisotropic_Sampler);
		m_shaderSharpening->AddSampler(Linear_Sampler);
		m_shaderSharpening->AddBuffer(WVP_Resolution, Global);

		// Blur shader
		m_shaderBlur = make_unique<Shader>(m_context);
		m_shaderBlur->AddDefine("BLUR");
		m_shaderBlur->Load(shaderDirectory + "PostProcess.hlsl");
		m_shaderBlur->SetInputLaytout(PositionTexture);
		m_shaderBlur->AddSampler(Anisotropic_Sampler);
		m_shaderBlur->AddSampler(Linear_Sampler);
		m_shaderBlur->AddBuffer(WVP_Resolution, Global);

		// Create render textures (used for post-processing)
		m_renderTexPing = make_shared<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
		m_renderTexPong = make_shared<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
		m_renderTexSSAO = make_shared<D3D11RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);
		m_renderTexSSAOBlurred = make_shared<D3D11RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);
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
		string fontDir = m_resourceMng->GetStandardResourceDirectory(Resource_Font);
		m_font->SetSize(12);
		m_font->SetColor(Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		m_font->LoadFromFile(fontDir + "CalibriBold.ttf");

		// Scene grid
		m_grid = make_unique<Grid>(m_context);
		m_grid->BuildGrid();

		return true;
	}

	void Renderer::SetRenderTarget(D3D11RenderTexture* renderTexture)
	{
		if (renderTexture)
		{
			renderTexture->SetAsRenderTarget();
			renderTexture->Clear(GetClearColor());
			return;
		}

		m_graphics->SetBackBufferAsRenderTarget();
		m_graphics->SetViewport();
		m_graphics->Clear(GetClearColor());
	}

	void Renderer::SetRenderTarget(shared_ptr<D3D11RenderTexture> renderTexture)
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

		PerformanceProfiler::RenderingStarted();

		// If there is no camera, clear to black and present
		if (!m_camera)
		{
			m_graphics->Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			m_graphics->Present();
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_renderables.empty())
		{
			m_graphics->Clear(m_camera->GetClearColor());
			m_graphics->Present();
			return;
		}

		DirectionalLightDepthPass();
		GBufferPass();
		PreDeferredPass();
		DeferredPass();
		PostDeferredPass();

		PerformanceProfiler::RenderingFinished();
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

		m_renderTexPing.reset();
		m_renderTexPing = make_unique<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexPong.reset();
		m_renderTexPong = make_unique<D3D11RenderTexture>(m_graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexSSAO.reset();
		m_renderTexSSAO = make_unique<D3D11RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);

		m_renderTexSSAOBlurred.reset();
		m_renderTexSSAOBlurred = make_unique<D3D11RenderTexture>(m_graphics, int(RESOLUTION_WIDTH * 0.5f), int(RESOLUTION_HEIGHT * 0.5f), false);

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

		m_directionalLight = nullptr;
		m_skybox = nullptr;
		m_lineRenderer = nullptr;
		m_camera = nullptr;
	}

	//= HELPER FUNCTIONS ==============================================================================================
	void Renderer::AcquireRenderables(Variant renderables)
	{
		Clear();
		auto renderablesVec = VariantToVector<weak_ptr<GameObject>>(renderables);

		for (const auto& renderable : renderablesVec)
		{
			GameObject* gameObject = renderable.lock().get();
			if (!gameObject)
				continue;

			// Get meshes
			if (gameObject->GetMeshRenderer() && gameObject->GetMeshFilter())
			{
				m_renderables.push_back(renderable);
			}

			// Get lights
			if (auto light = gameObject->GetComponent<Light>().lock())
			{
				m_lights.push_back(light.get());
				if (light->GetLightType() == Directional)
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
				m_camera		= camera.get();
				mView			= m_camera->GetViewMatrix();
				mProjection		= m_camera->GetProjectionMatrix();
				mViewProjection = mView * mProjection;
				mOrthographicProjection = Matrix::CreateOrthographicLH((float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
				mBaseView		= m_camera->GetBaseViewMatrix();
				m_nearPlane		= m_camera->GetNearPlane();
				m_farPlane		= m_camera->GetFarPlane();
			}
		}
	}

	void Renderer::DirectionalLightDepthPass()
	{
		if (!m_directionalLight || m_directionalLight->GetShadowQuality() == No_Shadows)
			return;

		m_graphics->EnableDepth(true);

		//m_graphics->SetCullMode(CullFront);
		m_shaderDepth->Set();

		for (int cascadeIndex = 0; cascadeIndex < m_directionalLight->GetShadowCascadeCount(); cascadeIndex++)
		{
			// Set appropriate shadow map as render target
			m_directionalLight->SetShadowCascadeAsRenderTarget(cascadeIndex);

			Matrix mViewLight		= m_directionalLight->GetViewMatrix();
			Matrix mProjectionLight = m_directionalLight->GetOrthographicProjectionMatrix(cascadeIndex);

			for (const auto& gameObjWeak : m_renderables)
			{
				auto gameObj = gameObjWeak.lock();
				if (!gameObj)
					continue;

				MeshFilter* meshFilter		= gameObj->GetMeshFilter();
				MeshRenderer* meshRenderer	= gameObj->GetMeshRenderer();
				auto material				= meshRenderer->GetMaterial();
				auto mesh					= meshFilter->GetMesh();

				// Make sure we have everything
				if (mesh.expired() || !meshFilter || !meshRenderer || material.expired())
					continue;

				// Skip meshes that don't cast shadows
				if (!meshRenderer->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material.lock()->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustrum
				//if (!m_directionalLight->IsInViewFrustrum(meshFilter))
					//continue;

				if (meshFilter->SetBuffers())
				{
					m_shaderDepth->SetBuffer(gameObj->GetTransform()->GetWorldTransform(), mViewLight, mProjectionLight, 0);
					m_shaderDepth->DrawIndexed(mesh.lock()->GetIndexCount());
				}
			}
		}

		m_graphics->EnableDepth(false);
	}

	void Renderer::GBufferPass()
	{
		if (!m_graphics)
			return;

		m_gbuffer->SetAsRenderTarget();
		m_gbuffer->Clear();

		vector<weak_ptr<Resource>> materials = m_resourceMng->GetResourcesByType(Resource_Material);
		vector<weak_ptr<Resource>> shaders = m_resourceMng->GetResourcesByType(Resource_Shader);

		for (const auto& shaderIt : shaders) // SHADER ITERATION
		{
			ShaderVariation* shader = (ShaderVariation*)shaderIt.lock().get();
			if (!shader)
				continue;

			// Set the shader and update frame buffer
			shader->Set();
			shader->UpdatePerFrameBuffer(m_directionalLight, m_camera);

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
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Albedo));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Roughness));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Metallic));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Normal));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Height));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Occlusion));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Emission));
				m_textures.push_back((ID3D11ShaderResourceView*)material->GetShaderResource(TextureType_Mask));

				if (m_directionalLight)
				{
					for (int i = 0; i < m_directionalLight->GetShadowCascadeCount(); i++)
					{
						auto shadowMap = m_directionalLight->GetShadowCascade(i).lock();
						m_textures.push_back(shadowMap ? shadowMap->GetShaderResource() : nullptr);
					}
				}
				else
				{
					m_textures.push_back(nullptr);
					m_textures.push_back(nullptr);
					m_textures.push_back(nullptr);
				}

				// UPDATE TEXTURE BUFFER
				shader->UpdateTextures(m_textures);
				//==================================================================================

				for (const auto& gameObjWeak : m_renderables) // GAMEOBJECT/MESH ITERATION
				{
					auto gameObj = gameObjWeak.lock();
					if (!gameObj)
						continue;

					//= Get all that we need ==================================================
					MeshFilter* meshFilter		= gameObj->GetMeshFilter();
					MeshRenderer* meshRenderer	= gameObj->GetMeshRenderer();
					auto objMesh				= meshFilter->GetMesh().lock();
					auto objMaterial			= meshRenderer->GetMaterial().lock();
					auto mWorld					= gameObj->GetTransform()->GetWorldTransform();
					//=========================================================================

					// skip objects that are missing required components
					if (!meshFilter || !objMesh || !meshRenderer || !objMaterial)
						continue;

					// skip objects that use a different material
					if (material->GetResourceID() != objMaterial->GetResourceID())
						continue;

					// skip transparent objects (for now)
					if (objMaterial->GetOpacity() < 1.0f)
						continue;

					// skip objects outside of the view frustrum
					if (!m_camera->IsInViewFrustrum(meshFilter))
						continue;

					// UPDATE PER OBJECT BUFFER
					shader->UpdatePerObjectBuffer(mWorld, mView, mProjection, meshRenderer->GetReceiveShadows());

					// Set mesh buffer
					if (meshFilter->HasMesh() && meshFilter->SetBuffers())
					{
						// Set face culling (changes only if required)
						m_graphics->SetCullMode(objMaterial->GetCullMode());

						// Render the mesh, finally!				
						meshRenderer->Render(objMesh->GetIndexCount());

						PerformanceProfiler::RenderingMesh();
					}
				} // GAMEOBJECT/MESH ITERATION

				m_textures.clear();

			} // MATERIAL ITERATION
		} // SHADER ITERATION
	}

	void Renderer::PreDeferredPass()
	{
		m_quad->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		// Set pong texture as render target
		SetRenderTarget(m_renderTexPong);

		//= SHADOW BLUR =====================================================================================
		if (m_directionalLight && m_directionalLight->GetShadowQuality() == Soft_Shadows)
		{
			// BLUR
			m_shaderBlur->Set();
			m_shaderBlur->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION, 0);
			m_shaderBlur->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Normal), 0); // Shadows are alpha
			m_shaderBlur->DrawIndexed(m_quad->GetIndexCount());
		}
		//===================================================================================================

		//= SSAO ========================================================================================================
		SetRenderTarget(m_renderTexSSAO);

		vector<void*> ssaoTextures;
		ssaoTextures.push_back(m_gbuffer->GetShaderResource(GBuffer_Target_Normal));
		ssaoTextures.push_back(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
		ssaoTextures.push_back(m_texNoiseMap->GetShaderResource());

		Matrix mvp_ortho = Matrix::Identity * mBaseView * mOrthographicProjection;
		Matrix mvp_persp_inv = (Matrix::Identity * mView * mProjection).Inverted();
		m_shaderSSAO->Set();
		m_shaderSSAO->SetBuffer(mvp_ortho, mvp_persp_inv, mView, mProjection, GET_RESOLUTION, m_camera->GetNearPlane(), m_camera->GetFarPlane(), 0);
		m_shaderSSAO->SetTextures(ssaoTextures);
		m_shaderSSAO->DrawIndexed(m_quad->GetIndexCount());

		SetRenderTarget(m_renderTexSSAOBlurred);

		// BLUR
		m_shaderBlur->Set();
		m_shaderBlur->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION * 0.5f, 0);
		m_shaderBlur->SetTexture(m_renderTexSSAO->GetShaderResourceView(), 0);
		m_shaderBlur->DrawIndexed(m_quad->GetIndexCount());
		//==============================================================================================================
	}

	void Renderer::DeferredPass()
	{
		if (!m_shaderDeferred->IsCompiled())
			return;

		// Set the deferred shader
		m_shaderDeferred->Set();

		// Set render target
		SetRenderTarget(m_renderTexPing);

		// Update buffers
		m_shaderDeferred->UpdateMatrixBuffer(Matrix::Identity, mView, mBaseView, mProjection, mOrthographicProjection);
		m_shaderDeferred->UpdateMiscBuffer(m_lights, m_camera);

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.push_back(m_gbuffer->GetShaderResource(GBuffer_Target_Albedo));
		m_texArray.push_back(m_gbuffer->GetShaderResource(GBuffer_Target_Normal));
		m_texArray.push_back(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
		m_texArray.push_back(m_gbuffer->GetShaderResource(GBuffer_Target_Material));
		m_texArray.push_back(m_renderTexPong->GetShaderResourceView()); // contains shadows
		m_texArray.push_back(m_renderTexSSAOBlurred->GetShaderResourceView());
		m_texArray.push_back(m_renderTexFinalFrame->GetShaderResourceView());
		m_texArray.push_back(m_skybox ? m_skybox->GetEnvironmentTexture() : nullptr);

		m_shaderDeferred->UpdateTextures(m_texArray);
		//=============================================================================

		m_shaderDeferred->Render(m_quad->GetIndexCount());
	}

	bool Renderer::RenderGBuffer()
	{
		if (!(m_renderFlags & Render_Albedo) && !(m_renderFlags & Render_Normal) && !(m_renderFlags & Render_Depth) && !(m_renderFlags & Render_Material))
			return false;

		GBuffer_Texture_Type texType = GBuffer_Target_Unknown;
		if (m_renderFlags & Render_Albedo)
		{
			texType = GBuffer_Target_Albedo;
		}
		else if (m_renderFlags & Render_Normal)
		{
			texType = GBuffer_Target_Normal;
		}
		else if (m_renderFlags & Render_Depth)
		{
			texType = GBuffer_Target_Depth;
		}
		else if (m_renderFlags & Render_Material)
		{
			texType = GBuffer_Target_Material;
		}

		// TEXTURE
		m_shaderTexture->Set();
		m_shaderTexture->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, 0);
		m_shaderTexture->SetTexture(m_gbuffer->GetShaderResource(texType), 0);
		m_shaderTexture->DrawIndexed(m_quad->GetIndexCount());

		return true;
	}

	void Renderer::PostDeferredPass()
	{
		m_quad->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		SetRenderTarget(m_renderTexPong);

		// For debugging purposes (ideally, we shouldn't post-process this) 
		RenderGBuffer();

		// FXAA
		m_shaderFXAA->Set();
		m_shaderFXAA->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION, 0);
		m_shaderFXAA->SetTexture(m_renderTexPing->GetShaderResourceView(), 0);
		m_shaderFXAA->DrawIndexed(m_quad->GetIndexCount());

		SetRenderTarget(m_renderTexFinalFrame);
		
		// SHARPENING
		m_shaderSharpening->Set();
		m_shaderSharpening->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION, 0);
		m_shaderSharpening->SetTexture(m_renderTexPong->GetShaderResourceView(), 0);
		m_shaderSharpening->DrawIndexed(m_quad->GetIndexCount());

		DebugDraw();
	}

	void Renderer::DebugDraw()
	{
		//= PRIMITIVES ===================================================================================
		// Anything that is a bunch of vertices (doesn't have a vertex and and index buffer) get's rendered here
		// by passing it's vertices (VertexPosCol) to the LineRenderer. Typically used only for debugging.
		if (m_lineRenderer)
		{
			m_lineRenderer->ClearVertices();

			// Physics
			if (m_renderFlags & Render_Physics)
			{
				Physics* physics = m_context->GetSubsystem<Physics>();
				physics->DebugDraw();
				if (physics->GetPhysicsDebugDraw()->IsDirty())
				{
					m_lineRenderer->AddLines(physics->GetPhysicsDebugDraw()->GetLines());
				}
			}

			// Picking ray
			if (m_renderFlags & Render_PickingRay)
			{
				m_lineRenderer->AddLines(m_camera->GetPickingRay());
			}

			// bounding boxes
			if (m_renderFlags & Render_AABB)
			{
				for (const auto& gameObject : m_renderables)
				{
					auto meshFilter = gameObject.lock()->GetComponent<MeshFilter>().lock();
					m_lineRenderer->AddBoundigBox(meshFilter->GetBoundingBoxTransformed(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
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
		if (m_renderFlags & Render_Grid)
		{
			m_grid->SetBuffer();
			m_shaderGrid->Set();
			m_shaderGrid->SetBuffer(m_grid->ComputeWorldMatrix(m_camera->GetTransform()), m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix(), 0);
			m_shaderGrid->SetTexture(m_gbuffer->GetShaderResource(GBuffer_Target_Depth), 0);
			m_shaderGrid->DrawIndexed(m_grid->GetIndexCount());
		}

		// Light gizmo
		if (m_renderFlags & Render_Light)
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
				LightType type = light->GetGameObject()->GetComponent<Light>().lock()->GetLightType();
				if (type == Directional)
				{
					lightTex = m_gizmoTexLightDirectional.get();
				}
				else if (type == Point)
				{
					lightTex = m_gizmoTexLightPoint.get();
				}
				else if (type == Spot)
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
				m_shaderTexture->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, 0);
				m_shaderTexture->SetTexture((ID3D11ShaderResourceView*)lightTex->GetShaderResource(), 0);
				m_shaderTexture->DrawIndexed(m_gizmoRectLight->GetIndexCount());
			}
		}

		// Performance metrics
		if (m_renderFlags & Render_PerformanceMetrics)
		{
			m_font->SetText(PerformanceProfiler::GetMetrics(), Vector2(-RESOLUTION_WIDTH * 0.5f + 1.0f, RESOLUTION_HEIGHT * 0.5f));
			m_font->SetBuffer();

			m_shaderFont->Set();
			m_shaderFont->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, m_font->GetColor(), 0);
			m_shaderFont->SetTexture((ID3D11ShaderResourceView*)m_font->GetShaderResource(), 0);
			m_shaderFont->DrawIndexed(m_font->GetIndexCount());
		}

		m_graphics->EnableAlphaBlending(false);
	}

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	}
	//===============================================================================================================
}
