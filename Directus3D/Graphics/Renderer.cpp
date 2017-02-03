/*
Copyright(c) 2016 Panos Karabelas

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
#include "Shaders/ShaderVariation.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DeferredShader.h"
#include "D3D11/D3D11RenderTexture.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/LineRenderer.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventHandler.h"
#include "../Resource/ResourceCache.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include "../Core/Context.h"
#include "../Core/Settings.h"
#include "../Core/Timer.h"
#include "../Core/Engine.h"
//======================================

//= NAMESPACES ====================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Resource;
//=================================

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

	D3D11GraphicsDevice* graphics = g_context->GetSubsystem<D3D11GraphicsDevice>();

	m_GBuffer = make_shared<GBuffer>(graphics);
	m_GBuffer->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_fullScreenQuad = make_shared<FullScreenQuad>();
	m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, graphics);

	/*------------------------------------------------------------------------------
	[SHADERS]
	------------------------------------------------------------------------------*/
	m_shaderDeferred = make_shared<DeferredShader>();
	m_shaderDeferred->Initialize(graphics);

	m_shaderDepth = make_shared<DepthShader>();
	m_shaderDepth->Initialize(graphics);

	m_shaderDebug = make_shared<DebugShader>();
	m_shaderDebug->Initialize(graphics);

	m_shaderFXAA = make_shared<PostProcessShader>();
	m_shaderFXAA->Initialize("FXAA", graphics);

	m_shaderSharpening = make_shared<PostProcessShader>();
	m_shaderSharpening->Initialize("SHARPENING", graphics);

	m_shaderBlur = make_shared<PostProcessShader>();
	m_shaderBlur->Initialize("BLUR", graphics);

	/*------------------------------------------------------------------------------
	[RENDER TEXTURES]
	------------------------------------------------------------------------------*/
	m_renderTexPing = make_shared<D3D11RenderTexture>(graphics);
	m_renderTexPing->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_renderTexPong = make_shared<D3D11RenderTexture>(graphics);
	m_renderTexPong->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	/*------------------------------------------------------------------------------
	[MISC]
	------------------------------------------------------------------------------*/
	m_texNoiseMap = make_shared<Texture>(g_context);
	m_texNoiseMap->LoadFromFile("Assets/Shaders/noise.png");
	m_texNoiseMap->SetType(Normal);

	// Subcribe to render event
	SUBSCRIBE_TO_EVENT(EVENT_RENDER, this, Renderer::Render);
}

Renderer::~Renderer()
{

}

void Renderer::Render()
{
	D3D11GraphicsDevice* graphics = g_context->GetSubsystem<D3D11GraphicsDevice>();

	StartCalculatingStats();
	AcquirePrerequisites();

	// If there is not camera, clear to black and present
	if (!m_camera)
	{
		graphics->Clear(Vector4(0, 0, 0, 1));
		graphics->Present();
		return;
	}

	// If there is nothing to render clear to camera's color and present
	if (m_renderables.empty())
	{
		graphics->Clear(m_camera->GetClearColor());
		graphics->Present();
		return;
	}

	// ENABLE Z-BUFFER
	graphics->EnableZBuffer(true);

	// Render light depth
	if (m_directionalLight)
		if (m_directionalLight->GetShadowType() != No_Shadows)
			DirectionalLightDepthPass();

	// G-Buffer Construction
	m_GBuffer->SetRenderTargets();
	m_GBuffer->Clear(m_camera->GetClearColor());
	GBufferPass();

	// DISABLE Z BUFFER - SET FULLSCREEN QUAD
	graphics->EnableZBuffer(false);
	m_fullScreenQuad->SetBuffers();

	// Deferred Pass
	DeferredPass();

	// Post Proessing
	PostProcessing();

	// Gizmos
	Gizmos();

	// display frame
	graphics->Present();

	StopCalculatingStats();
}

void Renderer::SetResolution(int width, int height)
{
	// A resolution of 0 won't cause a crash or anything crazy,
	// but it will cause the depth stancil buffer creation to fail,
	// various error messages to be displayed. I silently prevent that.
	if (width <= 0 || height <= 0)
		return;

	SET_RESOLUTION(width, height);
	auto graphicsDevice = g_context->GetSubsystem<GraphicsDevice>();

	m_GBuffer.reset();
	m_fullScreenQuad.reset();
	m_renderTexPing.reset();
	m_renderTexPong.reset();

	m_GBuffer = make_shared<GBuffer>(graphicsDevice);
	m_GBuffer->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_fullScreenQuad = make_shared<FullScreenQuad>();
	m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, graphicsDevice);

	m_renderTexPing = make_shared<D3D11RenderTexture>(graphicsDevice);
	m_renderTexPing->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_renderTexPong = make_shared<D3D11RenderTexture>(graphicsDevice);
	m_renderTexPong->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	graphicsDevice->SetResolution(width, height);
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
	Scene* scene = g_context->GetSubsystem<Scene>();
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
	g_context->GetSubsystem<D3D11GraphicsDevice>()->SetCullMode(CullFront);

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
	D3D11GraphicsDevice* graphics = g_context->GetSubsystem<D3D11GraphicsDevice>();
	auto materials = g_context->GetSubsystem<ResourceCache>()->GetResourcesOfType<Material>();
	auto shaders = g_context->GetSubsystem<ResourceCache>()->GetResourcesOfType<ShaderVariation>();

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
			if (renderMaterial->GetShader().lock()->GetID() != renderShader->GetID())
				continue;

			// UPDATE PER MATERIAL BUFFER
			renderShader->UpdatePerMaterialBuffer(renderMaterial);

			//= Gather any used textures and bind them to the GPU ===============================
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Albedo));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Roughness));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Metallic));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Normal));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Height));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Occlusion));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Emission));
			m_textures.push_back((ID3D11ShaderResourceView*)renderMaterial->GetShaderResourceViewByTextureType(Mask));

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
				if (renderMaterial->GetID() != material.lock()->GetID())
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
					graphics->SetCullMode(material.lock()->GetFaceCullMode());

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

	g_context->GetSubsystem<D3D11GraphicsDevice>()->SetBackBufferAsRenderTarget();
	g_context->GetSubsystem<D3D11GraphicsDevice>()->ResetViewport();
	g_context->GetSubsystem<D3D11GraphicsDevice>()->Clear(m_camera->GetClearColor());

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
	if (g_context->GetSubsystem<Engine>()->IsSimulating())
		return;

	g_context->GetSubsystem<PhysicsWorld>()->DebugDraw();

	if (!m_lineRenderer)
		return;

	if (!g_context->GetSubsystem<PhysicsWorld>()->GetPhysicsDebugDraw()->IsDirty())
		return;

	// Pass the line list from bullet to the line renderer component
	m_lineRenderer->AddLineList(g_context->GetSubsystem<PhysicsWorld>()->GetPhysicsDebugDraw()->GetLines());

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
	m_renderedMeshesTempCounter = 0;
}

// Called in the end of the rendering
void Renderer::StopCalculatingStats()
{
	// update counters
	m_frameCount++;
	m_timePassed += g_context->GetSubsystem<Timer>()->GetDeltaTime();

	if (m_timePassed >= 1000)
	{
		// calculate fps
		m_fps = (float)m_frameCount / (m_timePassed / 1000.0f);

		// reset counters
		m_frameCount = 0;
		m_timePassed = 0;
	}

	// meshes rendered
	m_renderedMeshesPerFrame = m_renderedMeshesTempCounter;
}
//====================================
//===============================================================================================================