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

//= INCLUDES ===========================
#include "Renderer.h"
#include "Gbuffer.h"
#include "FullScreenQuad.h"
#include "Shaders/ShaderVariation.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DeferredShader.h"
#include "D3D11/D3D11RenderTexture.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Skybox.h"
#include "../Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventSystem.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include "../Core/Context.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceManager.h"
#include "Material.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Renderer::Renderer(Context* context) : Subsystem(context)
	{
		m_renderedMeshesPerFrame = 0;
		m_renderedMeshesTempCounter = 0;
		m_skybox = nullptr;
		m_camera = nullptr;
		m_texEnvironment = nullptr;
		m_lineRenderer = nullptr;
		m_nearPlane = 0.0f;
		m_farPlane = 0.0f;
		m_resourceMng = nullptr;
		m_graphics = nullptr;
		m_renderOutput = Render_Default;

		// Subscribe to render event
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, this, Renderer::Render);
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
			LOG_ERROR("Rendere: Failed to initialize, an initialized Graphics subsystem is required.");
			return false;
		}

		// Get ResourceManager subsystem
		m_resourceMng = m_context->GetSubsystem<ResourceManager>();

		// Create G-Buffer
		m_GBuffer = make_shared<GBuffer>(m_graphics);
		m_GBuffer->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		// Create fullscreen quad
		m_fullScreenQuad = make_shared<FullScreenQuad>();
		m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_graphics);

		// Load and compile shaders
		string shaderDirectory = m_resourceMng->GetStandardResourceDirectory(Shader_Resource);
		string textureDirectory = m_resourceMng->GetStandardResourceDirectory(Texture_Resource);

		m_shaderDeferred = make_shared<DeferredShader>();
		m_shaderDeferred->Load(shaderDirectory + "Deferred.hlsl", m_graphics);

		m_shaderDepth = make_shared<DepthShader>();
		m_shaderDepth->Load(shaderDirectory + "Depth.hlsl", m_graphics);

		m_shaderDebug = make_shared<DebugShader>();
		m_shaderDebug->Load(shaderDirectory + "Debug.hlsl", m_graphics);

		m_shaderFXAA = make_shared<PostProcessShader>();
		m_shaderFXAA->Load(shaderDirectory + "PostProcess.hlsl", "FXAA", m_graphics);

		m_shaderSharpening = make_shared<PostProcessShader>();
		m_shaderSharpening->Load(shaderDirectory + "PostProcess.hlsl", "SHARPENING", m_graphics);

		m_shaderBlur = make_shared<PostProcessShader>();
		m_shaderBlur->Load(shaderDirectory + "PostProcess.hlsl", "BLUR", m_graphics);

		m_shaderTex = make_shared<PostProcessShader>();
		m_shaderTex->Load(shaderDirectory + "PostProcess.hlsl", "TEXTURE", m_graphics);

		// Create render textures (used for post processing)
		m_renderTexPing = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPing->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexPong = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPong->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		// Misc
		m_texNoiseMap = make_shared<Texture>(m_context);
		m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
		m_texNoiseMap->SetTextureType(Normal_Texture);

		return true;
	}

	void Renderer::Render()
	{
		if (!m_graphics)
			return;

		if (!m_graphics->IsInitialized())
			return;

		StartCalculatingStats();
		AcquirePrerequisites();

		// If there is no camera, clear to black and present
		if (!m_camera)
		{
			m_graphics->Clear(Vector4(0, 0, 0, 1));
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

		// ENABLE Z-BUFFER
		m_graphics->EnableDepth(true);

		// Render light depth
		DirectionalLightDepthPass();

		// G-Buffer
		GBufferPass();

		// DISABLE Z-BUFFER
		m_graphics->EnableDepth(false);

		// Deferred Pass
		DeferredPass();

		// Post Processing
		PostProcessing();

		// Render debug info
		DebugDraw();

		// display frame
		m_graphics->Present();

		StopCalculatingStats();
	}

	void Renderer::SetResolution(int width, int height)
	{
		// A resolution of 0 will cause the depth stencil 
		// buffer creation to fail, let's prevent that.
		if (width <= 0 || height <= 0)
			return;

		SET_RESOLUTION(width, height);

		m_GBuffer.reset();
		m_fullScreenQuad.reset();
		m_renderTexPing.reset();
		m_renderTexPong.reset();

		m_graphics->SetResolution(width, height);

		m_GBuffer = make_shared<GBuffer>(m_graphics);
		m_GBuffer->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_fullScreenQuad = make_shared<FullScreenQuad>();
		m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_graphics);

		m_renderTexPing = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPing->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexPong = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPong->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
	}

	void Renderer::SetViewport(float width, float height)
	{
		SET_VIEWPORT(width, height);
		m_graphics->SetViewport(width, height);
	}

	void Renderer::Clear()
	{
		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lights.clear();
		m_lights.shrink_to_fit();
		m_directionalLight = nullptr;
	}

	void Renderer::AcquirePrerequisites()
	{
		Clear();
		Scene* scene = m_context->GetSubsystem<Scene>();
		m_renderables = scene->GetRenderables();
		m_lights = scene->GetLights();

		// Get directional light
		for (const auto& light : m_lights)
		{
			if (light->GetLightType() == Directional)
			{
				m_directionalLight = light;
				break;
			}
		}

		// Get camera and camera related properties
		weakGameObj camera = scene->GetMainCamera();
		if (!camera.expired())
		{
			m_camera = camera.lock()->GetComponent<Camera>();

			weakGameObj skybox = scene->GetSkybox();
			if (!skybox.expired())
			{
				m_skybox = skybox.lock()->GetComponent<Skybox>();
				m_lineRenderer = skybox.lock()->GetComponent<LineRenderer>(); // Hush hush...
			}

			mView = m_camera->GetViewMatrix();
			mProjection = m_camera->GetProjectionMatrix();
			mViewProjection = mView * mProjection;
			mOrthographicProjection = Matrix::CreateOrthographicLH(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
			mBaseView = m_camera->GetBaseViewMatrix();
			m_nearPlane = m_camera->GetNearPlane();
			m_farPlane = m_camera->GetFarPlane();
		}
		else
		{
			m_camera = nullptr;
			m_skybox = nullptr;
			m_lineRenderer = nullptr;
		}
	}

	void Renderer::DirectionalLightDepthPass()
	{
		if (!m_directionalLight)
			return;

		if (m_directionalLight->GetShadowType() == No_Shadows)
			return;

		m_shaderDepth->Set();

		for (int cascadeIndex = 0; cascadeIndex < m_directionalLight->GetShadowCascadeCount(); cascadeIndex++)
		{
			// Set appropriate shadow map as render target
			m_directionalLight->SetShadowCascadeAsRenderTarget(cascadeIndex);

			Matrix mViewLight = m_directionalLight->ComputeViewMatrix();
			Matrix mProjectionLight = m_directionalLight->ComputeOrthographicProjectionMatrix(cascadeIndex);
			Matrix mViewProjectionLight = mViewLight * mProjectionLight;

			for (const auto& gameObj : m_renderables)
			{
				if (gameObj.expired())
					continue;

				MeshFilter* meshFilter = gameObj._Get()->GetMeshFilter();
				MeshRenderer* meshRenderer = gameObj._Get()->GetMeshRenderer();
				auto material = meshRenderer->GetMaterial();
				auto mesh = meshFilter->GetMesh();

				// Make sure we have everything
				if (mesh.expired() || !meshFilter || !meshRenderer || material.expired())
					continue;

				// Skip meshes that don't cast shadows
				if (!meshRenderer->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material._Get()->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustrum
				//if (!m_directionalLight->IsInViewFrustrum(meshFilter))
					//continue;

				if (meshFilter->SetBuffers())
				{
					// Set shader's buffer
					m_shaderDepth->UpdateMatrixBuffer(
						gameObj._Get()->GetTransform()->GetWorldTransform(),
						mViewProjectionLight
					);

					// Render
					m_shaderDepth->Render(mesh._Get()->GetIndexCount());
				}
			}
		}
	}

	void Renderer::GBufferPass()
	{
		if (!m_graphics)
			return;

		m_GBuffer->SetAsRenderTarget();
		m_graphics->ResetViewport();
		m_GBuffer->Clear();

		vector<weak_ptr<Material>> materials = m_resourceMng->GetResourcesByType<Material>();
		vector<weak_ptr<ShaderVariation>> shaders = m_resourceMng->GetResourcesByType<ShaderVariation>();

		for (const auto& shader : shaders) // SHADER ITERATION
		{
			// Set the shader
			shader._Get()->Set();

			// UPDATE PER FRAME BUFFER
			shader._Get()->UpdatePerFrameBuffer(m_directionalLight, m_camera);

			for (const auto& material : materials) // MATERIAL ITERATION
			{
				// Continue only if the material at hand happens to use the already set shader
				if (material._Get()->GetShader()._Get()->GetResourceID() != shader._Get()->GetResourceID())
					continue;

				// UPDATE PER MATERIAL BUFFER
				shader._Get()->UpdatePerMaterialBuffer(material);

				// Order the textures they way the shader expects them
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Albedo_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Roughness_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Metallic_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Normal_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Height_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Occlusion_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Emission_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Mask_Texture));

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
				shader._Get()->UpdateTextures(m_textures);
				//==================================================================================

				for (const auto& gameObj : m_renderables) // GAMEOBJECT/MESH ITERATION
				{
					if (gameObj.expired())
						continue;

					//= Get all that we need =========================================
					MeshFilter* meshFilter = gameObj._Get()->GetMeshFilter();
					MeshRenderer* meshRenderer = gameObj._Get()->GetMeshRenderer();
					auto objMesh = meshFilter->GetMesh()._Get();
					auto objMaterial = meshRenderer->GetMaterial()._Get();
					auto mWorld = gameObj._Get()->GetTransform()->GetWorldTransform();
					//================================================================

					// skip objects that are missing required components
					if (!meshFilter || !objMesh || !meshRenderer || !objMaterial)
						continue;

					// skip objects that use a different material
					if (material._Get()->GetResourceID() != objMaterial->GetResourceID())
						continue;

					// skip transparent objects (for now)
					if (objMaterial->GetOpacity() < 1.0f)
						continue;

					// skip objects outside of the view frustrum
					if (!m_camera->IsInViewFrustrum(meshFilter))
						continue;

					// UPDATE PER OBJECT BUFFER
					shader._Get()->UpdatePerObjectBuffer(mWorld, mView, mProjection, meshRenderer->GetReceiveShadows());

					// Set mesh buffer
					if (meshFilter->HasMesh())
					{
						if (meshFilter->SetBuffers())
						{
							// Set face culling (changes only if required)
							m_graphics->SetCullMode(objMaterial->GetCullMode());

							// Render the mesh, finally!				
							meshRenderer->Render(objMesh->GetIndexCount());

							m_renderedMeshesTempCounter++;
						}
					}
				} // GAMEOBJECT/MESH ITERATION

				m_textures.clear();

			} // MATERIAL ITERATION
		} // SHADER ITERATION
	}

	//= HELPER FUNCTIONS ==============================================================================================
	void Renderer::DeferredPass()
	{
		m_fullScreenQuad->SetBuffers();
		m_graphics->SetCullMode(CullBack);

		//= SHADOW BLUR ========================================
		if (m_directionalLight)
		{
			if (m_directionalLight->GetShadowType() == Soft_Shadows)
			{
				// Set render target
				m_renderTexPong->SetAsRenderTarget();
				m_renderTexPong->Clear(GetClearColor());

				m_shaderBlur->Render(
					m_fullScreenQuad->GetIndexCount(),
					Matrix::Identity,
					mBaseView,
					mOrthographicProjection,
					m_GBuffer->GetShaderResource(1) // Normal tex but shadows are in alpha channel
				);
			}
		}
		//=====================================================

		if (!m_shaderDeferred->IsCompiled())
			return;

		// Set the deferred shader
		m_shaderDeferred->Set();

		// Set render target
		m_renderTexPing->SetAsRenderTarget();
		m_renderTexPing->Clear(GetClearColor());

		// Update buffers
		m_shaderDeferred->UpdateMatrixBuffer(Matrix::Identity, mView, mBaseView, mProjection, mOrthographicProjection);
		m_shaderDeferred->UpdateMiscBuffer(m_lights, m_camera);

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.push_back(m_GBuffer->GetShaderResource(0)); // albedo
		m_texArray.push_back(m_GBuffer->GetShaderResource(1)); // normal
		m_texArray.push_back(m_GBuffer->GetShaderResource(2)); // depth
		m_texArray.push_back(m_GBuffer->GetShaderResource(3)); // material
		m_texArray.push_back((ID3D11ShaderResourceView*)m_texNoiseMap->GetShaderResource());
		m_texArray.push_back(m_renderTexPong->GetShaderResourceView()); // contains blurred shadows
		m_texArray.push_back(m_skybox ? (ID3D11ShaderResourceView*)m_skybox->GetEnvironmentTexture() : nullptr);

		m_shaderDeferred->UpdateTextures(m_texArray);
		//=============================================================================

		m_shaderDeferred->Render(m_fullScreenQuad->GetIndexCount());
	}

	void Renderer::PostProcessing()
	{
		m_graphics->SetCullMode(CullBack);

		if (m_renderOutput != Render_Default)
		{
			m_graphics->SetBackBufferAsRenderTarget();
			m_graphics->ResetViewport();
			m_graphics->Clear(m_camera->GetClearColor());

			int texIndex = 0;
			if (m_renderOutput == Render_Albedo)
			{
				texIndex = 0;
			}
			else if (m_renderOutput == Render_Normal)
			{
				texIndex = 1;
			}
			else if (m_renderOutput == Render_Depth)
			{
				texIndex = 2;
			}
			else if (m_renderOutput == Render_Material)
			{
				texIndex = 3;
			}

			m_shaderTex->Render(
				m_fullScreenQuad->GetIndexCount(),
				Matrix::Identity,
				mBaseView,
				mOrthographicProjection,
				m_GBuffer->GetShaderResource(texIndex)
			);

			return;
		}

		// Set Ping texture as render target
		m_renderTexPong->SetAsRenderTarget();
		m_renderTexPong->Clear(GetClearColor());
	
		// fxaa pass
		m_shaderFXAA->Render(
			m_fullScreenQuad->GetIndexCount(),
			Matrix::Identity,
			mBaseView,
			mOrthographicProjection,
			m_renderTexPing->GetShaderResourceView()
		);

		m_graphics->SetBackBufferAsRenderTarget();
		m_graphics->ResetViewport();
		m_graphics->Clear(m_camera->GetClearColor());

		// sharpening pass
		m_shaderSharpening->Render(
			m_fullScreenQuad->GetIndexCount(),
			Matrix::Identity,
			mBaseView,
			mOrthographicProjection,
			m_renderTexPong->GetShaderResourceView()
		);
	}

	void Renderer::DebugDraw()
	{
		if (!DEBUG_DRAW)
			return;

		if (!m_lineRenderer)
			return;

		m_lineRenderer->ClearVertices();

		// // Pass debug info from bullet physics
		m_context->GetSubsystem<Physics>()->DebugDraw();
		if (m_context->GetSubsystem<Physics>()->GetPhysicsDebugDraw()->IsDirty())
		{
			m_lineRenderer->AddLines(m_context->GetSubsystem<Physics>()->GetPhysicsDebugDraw()->GetLines());
		}

		// Pass the picking ray
		m_lineRenderer->AddLines(m_camera->GetPickingRay());

		// Pass bounding spheres
		for (const auto& gameObject : m_renderables) 
		{
			auto meshFilter = gameObject._Get()->GetComponent<MeshFilter>();
			m_lineRenderer->AddBoundigBox(meshFilter->GetBoundingBoxTransformed(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
		}

		// Set the buffer
		m_lineRenderer->SetBuffer();

		// Render
		m_shaderDebug->Render(
			m_lineRenderer->GetVertexCount(),
			Matrix::Identity,
			m_camera->GetViewMatrix(),
			m_camera->GetProjectionMatrix(),
			m_GBuffer->GetShaderResource(2) // depth
		);
	}

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	//= STATS ============================
	// Called in the beginning of the rendering
	void Renderer::StartCalculatingStats()
	{
		Stopwatch::Start();
		m_renderedMeshesTempCounter = 0;
	}

	// Called in the end of the rendering
	void Renderer::StopCalculatingStats()
	{
		m_renderTimeMs = Stopwatch::Stop();
		m_renderedMeshesPerFrame = m_renderedMeshesTempCounter;
	}
	//===============================================================================================================
}
