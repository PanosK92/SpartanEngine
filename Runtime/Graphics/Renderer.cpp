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
#include "../EventSystem/EventHandler.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include "../Core/Context.h"
#include "../Core/Settings.h"
#include "../Core/Timer.h"
#include "../Core/Engine.h"
#include "../Resource/ResourceManager.h"
#include "../Core/Stopwatch.h"
#include "../Math/Frustrum.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Renderer::Renderer(Context* context) : Subsystem(context)
	{
		m_GBuffer = nullptr;
		m_fullScreenQuad = nullptr;
		m_renderedMeshesPerFrame = 0;
		m_renderedMeshesTempCounter = 0;
		m_renderTexPing = nullptr;
		m_renderTexPong = nullptr;
		m_shaderDeferred = nullptr;
		m_shaderDepth = nullptr;
		m_shaderDebug = nullptr;
		m_shaderFXAA = nullptr;
		m_shaderSharpening = nullptr;
		m_shaderBlur = nullptr;
		m_texNoiseMap = nullptr;
		m_skybox = nullptr;
		m_camera = nullptr;
		m_texEnvironment = nullptr;
		m_lineRenderer = nullptr;
		m_directionalLight = nullptr;
		m_nearPlane = 0.0;
		m_farPlane = 0.0f;

		// Subscribe to render event
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, this, Renderer::Render);
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Get the graphics subsystem
		m_graphics = m_context->GetSubsystem<Graphics>();
		if (!m_graphics->IsInitialized())
		{
			LOG_ERROR("Rendere: Failed to initialize, an initialized Graphics subsystem is required.");
			return false;
		}

		m_GBuffer = make_shared<GBuffer>(m_graphics);
		m_GBuffer->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_fullScreenQuad = make_shared<FullScreenQuad>();
		m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_graphics);

		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		std::string shaderDirectory = resourceMng->GetResourceDirectory(Shader_Resource);
		std::string textureDirectory = resourceMng->GetResourceDirectory(Texture_Resource);

		// Shaders
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

		// Render textures
		m_renderTexPing = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPing->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_renderTexPong = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPong->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		// Misc
		m_texNoiseMap = make_shared<Texture>(m_context);
		m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
		m_texNoiseMap->SetTextureType(Normal_Texture);

		return true;
	}

	void Renderer::Render()
	{
		if (!m_graphics->IsInitialized())
			return;

		StartCalculatingStats();
		AcquirePrerequisites();

		// If there is not camera, clear to black and present
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
		m_graphics->EnableZBuffer(true);

		// Render light depth
		if (m_directionalLight)
			if (m_directionalLight->GetShadowType() != No_Shadows)
				DirectionalLightDepthPass();

		// G-Buffer Construction
		m_GBuffer->SetAsRenderTarget();
		m_GBuffer->Clear(m_camera->GetClearColor());
		GBufferPass();

		// DISABLE Z BUFFER - SET FULLSCREEN QUAD
		m_graphics->EnableZBuffer(false);
		m_fullScreenQuad->SetBuffers();

		// Deferred Pass
		DeferredPass();

		// Post Proessing
		PostProcessing();

		// Gizmos
		Gizmos();
		
		//ID3D11CommandList* m_commandList;
		//m_graphics->GetDeviceDeferredContext()->FinishCommandList(true, &m_commandList);
		//m_graphics->GetDeviceDeferredContext()->ExecuteCommandList(m_commandList, true);

		// display frame
		m_graphics->Present();

		StopCalculatingStats();
	}

	void Renderer::SetResolution(int width, int height)
	{
		// A resolution of 0 won't cause a crash or anything crazy,
		// but it will cause the depth stencil buffer creation to fail,
		// various error messages to be displayed. I silently prevent that.
		if (width <= 0 || height <= 0)
			return;

		SET_RESOLUTION(width, height);

		m_GBuffer.reset();
		m_fullScreenQuad.reset();
		m_renderTexPing.reset();
		m_renderTexPong.reset();

		m_GBuffer = make_shared<GBuffer>(m_graphics);
		m_GBuffer->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_fullScreenQuad = make_shared<FullScreenQuad>();
		m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_graphics);

		m_renderTexPing = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPing->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_renderTexPong = make_shared<D3D11RenderTexture>(m_graphics);
		m_renderTexPong->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_graphics->SetResolution(width, height);
	}

	void Renderer::Clear()
	{
		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lightsDirectional.clear();
		m_lightsDirectional.shrink_to_fit();

		m_lightsPoint.clear();
		m_lightsPoint.shrink_to_fit();
	}

	void Renderer::AcquirePrerequisites()
	{
		Clear();
		Scene* scene = m_context->GetSubsystem<Scene>();
		m_renderables = scene->GetRenderables();
		m_lightsDirectional = scene->GetLightsDirectional();
		m_lightsPoint = scene->GetLightsPoint();

		GameObject* camera = scene->GetMainCamera();
		if (camera)
		{
			m_camera = camera->GetComponent<Camera>();

			GameObject* skybox = scene->GetSkybox();
			if (skybox)
			{
				m_skybox = skybox->GetComponent<Skybox>();
				m_lineRenderer = skybox->GetComponent<LineRenderer>(); // Hush hush...
			}

			if (m_lightsDirectional.size() != 0)
				m_directionalLight = m_lightsDirectional[0]->GetComponent<Light>();
			else
				m_directionalLight = nullptr;

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
			m_directionalLight = nullptr;
		}
	}

	void Renderer::DirectionalLightDepthPass()
	{
		m_graphics->SetCullMode(CullFront);

		// Set the depth shader
		m_shaderDepth->Set();

		for (int cascadeIndex = 0; cascadeIndex < m_directionalLight->GetShadowCascadeCount(); cascadeIndex++)
		{
			// Set appropriate shadow map as render target
			m_directionalLight->SetShadowCascadeAsRenderTarget(cascadeIndex);

			Matrix mViewLight = m_directionalLight->CalculateViewMatrix();
			Matrix mProjectionLight = m_directionalLight->CalculateOrthographicProjectionMatrix(cascadeIndex);
			Matrix mViewProjectionLight = mViewLight * mProjectionLight;

			for (const auto& gameObject : m_renderables)
			{
				auto meshRenderer = gameObject->GetComponent<MeshRenderer>();
				auto material = meshRenderer->GetMaterial();
				auto meshFilter = gameObject->GetComponent<MeshFilter>();
				auto mesh = meshFilter->GetMesh();

				// Make sure we have everything
				if (mesh.expired() || !meshFilter || !meshRenderer)
					continue;

				// Skip meshes that don't cast shadows
				if (!meshRenderer->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material.lock()->GetOpacity() < 1.0f)
					continue;

				if (meshFilter->SetBuffers())
				{
					// Set shader's buffer
					m_shaderDepth->UpdateMatrixBuffer(
						gameObject->GetTransform()->GetWorldTransform(),
						mViewProjectionLight
					);

					// Render
					m_shaderDepth->Render(mesh.lock()->GetIndexCount());
				}
			}
		}
	}

	void Renderer::GBufferPass()
	{
		Graphics* graphics = m_context->GetSubsystem<Graphics>();
		auto materials = m_context->GetSubsystem<ResourceManager>()->GetAllByType<Material>();
		auto shaders = m_context->GetSubsystem<ResourceManager>()->GetAllByType<ShaderVariation>();

		for (const auto& tempShader : shaders) // iterate through the shaders
		{
			// Set the shader
			auto renderShader = tempShader.lock();
			renderShader->Set();

			// UPDATE PER FRAME BUFFER
			renderShader->UpdatePerFrameBuffer(m_directionalLight, m_camera);

			for (const auto& tempMaterial : materials) // iterate through the materials
			{
				// Continue only if the material at hand happens to use the already set shader
				auto renderMaterial = tempMaterial.lock();
				if (renderMaterial->GetShader().lock()->GetResourceID() != renderShader->GetResourceID())
					continue;

				// UPDATE PER MATERIAL BUFFER
				renderShader->UpdatePerMaterialBuffer(renderMaterial);

				//= Gather any used textures and bind them to the GPU ===============================
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Albedo_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Roughness_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Metallic_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Normal_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Height_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Occlusion_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Emission_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Mask_Texture));

				if (m_directionalLight)
				{
					for (int i = 0; i < m_directionalLight->GetShadowCascadeCount(); i++)
					{
						auto shadowMap = m_directionalLight->GetShadowCascade(i).lock();
						m_textures.push_back(shadowMap ? shadowMap->GetShaderResourceView() : nullptr);
					}
				}
				else
				{
					m_textures.push_back(nullptr);
					m_textures.push_back(nullptr);
					m_textures.push_back(nullptr);
				}

				// UPDATE TEXTURE BUFFER
				renderShader->UpdateTextures(m_textures);
				//==================================================================================

				for (const auto& gameObject : m_renderables) // render GameObject that use the renderMaterial
				{
					//= Get all that we need =====================================
					auto meshFilter = gameObject->GetComponent<MeshFilter>();
					auto mesh = meshFilter->GetMesh();
					auto meshRenderer = gameObject->GetComponent<MeshRenderer>();
					auto material = meshRenderer->GetMaterial();
					auto mWorld = gameObject->GetTransform()->GetWorldTransform();
					//============================================================

					// If any rendering requirement is missing, skip this GameObject
					if (!meshFilter || mesh.expired() || !meshRenderer || material.expired())
						continue;

					//... that uses the current material
					if (renderMaterial->GetResourceID() != material.lock()->GetResourceID())
						continue;

					// Skip transparent meshes (for now)
					if (material.lock()->GetOpacity() < 1.0f)
						continue;

					// Make sure the mesh is actually in our view frustrum
					if (!IsInViewFrustrum(m_camera->GetFrustrum(), meshFilter))
						continue;

					// UPDATE PER OBJECT BUFFER
					renderShader->UpdatePerObjectBuffer(mWorld, mView, mProjection, meshRenderer->GetReceiveShadows());

					// Set mesh buffer
					if (meshFilter->SetBuffers())
					{
						// Set face culling (changes only if required)
						graphics->SetCullMode(material.lock()->GetCullMode());

						// Render the mesh, finally!				
						meshRenderer->Render(mesh.lock()->GetIndexCount());

						m_renderedMeshesTempCounter++;
					}
				} // renderable loop

				m_textures.clear();
				m_textures.shrink_to_fit();

			} // material loop
		} // shader loop
	}

	//= HELPER FUNCTIONS ==============================================================================================
	bool Renderer::IsInViewFrustrum(const shared_ptr<Frustrum>& cameraFrustrum, MeshFilter* meshFilter)
	{
		Vector3 center = meshFilter->GetCenter();
		Vector3 extent = meshFilter->GetBoundingBox();

		float radius = max(abs(extent.x), abs(extent.y));
		radius = max(radius, abs(extent.z));

		return cameraFrustrum->CheckSphere(center, radius) == Outside ? false : true;
	}

	void Renderer::DeferredPass()
	{
		//= SHADOW BLUR ========================================
		if (m_directionalLight)
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
					m_GBuffer->GetShaderResourceView(1) // Normal tex but shadows are in alpha channel
				);
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
		m_shaderDeferred->UpdateMiscBuffer(m_directionalLight, m_lightsPoint, m_camera);

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.push_back(m_GBuffer->GetShaderResourceView(0)); // albedo
		m_texArray.push_back(m_GBuffer->GetShaderResourceView(1)); // normal
		m_texArray.push_back(m_GBuffer->GetShaderResourceView(2)); // depth
		m_texArray.push_back(m_GBuffer->GetShaderResourceView(3)); // material
		m_texArray.push_back((ID3D11ShaderResourceView*)m_texNoiseMap->GetShaderResourceView());
		m_texArray.push_back(m_renderTexPong->GetShaderResourceView());
		m_texArray.push_back(m_skybox ? (ID3D11ShaderResourceView*)m_skybox->GetEnvironmentTexture() : nullptr);

		m_shaderDeferred->UpdateTextures(m_texArray);
		//=============================================================================

		m_shaderDeferred->Render(m_fullScreenQuad->GetIndexCount());
	}

	void Renderer::PostProcessing()
	{
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

	void Renderer::Gizmos() const
	{
		if (m_context->GetSubsystem<Engine>()->IsSimulating())
			return;

		m_context->GetSubsystem<Physics>()->DebugDraw();

		if (!m_lineRenderer)
			return;

		if (!m_context->GetSubsystem<Physics>()->GetPhysicsDebugDraw()->IsDirty())
			return;

		// Pass the line list from bullet to the line renderer component
		m_lineRenderer->AddLineList(m_context->GetSubsystem<Physics>()->GetPhysicsDebugDraw()->GetLines());

		// Set the buffer
		m_lineRenderer->SetBuffer();

		// Render
		m_shaderDebug->Render(
			m_lineRenderer->GetVertexCount(),
			Matrix::Identity,
			m_camera->GetViewMatrix(),
			m_camera->GetProjectionMatrix(),
			m_GBuffer->GetShaderResourceView(2) // depth
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
		m_renderTimeMs = Stopwatch::End();
		m_renderedMeshesPerFrame = m_renderedMeshesTempCounter;
	}
	//===============================================================================================================
}